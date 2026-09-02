// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/builtin_object_codec.hpp"

#include <any>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "pj_base/builtin/camera_info_codec.hpp"
#include "pj_base/builtin/compressed_point_cloud_codec.hpp"
#include "pj_base/builtin/depth_image_codec.hpp"
#include "pj_base/builtin/frame_transforms_codec.hpp"
#include "pj_base/builtin/grid_map_codec.hpp"
#include "pj_base/builtin/image_annotations_codec.hpp"
#include "pj_base/builtin/image_codec.hpp"
#include "pj_base/builtin/log_codec.hpp"
#include "pj_base/builtin/mesh3d_codec.hpp"
#include "pj_base/builtin/occupancy_grid_codec.hpp"
#include "pj_base/builtin/occupancy_grid_update_codec.hpp"
#include "pj_base/builtin/plot_markers_codec.hpp"
#include "pj_base/builtin/point_cloud_codec.hpp"
#include "pj_base/builtin/poses_in_frame_codec.hpp"
#include "pj_base/builtin/scene_entities_codec.hpp"
#include "pj_base/builtin/video_frame_codec.hpp"
#include "pj_base/builtin/voxel_grid_codec.hpp"

namespace PJ {
namespace {

template <typename T, typename Serialize>
Expected<std::vector<uint8_t>> serializeAs(
    const sdk::BuiltinObject& object, sdk::BuiltinObjectType type, Serialize&& serialize) {
  const auto* value = std::any_cast<T>(&object);
  if (value == nullptr) {
    return unexpected(std::string("builtin object/type mismatch for ") + std::string(sdk::name(type)));
  }
  return std::forward<Serialize>(serialize)(*value);
}

template <typename T>
Expected<sdk::BuiltinObject> eraseDecoded(Expected<T> decoded) {
  if (!decoded) {
    return unexpected(std::move(decoded).error());
  }
  return sdk::BuiltinObject(std::move(*decoded));
}

template <typename T, typename Deserialize>
Expected<sdk::BuiltinObject> deserializeAs(const uint8_t* data, size_t size, Deserialize&& deserialize) {
  // An empty byte sequence is the canonical proto3 encoding of a default
  // message. Some concrete codecs deliberately reject empty storage as an
  // input-validation convenience, but the type-tagged dispatcher can
  // distinguish a valid default object from a missing/unknown payload.
  if (size == 0) {
    return sdk::BuiltinObject(T{});
  }
  return eraseDecoded(std::forward<Deserialize>(deserialize)(data, size));
}

}  // namespace

Expected<std::vector<uint8_t>> serializeBuiltinObject(const sdk::BuiltinObject& object) {
  const auto type = sdk::typeOf(object);
  switch (type) {
    case sdk::BuiltinObjectType::kImage:
      return serializeAs<sdk::Image>(object, type, serializeImage);
    case sdk::BuiltinObjectType::kPointCloud:
      return serializeAs<sdk::PointCloud>(object, type, serializePointCloud);
    case sdk::BuiltinObjectType::kDepthImage:
      return serializeAs<sdk::DepthImage>(object, type, serializeDepthImage);
    case sdk::BuiltinObjectType::kImageAnnotations:
      return serializeAs<sdk::ImageAnnotations>(object, type, serializeImageAnnotations);
    case sdk::BuiltinObjectType::kFrameTransforms:
      return serializeAs<sdk::FrameTransforms>(object, type, serializeFrameTransforms);
    case sdk::BuiltinObjectType::kOccupancyGrid:
      return serializeAs<sdk::OccupancyGrid>(object, type, serializeOccupancyGrid);
    case sdk::BuiltinObjectType::kCompressedPointCloud:
      return serializeAs<sdk::CompressedPointCloud>(object, type, serializeCompressedPointCloud);
    case sdk::BuiltinObjectType::kMesh3D:
      return serializeAs<sdk::Mesh3D>(object, type, serializeMesh3D);
    case sdk::BuiltinObjectType::kVideoFrame:
      return serializeAs<sdk::VideoFrame>(object, type, serializeVideoFrame);
    case sdk::BuiltinObjectType::kSceneEntities:
      return serializeAs<sdk::SceneEntities>(object, type, serializeSceneEntities);
    case sdk::BuiltinObjectType::kRobotDescription:
      return serializeAs<sdk::RobotDescription>(object, type, serializeRobotDescription);
    case sdk::BuiltinObjectType::kCameraInfo:
      return serializeAs<sdk::CameraInfo>(object, type, serializeCameraInfo);
    case sdk::BuiltinObjectType::kOccupancyGridUpdate:
      return serializeAs<sdk::OccupancyGridUpdate>(object, type, serializeOccupancyGridUpdate);
    case sdk::BuiltinObjectType::kLog:
      return serializeAs<sdk::Log>(object, type, serializeLog);
    case sdk::BuiltinObjectType::kPosesInFrame:
      return serializeAs<sdk::PosesInFrame>(object, type, serializePosesInFrame);
    case sdk::BuiltinObjectType::kVoxelGrid:
      return serializeAs<sdk::VoxelGrid>(object, type, serializeVoxelGrid);
    case sdk::BuiltinObjectType::kPlotMarkers:
      return serializeAs<sdk::PlotMarkers>(object, type, serializePlotMarkers);
    case sdk::BuiltinObjectType::kGridMap:
      return serializeAs<sdk::GridMap>(object, type, serializeGridMap);
    case sdk::BuiltinObjectType::kNone:
      return unexpected(std::string("cannot serialize builtin object with type kNone"));
  }
  return unexpected(std::string("cannot serialize unknown builtin object type"));
}

Expected<sdk::BuiltinObject> deserializeBuiltinObject(sdk::BuiltinObjectType type, const uint8_t* data, size_t size) {
  switch (type) {
    case sdk::BuiltinObjectType::kImage:
      return deserializeAs<sdk::Image>(data, size, deserializeImage);
    case sdk::BuiltinObjectType::kPointCloud:
      return deserializeAs<sdk::PointCloud>(data, size, deserializePointCloud);
    case sdk::BuiltinObjectType::kDepthImage:
      return deserializeAs<sdk::DepthImage>(data, size, deserializeDepthImage);
    case sdk::BuiltinObjectType::kImageAnnotations:
      return deserializeAs<sdk::ImageAnnotations>(data, size, deserializeImageAnnotations);
    case sdk::BuiltinObjectType::kFrameTransforms:
      return deserializeAs<sdk::FrameTransforms>(data, size, deserializeFrameTransforms);
    case sdk::BuiltinObjectType::kOccupancyGrid:
      return deserializeAs<sdk::OccupancyGrid>(data, size, deserializeOccupancyGrid);
    case sdk::BuiltinObjectType::kCompressedPointCloud:
      return deserializeAs<sdk::CompressedPointCloud>(data, size, deserializeCompressedPointCloud);
    case sdk::BuiltinObjectType::kMesh3D:
      return deserializeAs<sdk::Mesh3D>(data, size, deserializeMesh3D);
    case sdk::BuiltinObjectType::kVideoFrame:
      return deserializeAs<sdk::VideoFrame>(data, size, deserializeVideoFrame);
    case sdk::BuiltinObjectType::kSceneEntities:
      return deserializeAs<sdk::SceneEntities>(data, size, deserializeSceneEntities);
    case sdk::BuiltinObjectType::kRobotDescription:
      return deserializeAs<sdk::RobotDescription>(data, size, deserializeRobotDescription);
    case sdk::BuiltinObjectType::kCameraInfo:
      return deserializeAs<sdk::CameraInfo>(data, size, deserializeCameraInfo);
    case sdk::BuiltinObjectType::kOccupancyGridUpdate:
      return deserializeAs<sdk::OccupancyGridUpdate>(data, size, deserializeOccupancyGridUpdate);
    case sdk::BuiltinObjectType::kLog:
      return deserializeAs<sdk::Log>(data, size, deserializeLog);
    case sdk::BuiltinObjectType::kPosesInFrame:
      return deserializeAs<sdk::PosesInFrame>(data, size, deserializePosesInFrame);
    case sdk::BuiltinObjectType::kVoxelGrid:
      return deserializeAs<sdk::VoxelGrid>(data, size, deserializeVoxelGrid);
    case sdk::BuiltinObjectType::kPlotMarkers:
      return deserializeAs<sdk::PlotMarkers>(data, size, deserializePlotMarkers);
    case sdk::BuiltinObjectType::kGridMap:
      return deserializeAs<sdk::GridMap>(data, size, deserializeGridMap);
    case sdk::BuiltinObjectType::kNone:
      return unexpected(std::string("cannot deserialize builtin object with type kNone"));
  }
  return unexpected(
      std::string("cannot deserialize unknown builtin object type: ") + std::to_string(static_cast<uint16_t>(type)));
}

}  // namespace PJ
