#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// PJ::Span<T> (alias of std::span) plus BitSpan, a packed-bit view over a byte
// buffer (LSB-first). Non-owning views used across the codec and ABI surface.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include "pj_base/assert.hpp"

namespace PJ {

template <typename T, std::size_t Extent = std::dynamic_extent>
using Span = std::span<T, Extent>;

/// Bit-level view over a byte buffer.
struct BitSpan {
  /// Backing bytes containing packed bits (LSB first).
  Span<const uint8_t> bytes;
  /// Bit index in `bytes` where this view starts.
  std::size_t bit_offset = 0;
  /// Number of readable bits in the view.
  std::size_t bit_length = 0;

  [[nodiscard]] constexpr bool empty() const noexcept {
    return bit_length == 0;
  }

  [[nodiscard]] constexpr std::size_t sizeBits() const noexcept {
    return bit_length;
  }

  /// Read one bit at relative index `i`.
  [[nodiscard]] constexpr bool test(std::size_t i) const {
    PJ_ASSERT(i < bit_length, "BitSpan index out of bounds");
    const std::size_t bit = bit_offset + i;
    const uint8_t byte = bytes[bit / 8];
    return (byte & (1u << (bit % 8))) != 0;
  }

  /// Return a bit-range view relative to this span.
  [[nodiscard]] constexpr BitSpan subspan(std::size_t offset_bits, std::size_t count_bits) const {
    PJ_ASSERT(offset_bits <= bit_length, "BitSpan subspan offset out of bounds");
    PJ_ASSERT(offset_bits + count_bits <= bit_length, "BitSpan subspan range out of bounds");
    return BitSpan{bytes, bit_offset + offset_bits, count_bits};
  }
};

}  // namespace PJ
