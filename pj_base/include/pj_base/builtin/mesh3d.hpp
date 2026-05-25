/**
 * @file mesh3d.hpp
 * @brief 3D mesh asset in its native binary format (GLTF/STL/PLY/OBJ/USD/DAE).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/frame_transforms.hpp"   // for Pose, Vector3
#include "pj_base/builtin/image_annotations.hpp"  // for ColorRGBA
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// 3D mesh asset delivered in its native binary format. The renderer hands
/// `data` + `format` (or the contents at `url`) to a mesh-loader library
/// (Assimp, tinygltf, ...); PlotJuggler does not parse the asset itself.
///
/// Distinct from SceneEntities' TrianglePrimitive because asset formats can
/// carry richer scene content (materials, textures, skinning, animations)
/// that is not expressible as raw triangle soup.
///
/// Asset source: exactly one of `data` (with `anchor` keeping the bytes
/// alive) or `url` should be populated. When `data` is used, `format` is
/// required; when `url` is used, `format` may be inferred from the file
/// extension.
struct Mesh3D {
  Timestamp timestamp_ns = 0;
  std::string frame_id;
  std::string id;  ///< Republishing with the same id replaces the previous entry on the topic.

  Pose pose;
  Vector3 scale{1.0, 1.0, 1.0};

  std::string format;        ///< "gltf", "glb", "stl", "ply", "obj", "usd", "dae"
  Span<const uint8_t> data;  ///< Embedded asset bytes; when non-empty, `format` is required.
  BufferAnchor anchor;
  std::string url;  ///< External URL to the asset. Used when `data` is empty.

  ColorRGBA color;  ///< Applied when `override_color` is true.
  bool override_color = false;
};

}  // namespace sdk
}  // namespace PJ
