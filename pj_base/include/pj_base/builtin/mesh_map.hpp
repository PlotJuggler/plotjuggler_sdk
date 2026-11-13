/**
 * @file mesh_map.hpp
 * @brief Triangle mesh whose vertices and faces carry named channels (navigation meshes, cost meshes).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/frame_transforms.hpp"  // for Pose
#include "pj_base/builtin/point_cloud.hpp"       // for PointField (shared channel descriptor)
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// A connection between two faces that do not share an edge (stairs, ramp,
/// elevator, door, an off-mesh connection). The mesh only records it; what a
/// planner does with it is not the type's business.
struct MeshFaceLink {
  uint32_t from_face = 0;
  uint32_t to_face = 0;
  uint32_t kind = 0;  ///< Producer-defined category; viewers may colour by it.
  bool bidirectional = false;
  bool operator==(const MeshFaceLink&) const = default;
};

/// A triangle mesh whose vertices and faces carry named channels — the mesh
/// sibling of GridMap: navigation meshes, traversability / cost meshes,
/// semantic surface maps (e.g. EasyNavigation NavMap, mesh_navigation's
/// mesh_msgs).
///
/// Both element kinds are fixed-size records described with the `PointField`
/// channel model PointCloud, VoxelGrid and GridMap use, and both live in the one
/// `data` buffer:
///   vertex v : data[v * vertex_stride, + vertex_stride)
///   face   f : data[face_data_offset + f * face_stride, + face_stride)
/// Required channels: `x`, `y`, `z` (float32 or float64, count 1) when there
/// are vertices; `vertex_indices` (uint32, count 3) when there are faces, each
/// index below `vertex_count`. Any other field is a channel. A NaN in a float
/// channel means "no data"; integer channels have no empty sentinel, so a
/// producer whose source has one widens that channel to float32 with NaN.
///
/// Per-vertex channels interpolate across a face, per-face channels are flat.
/// Which channel colours the mesh, the colormap and the range are viewer-side;
/// the type carries no styling. Conventional channel names: `red`, `green`,
/// `blue`, `alpha` (vertex colour).
///
/// Vertices are in the local frame of `origin`, itself in `frame_id`.
/// `geometry_id` names the geometry revision ("" = unspecified); a
/// MeshMapUpdate applies only to the mesh carrying the same id.
///
/// `anchor` keeps `data` alive; it may be a view into the source payload or a
/// freshly allocated buffer.
struct MeshMap {
  Timestamp timestamp_ns = 0;
  std::string frame_id;  ///< Source coordinate frame; required for 3D TF resolution.
  Pose origin;           ///< Pose of the mesh's local frame in `frame_id` (identity for most producers).
  std::string geometry_id;

  uint32_t vertex_count = 0;
  uint32_t vertex_stride = 0;  ///< Bytes per vertex record.
  std::vector<PointField> vertex_fields;

  uint32_t face_count = 0;
  uint32_t face_stride = 0;       ///< Bytes per face record.
  uint64_t face_data_offset = 0;  ///< Byte offset of face 0 in `data` (>= vertex_count * vertex_stride).
  std::vector<PointField> face_fields;

  Span<const uint8_t> data;
  BufferAnchor anchor;

  std::vector<MeshFaceLink> links;
};

/// Required channel names.
inline constexpr std::string_view kMeshMapVertexIndicesField = "vertex_indices";

/// The field named @p name, or nullptr.
[[nodiscard]] inline const PointField* findField(const std::vector<PointField>& fields, std::string_view name) {
  for (const PointField& field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

}  // namespace sdk
}  // namespace PJ
