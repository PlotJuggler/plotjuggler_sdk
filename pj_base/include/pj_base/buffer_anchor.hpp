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
};

}  // namespace sdk
}  // namespace PJ
