/**
 * @file compressed_point_cloud.hpp
 * @brief Point cloud delivered in a compressed binary format (Draco, ...).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Point cloud delivered in a compressed binary format. Unlike PointCloud,
/// the wire layout is opaque to PlotJuggler — `data` + `format` must be
/// handed to the matching decoder library (e.g. Draco), which produces a
/// decompressed point set on the host side.
///
/// This type is distinct from PointCloud because per-format decoders carry
/// their own attribute table and layout semantics. Same reasoning that
/// separates VideoFrame from Image.
///
/// `anchor` keeps the underlying buffer alive — the producer may have made
/// `data` a view into the source payload (zero-copy) or into a freshly
/// allocated vector; consumers don't need to know which.
struct CompressedPointCloud {
  Timestamp timestamp_ns = 0;
  std::string frame_id;
  std::string format;  ///< Codec identifier, lowercase. Recognized values include "draco".
  Span<const uint8_t> data;
  BufferAnchor anchor;
};

}  // namespace sdk
}  // namespace PJ
