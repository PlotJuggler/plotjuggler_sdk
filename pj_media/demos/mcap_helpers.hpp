#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <zstd.h>

namespace pj_demos {

/// rosbag2 supports compression_mode=MESSAGE which zstd-compresses every
/// message payload independently. Detect by magic and decompress; pass-through
/// for uncompressed bags. Magic: 0x28 0xb5 0x2f 0xfd.
///
/// Takes the input by rvalue-ref so the uncompressed (common) path moves out
/// the buffer — no copy. The compressed path produces a new vector sized to
/// the decompressed payload.
inline std::vector<uint8_t> maybeDecompressZstd(std::vector<uint8_t>&& bytes) {
  if (bytes.size() < 4 ||
      bytes[0] != 0x28 || bytes[1] != 0xb5 || bytes[2] != 0x2f || bytes[3] != 0xfd) {
    return std::move(bytes);
  }
  unsigned long long out_size = ZSTD_getFrameContentSize(bytes.data(), bytes.size());
  if (out_size == ZSTD_CONTENTSIZE_ERROR || out_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return std::move(bytes);
  }
  std::vector<uint8_t> out(static_cast<size_t>(out_size));
  size_t actual = ZSTD_decompress(out.data(), out.size(), bytes.data(), bytes.size());
  if (ZSTD_isError(actual)) {
    return std::move(bytes);
  }
  out.resize(actual);
  return out;
}

}  // namespace pj_demos
