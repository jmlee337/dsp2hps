#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <Windows.h>
#include <boost/endian/arithmetic.hpp>
#include <sys/stat.h>

using namespace std;

const int kBlockSize = 0x00010000;
const int kReadSize = kBlockSize / 2;

struct DecodeCoefficients {
  boost::endian::big_int16_t decodeCoeffs[16];
  boost::endian::big_int16_t hist1;
  boost::endian::big_int16_t hist2;
};

// Pads a length to be 0x20/32 byte aligned
int calculatePadded(int length) {
  return ((length + 0x20 - 1) / 0x20) * 0x20;
}

// Header Format (0x10 bytes)
// 0x00: magic constant
// 0x08: sample rate
// 0x0C: number of channels
void writeHeader(ofstream &outfile) {
  char magicWords[8] = { ' ', 'H', 'A', 'L', 'P', 'S', 'T', '\0' };
  outfile.write(magicWords, 8);

  boost::endian::big_uint32_t sampleRate = 32000;
  char *sampleRateBytes = (char *)&sampleRate;
  outfile.write(sampleRateBytes, 4);

  boost::endian::big_uint32_t numChannels = 2;
  char *numChannelsBytes = (char *)&numChannels;
  outfile.write(numChannelsBytes, 4);
}

// Channel Info Format (0x38 bytes)
// 0x00: length of largest block
// 0x04: ??? (always 2)
// 0x08: number of DSP samples
// 0x0C: ??? (always 2)
// 0x10: DSP decode coefficients
// 0x30: initial DSP decoder state
//
// Does not mutate istream position
DecodeCoefficients *writeChannelInfo(ifstream &dsp, ofstream &outfile) {
  streampos pos = dsp.tellg();
  dsp.seekg(0);

  DecodeCoefficients *dc = new DecodeCoefficients();

  boost::endian::big_uint32_t maxBlockLength = kBlockSize;
  char *maxBlockLengthBytes = (char *)&maxBlockLength;
  outfile.write(maxBlockLengthBytes, 4);

  boost::endian::big_uint32_t unknownField1 = 2;
  char *unknownField1Bytes = (char *)&unknownField1;
  outfile.write(unknownField1Bytes, 4);

  char numSamples[4];
  dsp.read(numSamples, 4);
  outfile.write(numSamples, 4);

  boost::endian::big_uint32_t unknownField2 = 2;
  char *unknownField2Bytes = (char *)&unknownField2;
  outfile.write(unknownField2Bytes, 4);

  dsp.seekg(0x1C);
  dsp.read((char *)dc->decodeCoeffs, 0x20);
  outfile.write((char *)dc->decodeCoeffs, 0x20);

  // dsp position is 0x3C
  char decodeState[8];
  dsp.read(decodeState, 8);
  outfile.write(decodeState, 8);

  dsp.seekg(0x40);
  dsp.read((char *)&dc->hist1, 2);
  dsp.read((char *)&dc->hist2, 2);

  dsp.seekg(pos);
  return dc;
}

// Block Header Format (0x20 bytes)
// 0x00: length of DSP data (length of block - length of header)
// 0x04: last byte to read in the block???
// 0x08: address of next block to read (offset from beginning of file)
//
// Does not mutate istream position
void writeBlockHeader(ofstream &outfile, int readBytes, bool last) {
  if (last) {
    boost::endian::big_uint32_t dataLength = calculatePadded(readBytes) * 2;
    char *dataLengthBytes = (char *)&dataLength;
    outfile.write(dataLengthBytes, 4);

    boost::endian::big_uint32_t lastByte = (((readBytes * 2) + dataLength) / 2) - 1;
    char *lastByteBytes = (char *)&lastByte;
    outfile.write(lastByteBytes, 4);

    boost::endian::big_uint32_t nextBlock = 0x80;
    char *nextBlockBytes = (char *)&nextBlock;
    outfile.write(nextBlockBytes, 4);
  } else {
    streampos pos = outfile.tellp();

    boost::endian::big_uint32_t dataLength = readBytes * 2;
    char *dataLengthBytes = (char *)&dataLength;
    outfile.write(dataLengthBytes, 4);

    boost::endian::big_uint32_t lastByte = dataLength - 1;
    char *lastByteBytes = (char *)&lastByte;
    outfile.write(lastByteBytes, 4);

    boost::endian::big_uint32_t nextBlock = (int)pos + 0x20 + dataLength;
    char *nextBlockBytes = (char *)&nextBlock;
    outfile.write(nextBlockBytes, 4);
  }
}

