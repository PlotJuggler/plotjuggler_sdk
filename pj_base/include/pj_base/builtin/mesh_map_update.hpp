/**
 * @file mesh_map_update.hpp
 * @brief Channel patches for a previously-published MeshMap.
 *
 * MeshMapUpdate is the delta counterpart to MeshMap, as OccupancyGridUpdate is
 * to OccupancyGrid: it carries new channel values and no geometry, so it is not
 * independently drawable. A stateful consumer pairs it with the base mesh and
 * applies it only when `geometry_id` matches (an empty id on either side
 * matches anything). Pairing: an update topic belongs to the mesh whose
 * `geometry_id` it carries; a consumer that cannot tell (ids unset) uses the
 * topic-name convention `<base>_updates` <-> `<base>`.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/mesh_map.hpp"  // for MeshChannel
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// New values for one channel. `indices` (little-endian 4-byte uint32, no
/// alignment guarantee, may be empty) lists the
/// vertices or faces `channel.data` covers, in order; empty means dense: the
/// data replaces the whole channel and must hold one element per vertex or
/// face of the base mesh. A channel the base mesh lacks is added, with "no
/// data" (NaN, or `channel.info.no_data`) for elements a sparse patch does not
/// list. `channel.info` replaces the base channel's.
struct MeshChannelPatch {
  MeshChannel channel;
  Span<const uint8_t> indices;
};

/// One or more channel patches for the mesh `id` / `geometry_id` names.
struct MeshMapUpdate {
  Timestamp timestamp_ns = 0;
  std::string frame_id;  ///< the base mesh's frame (informative)
  std::string id;        ///< the base mesh's `id`
  std::string geometry_id;
  std::vector<MeshChannelPatch> patches;  ///< (target, name) pairs are unique
  BufferAnchor anchor;
};

}  // namespace sdk
}  // namespace PJ
