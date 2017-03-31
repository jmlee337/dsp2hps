#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <regex>
#include <Windows.h>
#include <boost/endian/arithmetic.hpp>
#include <boost/program_options.hpp>
#include <sys/stat.h>

using namespace std;

namespace po = boost::program_options;
namespace en = boost::endian;

const en::big_uint32_t kDefaultSampleRate = 32000;

// must be divisible by 64
const int kBlockSize = 0x00010000;
const int kBytesPerBlock = kBlockSize + 0x20;
const int kReadSize = kBlockSize / 2;

// kReadSize is divisible by 32, so no fraction will be truncated here
// will be a multiple of 56
const int kSamplesPerBlock = kReadSize * 14 / 8;

struct DecodeCoefficients {
  en::big_int16_t decodeCoeffs[16];
  en::big_int16_t hist1;
  en::big_int16_t hist2;
};

// Pads a length to be 0x20/32 byte aligned
int calculatePadded(int length) {
  return ((length + 0x20 - 1) / 0x20) * 0x20;
}

// Header Format (0x10 bytes)
// 0x00: magic constant
// 0x08: sample rate
// 0x0C: number of channels
void writeHeader(ofstream &outfile, en::big_uint32_t sampleRate) {
  char magicWords[8] = { ' ', 'H', 'A', 'L', 'P', 'S', 'T', '\0' };
  outfile.write(magicWords, 8);

  char *sampleRateBytes = (char *)&sampleRate;
  outfile.write(sampleRateBytes, 4);

  en::big_uint32_t numChannels = 2;
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

  en::big_uint32_t maxBlockLength = kBlockSize;
  char *maxBlockLengthBytes = (char *)&maxBlockLength;
  outfile.write(maxBlockLengthBytes, 4);

  en::big_uint32_t unknownField1 = 2;
  char *unknownField1Bytes = (char *)&unknownField1;
  outfile.write(unknownField1Bytes, 4);

  char numSamples[4];
  dsp.read(numSamples, 4);
  outfile.write(numSamples, 4);

  en::big_uint32_t unknownField2 = 2;
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
// loopPoint specifies that this is the last block in the file and where to loop to if not null.
void writeBlockHeader(ofstream &outfile, int readBytes, en::big_uint32_t *loopBlock) {
  cout << "block: 0x" << hex << outfile.tellp() << dec;
  if (loopBlock) {
    en::big_uint32_t dataLength = calculatePadded(readBytes) * 2;
    char *dataLengthBytes = (char *)&dataLength;
    outfile.write(dataLengthBytes, 4);

    en::big_uint32_t lastByte = (((readBytes * 2) + dataLength) / 2) - 1;
    char *lastByteBytes = (char *)&lastByte;
    outfile.write(lastByteBytes, 4);

    char *nextBlockBytes = (char *)loopBlock;
    outfile.write(nextBlockBytes, 4);

    cout << " length: 0x" << hex << dataLength << dec << endl;
    cout << "loop: 0x" << hex << *loopBlock << dec << endl;
  } else {
    streampos pos = outfile.tellp();

    en::big_uint32_t dataLength = readBytes * 2;
    char *dataLengthBytes = (char *)&dataLength;
    outfile.write(dataLengthBytes, 4);

    en::big_uint32_t lastByte = dataLength - 1;
    char *lastByteBytes = (char *)&lastByte;
    outfile.write(lastByteBytes, 4);

    en::big_uint32_t nextBlock = (int)pos + 0x20 + dataLength;
    char *nextBlockBytes = (char *)&nextBlock;
    outfile.write(nextBlockBytes, 4);

    cout << " length: 0x" << hex << dataLength << dec << endl;
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
    en::big_int16_t hist1, en::big_int16_t hist2) {
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

void writeBlockData(ifstream &dsp, ofstream &outfile, int readBytes, DecodeCoefficients *dc) {
  int paddedLength = calculatePadded(readBytes);
  char *dspFrames = new char[paddedLength]();
  dsp.read(dspFrames, readBytes);
  outfile.write(dspFrames, paddedLength);

  uint32_t scale;
  int cIndex;
  en::big_int16_t c1;
  en::big_int16_t c2;
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
        en::big_int16_t sample;

		en::big_int32_t sample32 = nibs[i];
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

int calculateNumBlocks(int fileSize, en::big_uint32_t loopBlock) {
  int dspSize = fileSize - 0x60;

  int shortBlockBytes = (loopBlock - 0x80) % kBytesPerBlock;
  if (shortBlockBytes > 0) {
    dspSize -= ((shortBlockBytes - 0x20) / 2);
    return ((dspSize + kReadSize - 1) / kReadSize) + 1;
  }

  // ceiling of dspSize / readSize
  return (dspSize + kReadSize - 1) / kReadSize;
}

// Calculates the beginning address of the loop block
en::big_uint32_t calculateLoopBlock(double loopPoint, en::big_uint32_t sampleRate) {
  int loopSample = round(loopPoint * sampleRate);

  // round loop sample to nearest multiple of 56
  loopSample = loopSample + (56 / 2);
  loopSample -= loopSample % 56;

  int blockBefore = floor(loopSample / kSamplesPerBlock);
  int addressBefore = blockBefore * kBytesPerBlock + 0x80;

  // blockBeforeSamples will be a multiple of 56
  int blockBeforeSamples = loopSample % kSamplesPerBlock;
  if (blockBeforeSamples == 0) {
	  return addressBefore;
  }
  // blockBeforeBytes will be a multiple of 32
  int blockBeforeBytes = 0x20 + (2 * (blockBeforeSamples * 8 / 14));
  return addressBefore + blockBeforeBytes;
}

int main(int argc, char *argv[]) {
  string leftFileName;
  string rightFileName;
  string outFileName;
  en::big_uint32_t sampleRate;
  double loopPoint;

  po::options_description desc("Options");
  desc.add_options()
    ("help", "help")
    ("left_dsp,l", po::value<string>(&leftFileName)->required(), "left dsp file")
    ("right_dsp,r", po::value<string>(&rightFileName)->required(), "right dsp file")
    ("output,o", po::value<string>(&outFileName)->required(), "output file")
    ("sample_rate", po::value<en::big_uint32_t>(&sampleRate), "set sample rate (default 32000)")
    ("loop_point", po::value<double>(&loopPoint), "set a custom loop point in seconds");
  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
      cout << desc << endl;
      return 0;
    }
    if (!vm.count("sample_rate")) {
      sampleRate = kDefaultSampleRate;
    }
  }
  catch (po::error &e) {
    cerr << e.what() << endl << endl;
    cerr << desc << endl;
    return -1;
  }

  // Validate input DSP file sizes
  struct stat leftFile;
  if (stat(leftFileName.c_str(), &leftFile) != 0) {
    cerr << "Failed to stat file, " << leftFileName << ": " << GetLastError();
    return -1;
  }
  struct stat rightFile;
  if (stat(rightFileName.c_str(), &rightFile) != 0) {
    cerr << "Failed to stat file, " << rightFileName << ": " << GetLastError();
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
  ifstream left(leftFileName, ios::in | ios::binary);
  if (!left.is_open()) {
    cerr << "Failed to open file, " << leftFileName << " for reading: " << GetLastError();
    return -1;
  }
  ifstream right(rightFileName, ios::in | ios::binary);
  if (!left.is_open()) {
    cerr << "Failed to open file, " << rightFileName << " for reading: " << GetLastError();
    return -1;
  }
  validateDSPFiles(left, right);

  // Open output file
  ofstream outfile(outFileName, ios::out | ios::binary);
  if (!outfile.is_open()) {
    cerr << "Failed to open file, " << outFileName << " for writing: " << GetLastError();
    return -1;
  }

  // HSP Format
  // 0x00: Header
  // 0x10: Left channel info
  // 0x48: Right channel info
  // 0x80: Blocks begin
  writeHeader(outfile, sampleRate);
  DecodeCoefficients *leftDc = writeChannelInfo(left, outfile);
  DecodeCoefficients *rightDc = writeChannelInfo(right, outfile);

  left.seekg(0x60);
  right.seekg(0x60);


  en::big_int16_t leftHist1 = leftDc->hist1;
  en::big_int16_t leftHist2 = leftDc->hist2;
  en::big_int16_t rightHist1 = rightDc->hist1;
  en::big_int16_t rightHist2 = rightDc->hist2;

  double actualLoopPoint = 0;
  smatch loopFloatMatch;
  if (regex_search(leftFileName, loopFloatMatch, regex("LOOP[0-9]*\\.?[0-9]+"))) {
    string floatMatch = loopFloatMatch[0];
    actualLoopPoint = stod(floatMatch.substr(4));
  } else if (vm.count("loop_point")) {
    actualLoopPoint = loopPoint;
  }
  cout << "Using loop point: " << actualLoopPoint << endl;
  en::big_uint32_t loopBlock = calculateLoopBlock(actualLoopPoint, sampleRate);
  int numBlocks = calculateNumBlocks(fileSize, loopBlock);

  // Block Format
  // 0x00: Block Header
  // 0x0C: Left DSP decoder state
  // 0x14: Right DSP decoder state
  // 0x1C: Pad (always 0)
  // 0x20: DSP frames begin
  for (int i = 0; i < numBlocks - 1; i++) {
    int readBytes = kReadSize;

    int pos = (int)outfile.tellp();
    if (pos < loopBlock && pos + kBytesPerBlock > loopBlock) {
      readBytes = (loopBlock - pos - 0x20) / 2;
    }

    writeBlockHeader(outfile, readBytes, NULL);
    writeDecoderState(left, outfile, leftHist1, leftHist2);
    writeDecoderState(right, outfile, rightHist1, rightHist2);
    writePad(outfile);
    writeBlockData(left, outfile, readBytes, leftDc);
    leftHist1 = leftDc->hist1;
    leftHist2 = leftDc->hist2;
    writeBlockData(right, outfile, readBytes, rightDc);
    rightHist1 = rightDc->hist1;
    rightHist2 = rightDc->hist2;
  }

  int bytesLeft = fileSize - left.tellg();
  writeBlockHeader(outfile, bytesLeft, &loopBlock);
  writeDecoderState(left, outfile, leftHist1, leftHist2);
  writeDecoderState(right, outfile, rightHist1, rightHist2);
  writePad(outfile);
  writeBlockData(left, outfile, bytesLeft, leftDc);
  writeBlockData(right, outfile, bytesLeft, rightDc);

  left.close();
  right.close();
  outfile.close();

  return 0;
}