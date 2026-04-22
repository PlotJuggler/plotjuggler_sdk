/**
 * @file arrow.hpp
 * @brief SDK helpers around the Arrow C Data Interface types declared in
 *        pj_base/plugin_data_api.h.
 *
 * The v4 ABI exposes raw `struct ArrowSchema`, `struct ArrowArray`, and
 * `struct ArrowArrayStream` POD structs at its surface. These carry their
 * own producer-owned `release` callback per the Arrow spec, so ownership
 * is always explicit — no free() mismatches, no allocator confusion.
 *
 * Plugin authors who'd rather not write `if (x.release) x.release(&x);`
 * everywhere use the RAII holders below. They are move-only wrappers
 * that call `release` on destruction. A moved-from holder is inert
 * (release is a no-op).
 *
 * Usage sketch:
 *   PJ::sdk::ArrowSchemaHolder schema;
 *   PJ::sdk::ArrowArrayHolder  array;
 *   auto status = toolbox.readSeriesArrow(field, schema.out(), array.out());
 *   if (!status) { ... }
 *   // use schema.get() / array.get() for read-only access;
 *   // release() is called automatically at scope exit.
 *
 * These wrappers are deliberately zero-dependency (stdlib only) so the
 * SDK surface stays tiny. Plugins that want richer builders link
 * nanoarrow themselves and use nanoarrow::UniqueSchema etc.
 */
#pragma once

#include <cstring>
#include <utility>

#include "pj_base/plugin_data_api.h"  // ArrowSchema, ArrowArray, ArrowArrayStream

namespace PJ::sdk {

namespace detail {

/// RAII holder template for the three Arrow C Data Interface POD types.
///
/// Each Arrow struct has:
///   - a `release` function pointer (nullable; null = already released / inert)
///   - `private_data` managed by whoever set `release`
///
/// The holder owns the struct by value and invokes `release` on destruction
/// iff `release != nullptr`. The release callback is spec'd to set
/// `release = nullptr` after running, so re-release is safe.
template <class T>
class ArrowHolder {
 public:
  ArrowHolder() noexcept : raw_{} {}

  /// Take ownership of an already-populated struct (e.g. returned by nanoarrow).
  /// The holder will release it on destruction.
  explicit ArrowHolder(T raw) noexcept : raw_(raw) {}

  ArrowHolder(const ArrowHolder&) = delete;
  ArrowHolder& operator=(const ArrowHolder&) = delete;

  ArrowHolder(ArrowHolder&& other) noexcept : raw_(other.raw_) {
    other.raw_ = {};
  }

  ArrowHolder& operator=(ArrowHolder&& other) noexcept {
    if (this != &other) {
      reset();
      raw_ = other.raw_;
      other.raw_ = {};
    }
    return *this;
  }

  ~ArrowHolder() noexcept {
    reset();
  }

  /// Release the underlying struct (if held) and return to empty state.
  void reset() noexcept {
    if (raw_.release != nullptr) {
      raw_.release(&raw_);
      // Per Arrow spec, release is expected to set raw_.release = nullptr.
      // Defensive: clear it ourselves in case the producer didn't.
      raw_.release = nullptr;
    }
  }

  /// Pointer to the internal struct for host vtable out-params. The holder
  /// retains ownership; callers MUST NOT invoke release themselves.
  [[nodiscard]] T* out() noexcept {
    reset();  // drop any previously-held struct before overwriting
    return &raw_;
  }

  /// Read-only access to the internal struct.
  [[nodiscard]] const T* get() const noexcept {
    return &raw_;
  }

  /// Mutable access (rarely needed; prefer get() + out()).
  [[nodiscard]] T* get() noexcept {
    return &raw_;
  }

  /// True if the holder currently owns a struct (has a non-null release).
  [[nodiscard]] bool valid() const noexcept {
    return raw_.release != nullptr;
  }

  /// Relinquish ownership without releasing. Caller receives the raw struct
  /// and becomes responsible for invoking its release callback.
  [[nodiscard]] T release() noexcept {
    T out = raw_;
    raw_ = {};
    return out;
  }

 private:
  T raw_;
};

}  // namespace detail

/// RAII wrapper for `struct ArrowSchema`. Auto-releases on destruction.
using ArrowSchemaHolder = detail::ArrowHolder<::ArrowSchema>;

/// RAII wrapper for `struct ArrowArray`. Auto-releases on destruction.
using ArrowArrayHolder = detail::ArrowHolder<::ArrowArray>;

/// RAII wrapper for `struct ArrowArrayStream`. Auto-releases on destruction.
///
/// Recommended usage: hand the holder by rvalue reference to the
/// `appendArrowStream(ArrowStreamHolder&&, ...)` overload on
/// `SourceWriteHostView` / `ToolboxHostView`, which disarms the holder on
/// success:
///
///   ArrowStreamHolder stream(buildMyStream());
///   auto status = writeHost.appendArrowStream(topic, std::move(stream), "timestamp");
///   // on success, holder is inert; on failure, destructor releases the stream.
///
/// The raw-pointer overload of `appendArrowStream` remains as an ABI escape
/// hatch for callers that own the stream through some other mechanism.
using ArrowStreamHolder = detail::ArrowHolder<::ArrowArrayStream>;

}  // namespace PJ::sdk
