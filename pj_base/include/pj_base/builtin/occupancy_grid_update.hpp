/**
 * @file occupancy_grid_update.hpp
 * @brief Incremental sub-rectangle patch for a previously-published OccupancyGrid.
 *
 * OccupancyGridUpdate is the delta counterpart to OccupancyGrid: a row-major
 * patch covering a sub-window of a base grid. It deliberately carries NO
 * origin/resolution — the patch is not independently placeable. A stateful
 * consumer pairs it with the base grid (by topic-name convention, e.g.
 * `<base>/costmap_updates` <-> `<base>/costmap`) and positions it using the
 * base's origin + resolution. Byte-backed: cell bytes live behind a
 * `Span<const uint8_t>` plus `BufferAnchor`, the same pattern as OccupancyGrid.
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

/// A sub-rectangle patch into a base OccupancyGrid. Cell values are signed
/// 8-bit integers stored row-major in `data` (-1 unknown, 0..100 occupancy
/// percent); `data.size()` must equal `width * height`. The patch carries no
/// origin/resolution: the consumer places it at the base grid's
/// `origin + (x, y) * resolution`.
struct OccupancyGridUpdate {
  Timestamp timestamp_ns = 0;
  std::string frame_id;      ///< Must match the base grid's frame.
  int32_t x = 0;             ///< Column offset (cells) of the patch top-left into the base grid.
  int32_t y = 0;             ///< Row offset (cells) of the patch top-left into the base grid.
  uint32_t width = 0;        ///< Patch width in cells.
  uint32_t height = 0;       ///< Patch height in cells.
  Span<const uint8_t> data;  ///< Row-major signed-8-bit cells; size() == width * height.
  BufferAnchor anchor;       ///< Keeps `data` alive; no wire equivalent.
};

}  // namespace sdk
}  // namespace PJ
