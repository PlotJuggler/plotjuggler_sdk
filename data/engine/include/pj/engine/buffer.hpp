#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "pj/base/span.hpp"

namespace pj::engine {

/// Growable byte buffer used by column/chunk encodings.
class RawBuffer {
 public:
  /// Construct an empty buffer.
  RawBuffer() = default;

  /// Construct an empty buffer reserving `initial_capacity` bytes.
  explicit RawBuffer(std::size_t initial_capacity);

  /// Ensure the buffer can hold at least `capacity` bytes without reallocation.
  void reserve(std::size_t capacity);

  /// Append `size` bytes from `data`.
  void append(const void* data, std::size_t size);

  /// Resize to exactly `new_size` bytes.
  void resize(std::size_t new_size);

  /// Reset size to zero, preserving capacity.
  void clear();

  [[nodiscard]] const uint8_t* data() const noexcept;

  [[nodiscard]] uint8_t* mutable_data() noexcept;

  [[nodiscard]] std::size_t size() const noexcept;

  [[nodiscard]] std::size_t capacity() const noexcept;

  [[nodiscard]] bool empty() const noexcept;

 private:
  std::vector<uint8_t> data_;
};

/// Owning packed validity bitmap (Arrow-compatible LSB-first layout).
class BitVector {
 public:
  /// Construct an empty bit vector.
  BitVector() = default;

  /// Required bytes for `num_bits` bits.
  [[nodiscard]] static constexpr std::size_t bytes_for_bits(std::size_t num_bits) noexcept {
    return (num_bits + 7) / 8;
  }

  /// Initialize to `num_bits` bits, all set to valid.
  void init_valid(std::size_t num_bits);

  /// Ensure capacity for at least `num_bits` bits.
  void ensure_size(std::size_t num_bits);

  /// Mark one bit as valid.
  void set_valid(std::size_t bit_index);

  /// Mark one bit as null.
  void set_null(std::size_t bit_index);

  /// Return true if one bit is valid.
  [[nodiscard]] bool is_valid(std::size_t bit_index) const;

  /// Count null bits in the first `num_bits` bits.
  [[nodiscard]] std::size_t count_nulls(std::size_t num_bits) const;

  /// Replace bytes from `bytes` and set total bit count.
  void assign_bytes(Span<const uint8_t> bytes, std::size_t bit_count);

  /// Reset to empty.
  void clear();

  /// Return non-owning bit view.
  [[nodiscard]] pj::BitSpan bit_span() const noexcept;

  /// Return underlying bytes.
  [[nodiscard]] const uint8_t* data() const noexcept;

  /// Return mutable underlying bytes.
  [[nodiscard]] uint8_t* mutable_data() noexcept;

  /// Return byte count of packed storage.
  [[nodiscard]] std::size_t size_bytes() const noexcept;

  /// Return bit count tracked by this vector.
  [[nodiscard]] std::size_t size_bits() const noexcept;

  /// Return true when no bits are stored.
  [[nodiscard]] bool empty() const noexcept;

 private:
  std::vector<uint8_t> bytes_;
  std::size_t bit_count_ = 0;
};

}  // namespace pj::engine
