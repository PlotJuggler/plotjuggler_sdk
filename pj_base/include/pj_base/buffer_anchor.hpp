/**
 * @file buffer_anchor.hpp
 * @brief Zero-copy ownership pattern: a type-erased anchor + a non-owning view.
 *
 * BufferAnchor + PayloadView are the general-purpose ownership token used
 * across the SDK to let consumers hold a span of bytes without committing to
 * a copy. The producer owns the underlying allocation (typically through a
 * shared_ptr<vector<uint8_t>>); the BufferAnchor erases the concrete type
 * and keeps the allocation alive while at least one anchor copy survives.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "pj_base/span.hpp"

namespace PJ {
namespace sdk {

/// Type-erased ownership token. A copy keeps the underlying allocation alive
/// while at least one BufferAnchor referencing it exists. The concrete type
/// erased is typically std::shared_ptr<std::vector<uint8_t>>, but any
/// shared_ptr<T> works — consumers never need to know which.
using BufferAnchor = std::shared_ptr<const void>;

/// Non-owning view + ownership anchor of a payload buffer. The view is valid
/// for as long as a copy of `anchor` is alive somewhere. Producers fabricate
/// these when handing bytes to a consumer that needs to outlive the call
/// (e.g. a parser returning a sdk::Image whose pixel span references the
/// same buffer).
///
/// `anchor` may be empty when the caller does not share ownership — in that
/// case the consumer must materialize any bytes it wants to retain past
/// the call.
struct PayloadView {
  Span<const uint8_t> bytes;
  BufferAnchor anchor;

  PayloadView() = default;

  PayloadView(Span<const uint8_t> bytes_, BufferAnchor anchor_) : bytes(bytes_), anchor(std::move(anchor_)) {}

  PayloadView(std::shared_ptr<const std::vector<uint8_t>> buffer)
      : bytes(buffer ? Span<const uint8_t>(buffer->data(), buffer->size()) : Span<const uint8_t>()),
        anchor(std::move(buffer)) {}
};

/// Convenience helper for producers whose only payload is a plain
/// std::vector<uint8_t>. Wraps it in a shared_ptr (which becomes both the
/// owner of the bytes and the type-erased anchor), and returns a PayloadView
/// whose Span points at that vector's contents. Use this when the producer
/// materializes bytes from a source that has no natural anchor (e.g. a
/// C-ABI fetch returning a raw byte buffer); when an upstream allocation
/// already exists (a chunk cache, an mmap region), construct the PayloadView
/// directly with that anchor to avoid the helper's copy.
inline PayloadView makePayloadView(std::vector<uint8_t> bytes) {
  auto shared = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
  return PayloadView{
      Span<const uint8_t>{shared->data(), shared->size()},
      BufferAnchor{shared},
  };
}

}  // namespace sdk
}  // namespace PJ