// Decoder State Format (0x08 bytes)
// 0x00: P/S high byte
// 0x01: P/S
// 0x02: hist 1
// 0x04: hist 2
// 0x06: gain/scale??? (always 0)
//
// Does not mutate istream position
void writeDecoderState(ifstream &dsp, ofstream &outfile,
    boost::endian::big_int16_t hist1, boost::endian::big_int16_t hist2) {
  streampos pos = dsp.tellg();

  char zero = 0;
  outfile.write(&zero, 1);

  char PSByte;
  dsp.read(&PSByte, 1);
  outfile.write(&PSByte, 1);

  outfile.write((char *)&hist1, 2);
  outfile.write((char *)&hist2, 2);

  outfile.write(&zero, 1);
  outfile.write(&zero, 1);

  dsp.seekg(pos);
}

void writePad(ofstream &outfile) {
  char padBytes[4] = { 0, 0, 0, 0 };
  outfile.write(padBytes, 4);
}

void *writeBlockData(ifstream &dsp, ofstream &outfile, int readBytes, DecodeCoefficients *dc) {
  int paddedLength = calculatePadded(readBytes);
  char *dspFrames = new char[paddedLength]();
  dsp.read(dspFrames, readBytes);
  outfile.write(dspFrames, paddedLength);

  uint32_t scale;
  int cIndex;
  boost::endian::big_int16_t c1;
  boost::endian::big_int16_t c2;
  for (int i = 0; i < readBytes; i++) {
    if (i % 8 == 0) {
      scale = 1 << (dspFrames[i] & 0x0F);
      cIndex = (dspFrames[i] >> 4) & 0x0F;
      if (cIndex > 7) {
        cerr << "DSP has PS with invalid cIndex in block ending at: " << hex << dsp.tellg() << dec;
        exit(-1);
      }
      c1 = dc->decodeCoeffs[cIndex * 2];
      c2 = dc->decodeCoeffs[cIndex * 2 + 1];
	} else {
      int nibHi = (dspFrames[i] >> 4) & 0x0F;
      if (nibHi > 7) {
        nibHi -= 16;
      }
      int nibLo = dspFrames[i] & 0x0F;
      if (nibLo > 7) {
        nibLo -= 16;
      }

      int nibs[2] = { nibHi, nibLo };
      for (int i = 0; i < 2; i++) {
        boost::endian::big_int16_t sample;

		boost::endian::big_int32_t sample32 = nibs[i];
		sample32 *= scale;
		sample32 = sample32 << 11;
		sample32 += c1 * dc->hist1;
		sample32 += c2 * dc->hist2;
		sample32 += 1024;
		sample32 = sample32 >> 11;

        if (sample32 > 0x7FFF) {
          sample = 0x7FFF;
        } else if (sample32 < -0x8000) {
          sample = -0x8000;
        } else {
          sample = sample32;
        }

        dc->hist2 = dc->hist1;
        dc->hist1 = sample;
      }
    }
  }
  
  delete dspFrames;
  return 0;
}

void validateDSPFiles(ifstream &left, ifstream &right) {
  streampos leftPos = left.tellg();
  streampos rightPos = right.tellg();
  left.seekg(0);
  right.seekg(0);

  char leftHeader[0x1C];
  char rightHeader[0x1C];
  left.read(leftHeader, 0x1C);
  right.read(rightHeader, 0x1C);
  if (strncmp(leftHeader, rightHeader, 0x1C)) {
    cerr << "Input DSP file headers do not match";
    exit(-1);
  }

  left.seekg(leftPos);
  right.seekg(rightPos);
}

