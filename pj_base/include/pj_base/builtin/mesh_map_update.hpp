/**
 * @file mesh_map_update.hpp
 * @brief Sparse channel patch for a previously-published MeshMap.
 *
 * MeshMapUpdate is the delta counterpart to MeshMap, as OccupancyGridUpdate is
 * to OccupancyGrid: it carries new channel values for listed vertices or faces
 * and no geometry, so it is not independently drawable. A stateful consumer
 * pairs it with the base mesh (by topic-name convention, `<base>_updates` <->
 * `<base>`) and applies it only when `geometry_id` matches the base mesh's.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/point_cloud.hpp"  // for PointField
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// New channel values for some elements of a base MeshMap. `data` holds
/// `count` records of `stride` bytes; with records, `fields` must contain
/// `index` (uint32, count 1), the vertex or face index the record updates.
/// Every other field is a channel overwriting the same-named channel of the
/// base mesh; a channel the base lacks is added, with "no data" (NaN) for
/// unlisted elements.
struct MeshMapUpdate {
  enum class Target : uint8_t {
    kFaces = 0,
    kVertices = 1,
  };

  Timestamp timestamp_ns = 0;
  std::string frame_id;     ///< Must match the base mesh's frame.
  std::string geometry_id;  ///< Must match the base mesh's; "" = unspecified (applies to any).
  Target target = Target::kFaces;
  uint32_t count = 0;   ///< Number of records.
  uint32_t stride = 0;  ///< Bytes per record.
  std::vector<PointField> fields;
  Span<const uint8_t> data;
  BufferAnchor anchor;
};

/// Required channel name.
inline constexpr std::string_view kMeshMapUpdateIndexField = "index";

}  // namespace sdk
}  // namespace PJ
