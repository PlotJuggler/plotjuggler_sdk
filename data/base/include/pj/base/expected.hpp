#pragma once

#include <type_traits>
#include <utility>
#include <variant>

#include "pj/base/assert.hpp"

namespace pj {

template <typename E>
class Unexpected {
 public:
  constexpr explicit Unexpected(E error) : error_(std::move(error)) {}

  [[nodiscard]] constexpr const E& value() const& noexcept { return error_; }
  [[nodiscard]] constexpr E& value() & noexcept { return error_; }
  [[nodiscard]] constexpr E&& value() && noexcept { return std::move(error_); }

 private:
  E error_;
};

template <typename E>
[[nodiscard]] constexpr Unexpected<std::decay_t<E>> unexpected(E&& error) {
  return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

template <typename T, typename E = std::string>
class Expected {
 public:
  constexpr Expected(const T& value) : storage_(value) {}
  constexpr Expected(T&& value) : storage_(std::move(value)) {}

  constexpr Expected(const Unexpected<E>& error) : storage_(error.value()) {}
  constexpr Expected(Unexpected<E>&& error) : storage_(std::move(error).value()) {}

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<T>(storage_);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] constexpr T& value() & {
    PJ_ASSERT(has_value(), "Expected does not contain a value");
    return std::get<T>(storage_);
  }

  [[nodiscard]] constexpr const T& value() const& {
    PJ_ASSERT(has_value(), "Expected does not contain a value");
    return std::get<T>(storage_);
  }

  [[nodiscard]] constexpr T&& value() && {
    PJ_ASSERT(has_value(), "Expected does not contain a value");
    return std::move(std::get<T>(storage_));
  }

  [[nodiscard]] constexpr E& error() & {
    PJ_ASSERT(!has_value(), "Expected does not contain an error");
    return std::get<E>(storage_);
  }

  [[nodiscard]] constexpr const E& error() const& {
    PJ_ASSERT(!has_value(), "Expected does not contain an error");
    return std::get<E>(storage_);
  }

  [[nodiscard]] constexpr E&& error() && {
    PJ_ASSERT(!has_value(), "Expected does not contain an error");
    return std::move(std::get<E>(storage_));
  }

 private:
  std::variant<T, E> storage_;
};

}  // namespace pj
