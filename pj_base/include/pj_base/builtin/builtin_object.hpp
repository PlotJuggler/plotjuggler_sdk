/**
 * @file builtin_object.hpp
 * @brief Type-erased holder for any builtin object a MessageParser may produce.
 *
 * BuiltinObject is `std::any`. A producer constructs it by passing a
 * concrete builtin value (`sdk::Image`, `sdk::PointCloud`, `sdk::DepthImage`,
 * `sdk::ImageAnnotations`, `sdk::FrameTransforms`, ...); a consumer recovers
 * the concrete type via `std::any_cast<T>(&obj)` and obtains the type tag via
 * `typeOf(obj)`.
 *
 * The type erasure is deliberate: choosing `std::any` over `std::variant`
 * keeps the SDK forward-compatible. Plugins built against an older SDK can
 * keep producing the alternatives they know without any TU referencing the
 * (later-extended) full alternative list; hosts built against an older SDK
 * that receive an unknown type simply see `BuiltinObjectType::kNone` from
 * `typeOf` and reject the message. No protocol bump required when a new
 * builtin type is appended to BuiltinObjectType.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <any>
#include <cstdint>
#include <optional>
#include <string_view>

#include "pj_base/builtin/asset_video.hpp"
#include "pj_base/builtin/camera_info.hpp"
#include "pj_base/builtin/compressed_point_cloud.hpp"
#include "pj_base/builtin/depth_image.hpp"
#include "pj_base/builtin/frame_transforms.hpp"
#include "pj_base/builtin/image.hpp"
#include "pj_base/builtin/image_annotations.hpp"
#include "pj_base/builtin/log.hpp"
#include "pj_base/builtin/mesh3d.hpp"
#include "pj_base/builtin/occupancy_grid.hpp"
#include "pj_base/builtin/occupancy_grid_update.hpp"
#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/builtin/robot_description.hpp"
#include "pj_base/builtin/scene_entities.hpp"
#include "pj_base/builtin/video_frame.hpp"

namespace PJ {
namespace sdk {

enum class BuiltinObjectType : uint16_t {
  kNone = 0,
  kImage = 1,                 ///< sdk::Image — raw or compressed, distinguished by encoding string.
  kPointCloud = 3,            ///< sdk::PointCloud — packed points + per-channel field layout.
  kDepthImage = 4,            ///< sdk::DepthImage — depth pixels + camera intrinsics.
  kImageAnnotations = 5,      ///< sdk::ImageAnnotations — 2D overlays (points, lines, text).
  kFrameTransforms = 6,       ///< sdk::FrameTransforms — named 3D frame relationships.
  kOccupancyGrid = 7,         ///< sdk::OccupancyGrid — 2D metric grid (maps, costmaps).
  kCompressedPointCloud = 8,  ///< sdk::CompressedPointCloud — opaque compressed cloud (Draco, ...).
  kMesh3D = 9,                ///< sdk::Mesh3D — binary mesh asset (GLTF/STL/PLY/OBJ/USD/DAE).
  kVideoFrame = 10,           ///< sdk::VideoFrame — single frame of h264/h265/vp9/av1 stream.
  kSceneEntities = 11,        ///< sdk::SceneEntities — procedural 3D scene primitives.
  kAssetVideo = 12,           ///< sdk::AssetVideo — file-backed video reference + playback metadata.
  kRobotDescription = 13,     ///< sdk::RobotDescription — raw URDF/SDF/MJCF text + format hint.
  kCameraInfo = 14,           ///< sdk::CameraInfo — pinhole camera calibration (K/D/R/P).
  kOccupancyGridUpdate = 15,  ///< sdk::OccupancyGridUpdate — incremental sub-rectangle patch for an OccupancyGrid.
  kLog = 16,                  ///< sdk::Log — textual log message (level + text + name).
};

/// A-priori classification of a schema. Currently carries only the type;
/// the struct leaves room to attach declarative metadata later without
/// breaking the API.
struct SchemaClassification {
  BuiltinObjectType object_type = BuiltinObjectType::kNone;
};

/// Canonical string for a type value. e.g. name(kImage) == "kImage".
[[nodiscard]] inline constexpr std::string_view name(BuiltinObjectType type) noexcept {
  switch (type) {
    case BuiltinObjectType::kNone:
      return "kNone";
    case BuiltinObjectType::kImage:
      return "kImage";
    case BuiltinObjectType::kPointCloud:
      return "kPointCloud";
    case BuiltinObjectType::kDepthImage:
      return "kDepthImage";
    case BuiltinObjectType::kImageAnnotations:
      return "kImageAnnotations";
    case BuiltinObjectType::kFrameTransforms:
      return "kFrameTransforms";
    case BuiltinObjectType::kOccupancyGrid:
      return "kOccupancyGrid";
    case BuiltinObjectType::kCompressedPointCloud:
      return "kCompressedPointCloud";
    case BuiltinObjectType::kMesh3D:
      return "kMesh3D";
    case BuiltinObjectType::kVideoFrame:
      return "kVideoFrame";
    case BuiltinObjectType::kSceneEntities:
      return "kSceneEntities";
    case BuiltinObjectType::kAssetVideo:
      return "kAssetVideo";
    case BuiltinObjectType::kRobotDescription:
      return "kRobotDescription";
    case BuiltinObjectType::kCameraInfo:
      return "kCameraInfo";
    case BuiltinObjectType::kOccupancyGridUpdate:
      return "kOccupancyGridUpdate";
    case BuiltinObjectType::kLog:
      return "kLog";
  }
  return "kNone";
}

/// Parse a type name into the enum. Accepts the same strings name()
/// emits (e.g. "kImage"). Returns nullopt for unknown names.
[[nodiscard]] inline constexpr std::optional<BuiltinObjectType> parseBuiltinObjectType(std::string_view s) noexcept {
  if (s == "kNone") {
    return BuiltinObjectType::kNone;
  }
  if (s == "kImage") {
    return BuiltinObjectType::kImage;
  }
  if (s == "kPointCloud") {
    return BuiltinObjectType::kPointCloud;
  }
  if (s == "kDepthImage") {
    return BuiltinObjectType::kDepthImage;
  }
  if (s == "kImageAnnotations") {
    return BuiltinObjectType::kImageAnnotations;
  }
  if (s == "kFrameTransforms") {
    return BuiltinObjectType::kFrameTransforms;
  }
  if (s == "kOccupancyGrid") {
    return BuiltinObjectType::kOccupancyGrid;
  }
  if (s == "kCompressedPointCloud") {
    return BuiltinObjectType::kCompressedPointCloud;
  }
  if (s == "kMesh3D") {
    return BuiltinObjectType::kMesh3D;
  }
  if (s == "kVideoFrame") {
    return BuiltinObjectType::kVideoFrame;
  }
  if (s == "kSceneEntities") {
    return BuiltinObjectType::kSceneEntities;
  }
  if (s == "kAssetVideo") {
    return BuiltinObjectType::kAssetVideo;
  }
  if (s == "kRobotDescription") {
    return BuiltinObjectType::kRobotDescription;
  }
  if (s == "kCameraInfo") {
    return BuiltinObjectType::kCameraInfo;
  }
  if (s == "kOccupancyGridUpdate") {
    return BuiltinObjectType::kOccupancyGridUpdate;
  }
  if (s == "kLog") {
    return BuiltinObjectType::kLog;
  }
  return std::nullopt;
}

using BuiltinObject = std::any;

/// Get the type tag for a BuiltinObject without copying it.
/// Returns kNone for an empty BuiltinObject or one that wraps a type
/// unknown to this SDK build.
[[nodiscard]] inline BuiltinObjectType typeOf(const BuiltinObject& obj) noexcept {
  if (!obj.has_value()) {
    return BuiltinObjectType::kNone;
  }
  const auto& t = obj.type();
  if (t == typeid(Image)) {
    return BuiltinObjectType::kImage;
  }
  if (t == typeid(PointCloud)) {
    return BuiltinObjectType::kPointCloud;
  }
  if (t == typeid(DepthImage)) {
    return BuiltinObjectType::kDepthImage;
  }
  if (t == typeid(ImageAnnotations)) {
    return BuiltinObjectType::kImageAnnotations;
  }
  if (t == typeid(FrameTransforms)) {
    return BuiltinObjectType::kFrameTransforms;
  }
  if (t == typeid(OccupancyGrid)) {
    return BuiltinObjectType::kOccupancyGrid;
  }
  if (t == typeid(CompressedPointCloud)) {
    return BuiltinObjectType::kCompressedPointCloud;
  }
  if (t == typeid(Mesh3D)) {
    return BuiltinObjectType::kMesh3D;
  }
  if (t == typeid(VideoFrame)) {
    return BuiltinObjectType::kVideoFrame;
  }
  if (t == typeid(SceneEntities)) {
    return BuiltinObjectType::kSceneEntities;
  }
  if (t == typeid(AssetVideo)) {
    return BuiltinObjectType::kAssetVideo;
  }
  if (t == typeid(RobotDescription)) {
    return BuiltinObjectType::kRobotDescription;
  }
  if (t == typeid(CameraInfo)) {
    return BuiltinObjectType::kCameraInfo;
  }
  if (t == typeid(OccupancyGridUpdate)) {
    return BuiltinObjectType::kOccupancyGridUpdate;
  }
  if (t == typeid(Log)) {
    return BuiltinObjectType::kLog;
  }
  return BuiltinObjectType::kNone;
}

}  // namespace sdk
}  // namespace PJ
