/**
 * @file mesh_map.hpp
 * @brief Triangle mesh whose vertices and faces carry named data channels (navigation, cost, semantic meshes).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/channel_info.hpp"
#include "pj_base/builtin/frame_transforms.hpp"  // for Pose
#include "pj_base/builtin/point_cloud.hpp"       // for PointField::Datatype, bytesPerElement
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// One data array of a mesh: a value (or a `count`-component vector) per
/// vertex or per face, stored tightly packed in element order: element `e`,
/// component `c` at byte `(e * count + c) * bytesPerElement(datatype)`.
/// `data.size()` is exactly `element_count * count * bytesPerElement(datatype)`,
/// where the element count is the mesh's vertex or face count (or, in a
/// MeshChannelPatch, its index count).
///
/// Conventional names: `red`, `green`, `blue`, `alpha` (uint8 vertex colour),
/// `normal` (count 3, vertex or face), `uv` (count 2, vertex).
struct MeshChannel {
  enum class Target : uint8_t {
    kVertex = 0,
    kFace = 1,
  };

  std::string name;
  Target target = Target::kFace;
  PointField::Datatype datatype = PointField::Datatype::kFloat32;
  uint32_t count = 1;  ///< components per element (3 for a normal, 2 for a uv)
  Span<const uint8_t> data;
  ChannelInfo info;
};

/// A connection between two faces that do not share an edge (stairs, ramp,
/// elevator, door, an off-mesh connection). The mesh only records it; what a
/// planner does with it is not the type's business.
struct MeshFaceLink {
  uint32_t from_face = 0;
  uint32_t to_face = 0;
  uint32_t kind = 0;  ///< producer-defined category; viewers may colour by it
  bool bidirectional = false;
  double cost = std::numeric_limits<double>::quiet_NaN();  ///< travel cost or length; NaN = unspecified
};

/// A triangle mesh whose vertices and faces carry named data channels — the
/// mesh sibling of GridMap — for navigation meshes, traversability / cost
/// meshes and semantic surface maps (e.g. EasyNavigation NavMap,
/// mesh_navigation's mesh_msgs, PLY meshes with properties).
///
/// Geometry is two arrays: `positions`, `vertex_count` interleaved x, y, z of
/// `position_type` (float32 or float64), in the local frame of `origin`, itself
/// in `frame_id`; and `indices`, `face_count` uint32 triplets, each below
/// `vertex_count`. Data lives in `channels`, one array per channel, so a
/// producer that stores one array per field hands them over without repacking.
/// Which channel colours the mesh, the colormap and the range are viewer-side.
///
/// `geometry_id` names the geometry revision ("" = unspecified); a
/// MeshMapUpdate applies only to the mesh carrying the same id. `id` names the
/// mesh within its topic ("" = the topic's only mesh); it is reserved for
/// topics that carry several meshes (tiles, blocks) replaced by id.
///
/// Every span is kept alive by `anchor`; they may view the source payload or a
/// freshly allocated buffer.
struct MeshMap {
  Timestamp timestamp_ns = 0;
  std::string frame_id;  ///< Source coordinate frame; required for 3D TF resolution.
  Pose origin;           ///< Pose of the mesh's local frame in `frame_id` (identity for most producers).
  std::string id;
  std::string geometry_id;

  uint32_t vertex_count = 0;
  PointField::Datatype position_type = PointField::Datatype::kFloat32;
  Span<const uint8_t> positions;
  uint32_t face_count = 0;
  Span<const uint8_t> indices;

  std::vector<MeshChannel> channels;  ///< (target, name) pairs are unique
  std::vector<MeshFaceLink> links;
  BufferAnchor anchor;
};

/// The channel with this target and name, or nullptr.
[[nodiscard]] inline const MeshChannel* findChannel(
    const std::vector<MeshChannel>& channels, MeshChannel::Target target, std::string_view name) {
  for (const MeshChannel& channel : channels) {
    if (channel.target == target && channel.name == name) {
      return &channel;
    }
  }
  return nullptr;
}

}  // namespace sdk
}  // namespace PJ
