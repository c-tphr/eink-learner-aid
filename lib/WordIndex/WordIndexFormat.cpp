#include "WordIndexFormat.h"

#include <cstring>

namespace wordindex {

namespace {

constexpr uint32_t HEADER_CRC_AT = HEADER_BYTES - 4;

// Half-byte table: 64 bytes of flash instead of 1 KB, fast enough for a few KB per open.
constexpr uint32_t CRC_NIBBLES[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                      0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                      0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

}  // namespace

uint32_t crc32Update(uint32_t crc, const uint8_t* data, const size_t length) {
  crc = ~crc;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    crc = (crc >> 4) ^ CRC_NIBBLES[crc & 0x0F];
    crc = (crc >> 4) ^ CRC_NIBBLES[crc & 0x0F];
  }
  return ~crc;
}

void encodeHeader(const Header& h, uint8_t out[HEADER_BYTES]) {
  std::memset(out, 0, HEADER_BYTES);
  std::memcpy(out, MAGIC, sizeof(MAGIC));
  out[4] = h.formatVersion;
  out[5] = h.rulesVersion;
  writeU16(out + 6, HEADER_BYTES);
  writeU16(out + 8, SLOT_BYTES);
  writeU32(out + 12, h.sourceBytes);
  writeU32(out + 16, h.sourceCrc32);
  writeU32(out + 20, h.visibleCodepoints);
  writeU32(out + 24, h.wordCount);
  writeU32(out + 28, h.slotCount);
  writeU32(out + 32, h.blockCount);
  writeU32(out + 36, h.directoryOffset);
  writeU32(out + 40, h.anchorCount);
  writeU32(out + 44, h.anchorOffset);
  writeU32(out + 48, h.anchorBytes);
  writeU32(out + 52, h.markerCount);
  writeU32(out + 56, h.markerOffset);
  writeU32(out + 60, h.fileBytes);
  writeU32(out + 64, h.tablesCrc32);
  writeU32(out + HEADER_CRC_AT, crc32Update(0, out, HEADER_CRC_AT));
}

HeaderCheck decodeHeader(const uint8_t in[HEADER_BYTES], Header& out) {
  if (std::memcmp(in, MAGIC, sizeof(MAGIC)) != 0) return HeaderCheck::BadMagic;
  if (in[4] != FORMAT_VERSION) return HeaderCheck::OtherFormat;
  if (readU32(in + HEADER_CRC_AT) != crc32Update(0, in, HEADER_CRC_AT)) return HeaderCheck::BadCrc;
  if (readU16(in + 6) != HEADER_BYTES || readU16(in + 8) != SLOT_BYTES) return HeaderCheck::BadLayout;
  Header h;
  h.formatVersion = in[4];
  h.rulesVersion = in[5];
  h.sourceBytes = readU32(in + 12);
  h.sourceCrc32 = readU32(in + 16);
  h.visibleCodepoints = readU32(in + 20);
  h.wordCount = readU32(in + 24);
  h.slotCount = readU32(in + 28);
  h.blockCount = readU32(in + 32);
  h.directoryOffset = readU32(in + 36);
  h.anchorCount = readU32(in + 40);
  h.anchorOffset = readU32(in + 44);
  h.anchorBytes = readU32(in + 48);
  h.markerCount = readU32(in + 52);
  h.markerOffset = readU32(in + 56);
  h.fileBytes = readU32(in + 60);
  h.tablesCrc32 = readU32(in + 64);
  out = h;
  return HeaderCheck::Ok;
}

}  // namespace wordindex
