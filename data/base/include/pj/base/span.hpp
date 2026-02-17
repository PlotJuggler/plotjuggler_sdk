#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "pj/base/assert.hpp"

namespace pj {

template <typename T>
class Span {
 public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using size_type = std::size_t;
  using pointer = T*;
  using reference = T&;
  using iterator = T*;

  constexpr Span() noexcept : data_(nullptr), size_(0) {}

  constexpr Span(pointer data, size_type size) noexcept
      : data_(data), size_(size) {}

  template <std::size_t N>
  constexpr Span(element_type (&arr)[N]) noexcept : data_(arr), size_(N) {}

  template <typename U, std::size_t N,
            typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
  constexpr Span(std::array<U, N>& arr) noexcept : data_(arr.data()), size_(N) {}

  template <typename U, std::size_t N,
            typename = std::enable_if_t<std::is_convertible_v<const U (*)[], T (*)[]>>>
  constexpr Span(const std::array<U, N>& arr) noexcept
      : data_(arr.data()), size_(N) {}

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
  constexpr Span(std::vector<U>& v) noexcept : data_(v.data()), size_(v.size()) {}

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<const U (*)[], T (*)[]>>>
  constexpr Span(const std::vector<U>& v) noexcept
      : data_(v.data()), size_(v.size()) {}

  [[nodiscard]] constexpr pointer data() const noexcept { return data_; }
  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] constexpr reference operator[](size_type idx) const {
    PJ_ASSERT(idx < size_, "Span index out of bounds");
    return data_[idx];
  }

  [[nodiscard]] constexpr reference front() const {
    PJ_ASSERT(size_ > 0, "Span is empty");
    return data_[0];
  }

  [[nodiscard]] constexpr reference back() const {
    PJ_ASSERT(size_ > 0, "Span is empty");
    return data_[size_ - 1];
  }

  [[nodiscard]] constexpr iterator begin() const noexcept { return data_; }
  [[nodiscard]] constexpr iterator end() const noexcept { return data_ + size_; }

  [[nodiscard]] constexpr Span<T> subspan(size_type offset,
                                          size_type count) const noexcept {
    PJ_ASSERT(offset <= size_, "Span subspan offset out of bounds");
    PJ_ASSERT(offset + count <= size_, "Span subspan range out of bounds");
    return Span<T>(data_ + offset, count);
  }

 private:
  pointer data_;
  size_type size_;
};

}  // namespace pj
