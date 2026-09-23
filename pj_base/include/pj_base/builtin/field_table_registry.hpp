/**
 * @file field_table_registry.hpp
 * @brief Looks up a builtin struct's `FieldTable` from its runtime
 *        `BuiltinObjectType` tag, so a generic binder can go straight from
 *        a `BuiltinObject` to its field descriptors without a `switch` of
 *        its own.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>

#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/frame_transforms_fields.hpp"
#include "pj_base/builtin/image_annotations_fields.hpp"
#include "pj_base/builtin/point_cloud_fields.hpp"
#include "pj_base/builtin/scene_entities_fields.hpp"

namespace PJ::sdk {

/// Returns the field table for @p type, or `nullptr` for a type that has none
/// (buffer-only or not yet described) and for `kNone`. Every enumerator is
/// listed without a `default:` so `-Wswitch` flags a new `BuiltinObjectType`
/// until this function decides what it describes.
[[nodiscard]] inline const FieldTableView* describe(BuiltinObjectType type) noexcept {
  switch (type) {
    case BuiltinObjectType::kFrameTransforms:
      return &FieldTable<FrameTransforms>::view;
    case BuiltinObjectType::kImageAnnotations:
      return &FieldTable<ImageAnnotations>::view;
    case BuiltinObjectType::kPointCloud:
      return &FieldTable<PointCloud>::view;
    case BuiltinObjectType::kSceneEntities:
      return &FieldTable<SceneEntities>::view;
    case BuiltinObjectType::kNone:
    case BuiltinObjectType::kImage:
    case BuiltinObjectType::kDepthImage:
    case BuiltinObjectType::kOccupancyGrid:
    case BuiltinObjectType::kCompressedPointCloud:
    case BuiltinObjectType::kMesh3D:
    case BuiltinObjectType::kVideoFrame:
    case BuiltinObjectType::kRobotDescription:
    case BuiltinObjectType::kCameraInfo:
    case BuiltinObjectType::kOccupancyGridUpdate:
    case BuiltinObjectType::kLog:
    case BuiltinObjectType::kPosesInFrame:
    case BuiltinObjectType::kVoxelGrid:
    case BuiltinObjectType::kPlotMarkers:
    case BuiltinObjectType::kGridMap:
      return nullptr;
  }
  return nullptr;
}

}  // namespace PJ::sdk