int calculateNumBlocks(int fileSize) {
  int dspSize = fileSize - 0x60;

  // ceiling of dspSize / readSize
  return (dspSize + kReadSize - 1) / kReadSize;
}

int main(int argc, char *argv[]) {
  // Validate usage
  if (argc != 4) {
    cerr << "Usage: dsp2hps.exe left_dsp right_dsp output_hps";
    return -1;
  }

  // Validate input DSP file sizes
  struct stat leftFile;
  if (stat(argv[1], &leftFile) != 0) {
    cerr << "Failed to stat file, " << argv[1] << ": " << GetLastError();
    return -1;
  }
  struct stat rightFile;
  if (stat(argv[2], &rightFile) != 0) {
    cerr << "Failed to stat file, " << argv[2] << ": " << GetLastError();
    return -1;
  }
  if (leftFile.st_size != rightFile.st_size) {
    cerr << "Input files are not the same length: "
      << leftFile.st_size << ", " << rightFile.st_size;
    return -1;
  }
  if (leftFile.st_size <= 0x60) {
    cerr << "Input files are not valid DSP files";
    return -1;
  }
  int fileSize = leftFile.st_size;

  // Validate DSP file headers
  ifstream left(argv[1], ios::in | ios::binary);
  if (!left.is_open()) {
    cerr << "Failed to open file, " << argv[1] << " for reading: " << GetLastError();
    return -1;
  }
  ifstream right(argv[2], ios::in | ios::binary);
  if (!left.is_open()) {
    cerr << "Failed to open file, " << argv[2] << " for reading: " << GetLastError();
    return -1;
  }
  validateDSPFiles(left, right);

  // Open output file
  ofstream outfile(argv[3], ios::out | ios::binary);
  if (!outfile.is_open()) {
    cerr << "Failed to open file, " << argv[3] << " for writing: " << GetLastError();
    return -1;
  }

  // HSP Format
  // 0x00: Header
  // 0x10: Left channel info
  // 0x48: Right channel info
  // 0x80: Blocks begin
  writeHeader(outfile);
  DecodeCoefficients *leftDc = writeChannelInfo(left, outfile);
  DecodeCoefficients *rightDc = writeChannelInfo(right, outfile);

  left.seekg(0x60);
  right.seekg(0x60);


  boost::endian::big_int16_t leftHist1 = leftDc->hist1;
  boost::endian::big_int16_t leftHist2 = leftDc->hist2;
  boost::endian::big_int16_t rightHist1 = rightDc->hist1;
  boost::endian::big_int16_t rightHist2 = rightDc->hist2;
  int numBlocks = calculateNumBlocks(fileSize);

  // Block Format
  // 0x00: Block Header
  // 0x0C: Left DSP decoder state
  // 0x14: Right DSP decoder state
  // 0x1C: Pad (always 0)
  // 0x20: DSP frames begin
  for (int i = 0; i < numBlocks - 1; i++) {
    writeBlockHeader(outfile, kReadSize, false);
    writeDecoderState(left, outfile, leftHist1, leftHist2);
    writeDecoderState(right, outfile, rightHist1, rightHist2);
    writePad(outfile);
    writeBlockData(left, outfile, kReadSize, leftDc);
    leftHist1 = leftDc->hist1;
    leftHist2 = leftDc->hist2;
    writeBlockData(right, outfile, kReadSize, rightDc);
    rightHist1 = rightDc->hist1;
    rightHist2 = rightDc->hist2;
  }
  int bytesLeft = fileSize - left.tellg();
  writeBlockHeader(outfile, bytesLeft, true);
  writeDecoderState(left, outfile, leftHist1, leftHist2);
  writeDecoderState(right, outfile, rightHist1, rightHist2);
  writePad(outfile);
  delete writeBlockData(left, outfile, bytesLeft, leftDc);
  delete writeBlockData(right, outfile, bytesLeft, rightDc);

  left.close();
  right.close();
  outfile.close();

  return 0;
}