/**
 * @file builtin_object.hpp
 * @brief Type-erased holder for any builtin object a MessageParser may produce.
 *
 * BuiltinObject wraps one concrete builtin value (`sdk::Image`,
 * `sdk::PointCloud`, `sdk::DepthImage`, `sdk::ImageAnnotations`,
 * `sdk::FrameTransforms`, ...) together with its `BuiltinObjectType` tag,
 * recorded at construction. A consumer recovers the concrete type via
 * `obj.get<T>()` and the tag via `typeOf(obj)` — an integer compare, never
 * RTTI. `std::any` filled this role before, but its `typeid`/manager identity
 * is a linker property that broke inside macOS plugin dylibs whenever the
 * producing and consuming TUs were compiled with different type visibility;
 * an explicit tag is immune to visibility, linkers, and dlopen on every
 * platform.
 *
 * The type erasure stays deliberately open-ended (a tag + opaque pointer, not
 * `std::variant`): no TU ever references a full alternative list, so plugins
 * built against an older SDK keep producing the alternatives they know, and a
 * host that receives a tag it does not know sees `BuiltinObjectType::kNone`
 * from `typeOf` and rejects the message. No protocol bump is required when a
 * new builtin type is appended to BuiltinObjectType.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "pj_base/builtin/camera_info.hpp"
#include "pj_base/builtin/compressed_point_cloud.hpp"
#include "pj_base/builtin/depth_image.hpp"
#include "pj_base/builtin/frame_transforms.hpp"
#include "pj_base/builtin/grid_map.hpp"
#include "pj_base/builtin/image.hpp"
#include "pj_base/builtin/image_annotations.hpp"
#include "pj_base/builtin/log.hpp"
#include "pj_base/builtin/mesh3d.hpp"
#include "pj_base/builtin/occupancy_grid.hpp"
#include "pj_base/builtin/occupancy_grid_update.hpp"
#include "pj_base/builtin/plot_markers.hpp"
#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/builtin/poses_in_frame.hpp"
#include "pj_base/builtin/robot_description.hpp"
#include "pj_base/builtin/scene_entities.hpp"
#include "pj_base/builtin/video_frame.hpp"
#include "pj_base/builtin/voxel_grid.hpp"

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
  // 12 reserved — was kAssetVideo (removed; video unified on kVideoFrame). Never reuse.
  kRobotDescription = 13,     ///< sdk::RobotDescription — raw URDF/SDF/MJCF text + format hint.
  kCameraInfo = 14,           ///< sdk::CameraInfo — pinhole camera calibration (K/D/R/P).
  kOccupancyGridUpdate = 15,  ///< sdk::OccupancyGridUpdate — incremental sub-rectangle patch for an OccupancyGrid.
  kLog = 16,                  ///< sdk::Log — textual log message (level + text + name).
  kPosesInFrame = 17,         ///< sdk::PosesInFrame — array of poses in one reference frame.
  kVoxelGrid = 18,            ///< sdk::VoxelGrid — dense 3D voxel grid (occupancy/cost/ESDF/semantic).
  kPlotMarkers = 19,          ///< sdk::PlotMarkers — findings on a time-series plot (regions, events, bands, labels).
  kGridMap = 20,              ///< sdk::GridMap — 2D grid of per-cell channels (elevation maps, layered costmaps).
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
    case BuiltinObjectType::kRobotDescription:
      return "kRobotDescription";
    case BuiltinObjectType::kCameraInfo:
      return "kCameraInfo";
    case BuiltinObjectType::kOccupancyGridUpdate:
      return "kOccupancyGridUpdate";
    case BuiltinObjectType::kLog:
      return "kLog";
    case BuiltinObjectType::kPosesInFrame:
      return "kPosesInFrame";
    case BuiltinObjectType::kVoxelGrid:
      return "kVoxelGrid";
    case BuiltinObjectType::kPlotMarkers:
      return "kPlotMarkers";
    case BuiltinObjectType::kGridMap:
      return "kGridMap";
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
  if (s == "kPosesInFrame") {
    return BuiltinObjectType::kPosesInFrame;
  }
  if (s == "kVoxelGrid") {
    return BuiltinObjectType::kVoxelGrid;
  }
  if (s == "kPlotMarkers") {
    return BuiltinObjectType::kPlotMarkers;
  }
  if (s == "kGridMap") {
    return BuiltinObjectType::kGridMap;
  }
  return std::nullopt;
}

/// Maps each concrete builtin value type to its BuiltinObjectType tag.
/// Specialized below for every alternative; a type without a specialization
/// cannot enter a BuiltinObject (the wrapping constructor fails to compile).
template <typename T>
struct BuiltinObjectTraits;

#define PJ_BUILTIN_OBJECT_TRAIT(TYPE)                                      \
  template <>                                                              \
  struct BuiltinObjectTraits<TYPE> {                                       \
    static constexpr BuiltinObjectType kType = BuiltinObjectType::k##TYPE; \
  };
PJ_BUILTIN_OBJECT_TRAIT(Image)
PJ_BUILTIN_OBJECT_TRAIT(PointCloud)
PJ_BUILTIN_OBJECT_TRAIT(DepthImage)
PJ_BUILTIN_OBJECT_TRAIT(ImageAnnotations)
PJ_BUILTIN_OBJECT_TRAIT(FrameTransforms)
PJ_BUILTIN_OBJECT_TRAIT(OccupancyGrid)
PJ_BUILTIN_OBJECT_TRAIT(CompressedPointCloud)
PJ_BUILTIN_OBJECT_TRAIT(Mesh3D)
PJ_BUILTIN_OBJECT_TRAIT(VideoFrame)
PJ_BUILTIN_OBJECT_TRAIT(SceneEntities)
PJ_BUILTIN_OBJECT_TRAIT(RobotDescription)
PJ_BUILTIN_OBJECT_TRAIT(CameraInfo)
PJ_BUILTIN_OBJECT_TRAIT(OccupancyGridUpdate)
PJ_BUILTIN_OBJECT_TRAIT(Log)
PJ_BUILTIN_OBJECT_TRAIT(PosesInFrame)
PJ_BUILTIN_OBJECT_TRAIT(VoxelGrid)
PJ_BUILTIN_OBJECT_TRAIT(PlotMarkers)
PJ_BUILTIN_OBJECT_TRAIT(GridMap)
#undef PJ_BUILTIN_OBJECT_TRAIT

/// Tag + opaque value. See the file comment for why the tag is explicit
/// rather than recovered through RTTI.
///
/// Copies SHARE the underlying value (shared_ptr semantics — `std::any`
/// deep-copied). Treat the payload as immutable once the record can have
/// been copied; mutate through the non-const get() only while this holder
/// is the single owner, e.g. right after construction or deserialization.
class BuiltinObject {
 public:
  BuiltinObject() = default;

  /// Wraps a concrete builtin value and records its tag. Participates in
  /// overload resolution only for types with a BuiltinObjectTraits
  /// specialization, so copy/move construction is unaffected.
  template <typename T, typename Decayed = std::decay_t<T>, BuiltinObjectType = BuiltinObjectTraits<Decayed>::kType>
  // NOLINTNEXTLINE(google-explicit-constructor,bugprone-forwarding-reference-overload): mirrors std::any's
  // converting constructor; the traits default argument excludes BuiltinObject itself, so copy/move stay visible.
  BuiltinObject(T&& value)
      : type_(BuiltinObjectTraits<Decayed>::kType), value_(std::make_shared<Decayed>(std::forward<T>(value))) {}

  [[nodiscard]] bool has_value() const noexcept {
    return value_ != nullptr;
  }

  /// The tag recorded at construction; kNone when empty.
  [[nodiscard]] BuiltinObjectType type() const noexcept {
    return value_ ? type_ : BuiltinObjectType::kNone;
  }

  /// The stored value as T — nullptr when empty or tagged as another type.
  template <typename T>
  [[nodiscard]] const T* get() const noexcept {
    return value_ && type_ == BuiltinObjectTraits<T>::kType ? static_cast<const T*>(value_.get()) : nullptr;
  }

  /// Mutable access — see the class comment for the single-owner caveat.
  template <typename T>
  [[nodiscard]] T* get() noexcept {
    return value_ && type_ == BuiltinObjectTraits<T>::kType ? static_cast<T*>(value_.get()) : nullptr;
  }

 private:
  BuiltinObjectType type_ = BuiltinObjectType::kNone;
  std::shared_ptr<void> value_;
};

/// Get the type tag for a BuiltinObject without copying it.
/// Returns kNone for an empty BuiltinObject. (A tag appended by a NEWER SDK
/// never reaches this build as a live C++ object — unknown types only arrive
/// serialized, and deserializeBuiltinObject rejects tags it does not know.)
[[nodiscard]] inline BuiltinObjectType typeOf(const BuiltinObject& obj) noexcept {
  return obj.type();
}

}  // namespace sdk
}  // namespace PJ
