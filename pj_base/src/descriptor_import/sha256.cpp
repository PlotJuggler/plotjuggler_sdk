// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Straight FIPS 180-4 SHA-256. Words are assembled/emitted with explicit
// shifts (no endian assumptions, no type punning); the whole message is
// processed in one pass over 512-bit blocks with the standard 9..73-byte
// length padding tail.

#include "descriptor_import/sha256.hpp"

#include <cstddef>

namespace PJ {
namespace sdk {
namespace descriptor_import {
namespace detail {

namespace {

// FIPS 180-4 §4.2.2: first 32 bits of the fractional parts of the cube roots
// of the first 64 primes.
constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) {
  return (x >> n) | (x << (32u - n));
}

// One 512-bit block: message schedule W[64] + 64 compression rounds (§6.2.2).
void compress(std::uint32_t (&h)[8], const std::uint8_t* block) {
  std::uint32_t w[64];
  for (std::size_t t = 0; t < 16; ++t) {
    w[t] = (static_cast<std::uint32_t>(block[4 * t]) << 24) | (static_cast<std::uint32_t>(block[4 * t + 1]) << 16) |
           (static_cast<std::uint32_t>(block[4 * t + 2]) << 8) | static_cast<std::uint32_t>(block[4 * t + 3]);
  }
  for (std::size_t t = 16; t < 64; ++t) {
    const std::uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
    const std::uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
    w[t] = w[t - 16] + s0 + w[t - 7] + s1;
  }

  std::uint32_t a = h[0];
  std::uint32_t b = h[1];
  std::uint32_t c = h[2];
  std::uint32_t d = h[3];
  std::uint32_t e = h[4];
  std::uint32_t f = h[5];
  std::uint32_t g = h[6];
  std::uint32_t hh = h[7];
  for (std::size_t t = 0; t < 64; ++t) {
    const std::uint32_t big_s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = hh + big_s1 + ch + kK[t] + w[t];
    const std::uint32_t big_s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = big_s0 + maj;
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
  h[5] += f;
  h[6] += g;
  h[7] += hh;
}

}  // namespace

std::array<std::uint8_t, 32> sha256(std::string_view data) {
  // §5.3.3 initial hash: first 32 bits of the fractional parts of the square
  // roots of the first 8 primes.
  std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
  const std::size_t n = data.size();

  std::size_t off = 0;
  for (; off + 64 <= n; off += 64) {
    compress(h, bytes + off);
  }

  // Padding: 0x80, zeros to 56 mod 64, then the bit length as big-endian u64.
  // The tail spans one block, or two when fewer than 9 bytes remain free.
  std::uint8_t tail[128] = {};
  const std::size_t rem = n - off;
  for (std::size_t i = 0; i < rem; ++i) {
    tail[i] = bytes[off + i];
  }
  tail[rem] = 0x80;
  const std::size_t tail_blocks = (rem + 1 + 8 <= 64) ? 1 : 2;
  const std::uint64_t bit_len = static_cast<std::uint64_t>(n) * 8;
  for (std::size_t i = 0; i < 8; ++i) {
    tail[tail_blocks * 64 - 1 - i] = static_cast<std::uint8_t>(bit_len >> (8 * i));
  }
  compress(h, tail);
  if (tail_blocks == 2) {
    compress(h, tail + 64);
  }

  std::array<std::uint8_t, 32> digest{};
  for (std::size_t i = 0; i < 8; ++i) {
    digest[4 * i] = static_cast<std::uint8_t>(h[i] >> 24);
    digest[4 * i + 1] = static_cast<std::uint8_t>(h[i] >> 16);
    digest[4 * i + 2] = static_cast<std::uint8_t>(h[i] >> 8);
    digest[4 * i + 3] = static_cast<std::uint8_t>(h[i]);
  }
  return digest;
}

}  // namespace detail
}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
