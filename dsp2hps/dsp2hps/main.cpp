#include <fstream>
#include <iostream>
#include <string.h>
#include <Windows.h>
#include <boost/endian/arithmetic.hpp>
#include <sys/stat.h>

using namespace std;

const int kBlockSize = 0x00010000;
const int kReadSize = kBlockSize / 2;

void writeHeader(ofstream &outfile);
void writeChannelInfo(ifstream &dsp, ofstream &outfile);
void writeBlockHeader(ofstream &outfile, int readBytes, bool last);
void writeDecoderState(ifstream &dsp, ofstream &outfile);
void writePad(ofstream &outfile);
void writeBlockData(ifstream &dsp, ofstream &outfile, int readBytes);
void validateDSPFiles(ifstream &left, ifstream &right);
int calculateNumBlocks(int fileSize);
int calculatePadded(int length);

int main(int argc, char *argv[]) {
  // Validate usage
  if (argc != 4) {
    cout << "three arguments required, found " << argc - 1;
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
  writeChannelInfo(left, outfile);
  writeChannelInfo(right, outfile);

  left.seekg(0x60);
  right.seekg(0x60);

  // Block Format
  // 0x00: Block Header
  // 0x0C: Left DSP decoder state
  // 0x14: Right DSP decoder state
  // 0x1C: Pad (always 0)
  // 0x20: DSP frames begin
  int numBlocks = calculateNumBlocks(fileSize);
  for (int i = 0; i < numBlocks - 1; i++) {
    writeBlockHeader(outfile, kReadSize, false);
    writeDecoderState(left, outfile);
    writeDecoderState(right, outfile);
    writePad(outfile);
    writeBlockData(left, outfile, kReadSize);
    writeBlockData(right, outfile, kReadSize);
  }
  int bytesLeft = fileSize - left.tellg();
  writeBlockHeader(outfile, bytesLeft, true);
  writeDecoderState(left, outfile);
  writeDecoderState(right, outfile);
  writePad(outfile);
  writeBlockData(left, outfile, bytesLeft);
  writeBlockData(right, outfile, bytesLeft);

  left.close();
  right.close();
  outfile.close();

  return 0;
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
void writeChannelInfo(ifstream &dsp, ofstream &outfile) {
  streampos pos = dsp.tellg();
  dsp.seekg(0);

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
  char decodeCoeffs[0x20];
  dsp.read(decodeCoeffs, 0x20);
  outfile.write(decodeCoeffs, 0x20);

  // Initial Decoder State Format (0x08 bytes)
  // 0x00: P/S high byte or gain??? (always 0)
  // 0x02: Initial P/S (from first DSP frame header)
  // 0x04: Initial hist1 (always 0)
  // 0x06: Initial hist2 (always 0)
  dsp.seekg(0x60);
  char initialPS;
  dsp.read(&initialPS, 1);
  char decodeState[8] = { 0, 0, 0, initialPS, 0, 0, 0, 0 };
  outfile.write(decodeState, 8);

  dsp.seekg(pos);
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

    boost::endian::big_uint32_t lastByte = ((readBytes * 2) + dataLength) / 2;
    char *lastByteBytes = (char *)&lastByte;
    outfile.write(lastByteBytes, 4);

    boost::endian::big_uint32_t nextBlock = 0x80;
    char *nextBlockBytes = (char *)&nextBlock;
    outfile.write(nextBlockBytes, 4);
  }
  else {
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
void writeDecoderState(ifstream &dsp, ofstream &outfile) {
  streampos pos = dsp.tellg();

  char PSByte;
  dsp.read(&PSByte, 1);
  char decoderState[8] = { 0, PSByte, 0, 0, 0, 0, 0, 0 };
  outfile.write(decoderState, 8);

  dsp.seekg(pos);
}

void writePad(ofstream &outfile) {
  char padBytes[4] = { 0, 0, 0, 0 };
  outfile.write(padBytes, 4);
}

void writeBlockData(ifstream &dsp, ofstream &outfile, int readBytes) {
  int paddedLength = calculatePadded(readBytes);
  char *dspFrames = new char[paddedLength]();
  dsp.read(dspFrames, readBytes);
  outfile.write(dspFrames, paddedLength);
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

int calculatePadded(int length) {
  return ((length + 0x20 - 1) / 0x20) * 0x20;
}