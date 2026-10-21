// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/field_table.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/builtin/builtin_object_codec.hpp"
#include "pj_base/builtin/camera_info_fields.hpp"
#include "pj_base/builtin/depth_image_fields.hpp"
#include "pj_base/builtin/field_table_registry.hpp"
#include "pj_base/builtin/frame_transforms_fields.hpp"
#include "pj_base/builtin/image_annotations_fields.hpp"
#include "pj_base/builtin/image_fields.hpp"
#include "pj_base/builtin/point_cloud_fields.hpp"
#include "pj_base/builtin/scene_entities_fields.hpp"
#include "pj_base/builtin/video_frame_fields.hpp"
#include "pj_base/builtin/voxel_grid_fields.hpp"

namespace {

using PJ::sdk::AnnotationTopology;
using PJ::sdk::ArrowPrimitive;
using PJ::sdk::AxesPrimitive;
using PJ::sdk::BufferLayout;
using PJ::sdk::BuiltinObjectType;
using PJ::sdk::CameraInfo;
using PJ::sdk::CircleAnnotation;
using PJ::sdk::ColorRGBA;
using PJ::sdk::CubePrimitive;
using PJ::sdk::CylinderPrimitive;
using PJ::sdk::DepthImage;
using PJ::sdk::FieldDescriptor;
using PJ::sdk::FieldKind;
using PJ::sdk::FieldTable;
using PJ::sdk::FieldTableView;
using PJ::sdk::FrameTransform;
using PJ::sdk::FrameTransforms;
using PJ::sdk::Image;
using PJ::sdk::ImageAnnotations;
using PJ::sdk::KeyValuePair;
using PJ::sdk::LinePrimitive;
using PJ::sdk::LineType;
using PJ::sdk::ModelPrimitive;
using PJ::sdk::PointCloud;
using PJ::sdk::PointField;
using PJ::sdk::PointsAnnotation;
using PJ::sdk::Pose;
using PJ::sdk::SceneEntities;
using PJ::sdk::SceneEntity;
using PJ::sdk::SceneEntityDeletion;
using PJ::sdk::SpherePrimitive;
using PJ::sdk::TextAnnotation;
using PJ::sdk::TextPrimitive;
using PJ::sdk::TrianglePrimitive;
using PJ::sdk::Vector2;
using PJ::sdk::VideoFrame;
using PJ::sdk::VoxelGrid;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

const FieldDescriptor* findField(const FieldTableView& table, std::string_view name) {
  for (const auto& f : table.fields) {
    if (f.name == name) {
      return &f;
    }
  }
  return nullptr;
}

// Walks `table` and every table reachable through `nested`, depth-first,
// visiting each distinct type name once (guards against re-visiting a type
// reachable through multiple paths, e.g. Vector3 via both Pose and FrameTransform).
void collectTables(
    const FieldTableView& table, std::set<std::string_view>& visited, std::vector<const FieldTableView*>& out) {
  if (!visited.insert(table.type_name).second) {
    return;
  }
  out.push_back(&table);
  for (const auto& f : table.fields) {
    if (f.nested != nullptr) {
      collectTables(*f.nested, visited, out);
    }
  }
}

// Recursively copies every described field from `src` to `dst` using only
// FieldDescriptor accessors — the same access pattern a generic script
// binder would use. A kList field either holds structs (nested != nullptr,
// recurse into the nested table) or scalars (nested == nullptr; copy via the
// get_*/set_* pair matching element_kind, applied to the element address
// returned by list_at/list_emplace for a growable list, or list_replace for
// a fixed-size one — see field_table.hpp's kList doc comment). A kBuffer
// field resolves its current bytes and re-assigns them onto dst, taking a
// fresh copy/anchor. A kOptionalNumber field copies the value only when
// present, leaving a freshly-constructed dst's absent default untouched
// otherwise.
void copyThroughTable(const FieldTableView& table, const void* src, void* dst) {
  for (const auto& f : table.fields) {
    switch (f.kind) {
      case FieldKind::kNumber:
      case FieldKind::kBool:
      case FieldKind::kEnum:
        f.set_number(dst, f.get_number(src));
        break;
      case FieldKind::kInt64:
        f.set_int64(dst, f.get_int64(src));
        break;
      case FieldKind::kString:
        f.set_string(dst, f.get_string(src));
        break;
      case FieldKind::kOptionalNumber:
        if (f.has_value(src)) {
          f.set_number(dst, f.get_number(src));
        }
        break;
      case FieldKind::kStruct:
        ASSERT_NE(f.nested, nullptr);
        copyThroughTable(*f.nested, f.struct_ptr(src), f.struct_ptr_mut(dst));
        break;
      case FieldKind::kList: {
        const size_t n = f.list_size(src);
        const bool fixed_size = f.list_replace != nullptr;
        if (!fixed_size) {
          f.list_clear(dst);
        } else {
          ASSERT_EQ(n, f.list_size(dst)) << "fixed-size list: src/dst element counts must match";
        }
        for (size_t i = 0; i < n; ++i) {
          void* dst_elem = fixed_size ? f.list_replace(dst, i) : f.list_emplace(dst);
          const void* src_elem = f.list_at(src, i);
          if (f.nested != nullptr) {
            copyThroughTable(*f.nested, src_elem, dst_elem);
            continue;
          }
          switch (f.element_kind) {
            case FieldKind::kNumber:
            case FieldKind::kBool:
            case FieldKind::kEnum:
              f.set_number(dst_elem, f.get_number(src_elem));
              break;
            case FieldKind::kInt64:
              f.set_int64(dst_elem, f.get_int64(src_elem));
              break;
            case FieldKind::kString:
              f.set_string(dst_elem, f.get_string(src_elem));
              break;
            default:
              FAIL() << "unsupported scalar element_kind in copyThroughTable";
          }
        }
        break;
      }
      case FieldKind::kBuffer: {
        const BufferLayout layout = f.buffer(src);
        std::vector<uint8_t> bytes(layout.bytes.begin(), layout.bytes.end());
        f.buffer_assign(dst, std::move(bytes));
        break;
      }
    }
  }
}

// Serializes both objects through the canonical wire codec and expects identical
// bytes: the comparison for builtins whose payload has no meaningful operator==.
template <class T>
void expectSameWireBytes(const T& src, const T& dst) {
  auto src_bytes = PJ::serializeBuiltinObject(PJ::sdk::BuiltinObject(src));
  auto dst_bytes = PJ::serializeBuiltinObject(PJ::sdk::BuiltinObject(dst));
  ASSERT_TRUE(src_bytes) << src_bytes.error();
  ASSERT_TRUE(dst_bytes) << dst_bytes.error();
  EXPECT_EQ(*src_bytes, *dst_bytes);
}

// ---------------------------------------------------------------------------
// Sample data — populated, so every accessor path is exercised.
// ---------------------------------------------------------------------------

FrameTransforms makeSampleFrameTransforms() {
  FrameTransforms ft;
  ft.transforms.push_back(
      FrameTransform{
          .timestamp = 1'234'567'890'123'456,
          .parent_frame_id = "map",
          .child_frame_id = "base_link",
          .translation = {.x = 1.0, .y = 2.0, .z = 3.0},
          .rotation = {.x = 0.1, .y = 0.2, .z = 0.3, .w = 0.9},
      });
  ft.transforms.push_back(
      FrameTransform{
          .timestamp = -42,
          .parent_frame_id = "odom",
          .child_frame_id = "map",
          .translation = {.x = -1.5, .y = 2.5, .z = -3.5},
          .rotation = {.x = 0.0, .y = 0.0, .z = 0.707, .w = 0.707},
      });
  return ft;
}

ImageAnnotations makeSampleImageAnnotations() {
  ImageAnnotations ia;
  ia.timestamp = 9'007'199'254'740'993;  // > 2^53: would lose precision as a double.
  ia.image_topic = "/camera/image";

  PointsAnnotation pa;
  pa.topology = AnnotationTopology::kLineStrip;  // non-default (kPoints == 0)
  pa.points = {{.x = 1.0, .y = 2.0}, {.x = 3.0, .y = 4.0}, {.x = 5.0, .y = 6.0}};
  pa.thickness = 3.5;
  pa.color = {.r = 10, .g = 20, .b = 30, .a = 40};
  pa.colors = {{.r = 1, .g = 2, .b = 3, .a = 4}, {.r = 5, .g = 6, .b = 7, .a = 8}};
  pa.fill_color = {.r = 50, .g = 60, .b = 70, .a = 80};
  ia.points.push_back(pa);

  CircleAnnotation ca;
  ca.center = {.x = 7.0, .y = 8.0};
  ca.radius = 4.0;
  ca.thickness = 1.5;
  ca.color = {.r = 100, .g = 110, .b = 120, .a = 130};
  ca.fill_color = {.r = 140, .g = 150, .b = 160, .a = 170};
  ia.circles.push_back(ca);

  TextAnnotation ta;
  ta.position = {.x = 9.0, .y = 10.0};
  ta.font_size = 18.0;
  ta.color = {.r = 200, .g = 210, .b = 220, .a = 230};
  ta.text = "hello";
  ia.texts.push_back(ta);

  return ia;
}

// One entity carrying at least one of every SceneEntity primitive kind, plus
// metadata; the batch also carries one deletion.
SceneEntities makeSampleSceneEntities() {
  SceneEntity e;
  e.timestamp = 1'000'000'000'000'123;  // > 2^53.
  e.frame_id = "map";
  e.id = "entity-1";
  e.lifetime_ns = 5'000'000'000;
  e.frame_locked = true;
  e.metadata = {
      KeyValuePair{.key = "k1", .value = "v1"},
      KeyValuePair{.key = "k2", .value = "v2"},
  };

  e.arrows.push_back(
      ArrowPrimitive{
          .pose = {.position = {.x = 1, .y = 2, .z = 3}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .shaft_length = 1.0,
          .shaft_diameter = 0.1,
          .head_length = 0.3,
          .head_diameter = 0.2,
          .color = {.r = 255, .g = 0, .b = 0, .a = 255},
      });

  e.cubes.push_back(
      CubePrimitive{
          .pose = {.position = {.x = 4, .y = 5, .z = 6}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .size = {.x = 1, .y = 1, .z = 1},
          .color = {.r = 0, .g = 255, .b = 0, .a = 255},
      });

  e.spheres.push_back(
      SpherePrimitive{
          .pose = {.position = {.x = 7, .y = 8, .z = 9}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .size = {.x = 2, .y = 2, .z = 2},
          .color = {.r = 0, .g = 0, .b = 255, .a = 255},
      });

  e.cylinders.push_back(
      CylinderPrimitive{
          .pose = {.position = {.x = 1, .y = 1, .z = 1}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .size = {.x = 1, .y = 1, .z = 2},
          .bottom_scale = 0.5,
          .top_scale = 0.8,
          .color = {.r = 10, .g = 20, .b = 30, .a = 255},
      });

  e.lines.push_back(
      LinePrimitive{
          .type = LineType::kLineLoop,  // non-default (kLineStrip == 0)
          .pose = {.position = {.x = 0, .y = 0, .z = 0}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .thickness = 0.05,
          .scale_invariant = true,
          .points = {{.x = 0, .y = 0, .z = 0}, {.x = 1, .y = 0, .z = 0}, {.x = 1, .y = 1, .z = 0}},
          .color = {.r = 1, .g = 2, .b = 3, .a = 4},
          .colors =
              {{.r = 5, .g = 6, .b = 7, .a = 8},
               {.r = 9, .g = 10, .b = 11, .a = 12},
               {.r = 13, .g = 14, .b = 15, .a = 16}},
          .indices = {0, 1, 2},
      });

  e.triangles.push_back(
      TrianglePrimitive{
          .pose = {.position = {.x = 2, .y = 2, .z = 2}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .points = {{.x = 0, .y = 0, .z = 0}, {.x = 1, .y = 0, .z = 0}, {.x = 0, .y = 1, .z = 0}},
          .color = {.r = 20, .g = 21, .b = 22, .a = 23},
          .colors =
              {{.r = 24, .g = 25, .b = 26, .a = 27},
               {.r = 28, .g = 29, .b = 30, .a = 31},
               {.r = 32, .g = 33, .b = 34, .a = 35}},
          .indices = {0, 1, 2},
      });

  e.texts.push_back(
      TextPrimitive{
          .pose = {.position = {.x = 3, .y = 3, .z = 3}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .billboard = true,
          .font_size = 12.0,
          .scale_invariant = true,
          .color = {.r = 40, .g = 41, .b = 42, .a = 43},
          .text = "hello scene",
      });

  e.models.push_back(
      ModelPrimitive{
          .pose = {.position = {.x = 4, .y = 4, .z = 4}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .scale = {.x = 1, .y = 1, .z = 1},
          .color = {.r = 50, .g = 51, .b = 52, .a = 53},
          .override_color = true,
          .url = "",
          .media_type = "model/gltf-binary",
          .data = {0x01, 0x02, 0x03, 0x04},
      });

  e.axes.push_back(
      AxesPrimitive{
          .pose = {.position = {.x = 5, .y = 5, .z = 5}, .orientation = {.x = 0, .y = 0, .z = 0, .w = 1}},
          .length = 0.5,
          .thickness = 0.02,
          .scale_invariant = false,
      });

  SceneEntities entities;
  entities.entities.push_back(e);
  entities.deletions.push_back(
      SceneEntityDeletion{
          .type = SceneEntityDeletion::Type::kAll,  // non-default (kMatchingId == 0)
          .timestamp = 999,
          .id = "",
      });
  return entities;
}

// Packs `points` (each {x, y, z, intensity} float32) at `point_step`-byte
// stride into a fresh owned buffer via `buffer_assign()`.
PointCloud makeSamplePointCloudUnorganized() {
  PointCloud cloud;
  cloud.width = 3;
  cloud.height = 1;
  cloud.point_step = 16;
  cloud.row_step = 48;  // 3 * 16, no padding.
  cloud.is_bigendian = false;
  cloud.is_dense = true;
  cloud.frame_id = "lidar";
  cloud.fields = {
      PointField{.name = "x", .offset = 0, .datatype = PointField::Datatype::kFloat32, .count = 1},
      PointField{.name = "y", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1},
      PointField{.name = "z", .offset = 8, .datatype = PointField::Datatype::kFloat32, .count = 1},
      PointField{.name = "intensity", .offset = 12, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };
  cloud.timestamp_ns = 1'234'567'890'123'456;

  std::vector<uint8_t> bytes(static_cast<size_t>(cloud.point_step) * cloud.width);
  const float points[3][4] = {
      {1.0f, 2.0f, 3.0f, 0.5f},
      {4.0f, 5.0f, 6.0f, 0.6f},
      {7.0f, 8.0f, 9.0f, 0.7f},
  };
  for (size_t i = 0; i < 3; ++i) {
    std::memcpy(bytes.data() + i * cloud.point_step, points[i], sizeof(points[i]));
  }

  const FieldDescriptor* data_field = findField(FieldTable<PointCloud>::view, "data");
  data_field->buffer_assign(&cloud, std::move(bytes));
  return cloud;
}

// A 4x3 rgb8 image (36 bytes, no row padding), both optionals set even
// though they are only semantically meaningful for "compressedDepth" — this
// exercises the kOptionalNumber accessors on every populated fixture.
Image makeSampleImage() {
  Image image;
  image.width = 4;
  image.height = 3;
  image.encoding = "rgb8";
  image.row_step = 12;  // 4 * 3, no padding.
  image.is_bigendian = false;
  image.compressed_depth_min = 0.1f;
  image.compressed_depth_max = 6.4f;
  image.timestamp_ns = 1'234'567'890'123'456;
  image.frame_id = "camera_optical_frame";

  std::vector<uint8_t> bytes(static_cast<size_t>(image.row_step) * image.height);
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>(i + 1);
  }
  const FieldDescriptor* data_field = findField(FieldTable<Image>::view, "data");
  data_field->buffer_assign(&image, std::move(bytes));
  return image;
}

// A 2x2 32FC1 depth image (16 bytes) with a populated K and a non-empty D.
DepthImage makeSampleDepthImage() {
  DepthImage image;
  image.width = 2;
  image.height = 2;
  image.encoding = "32FC1";
  image.K = {525.0, 0.0, 319.5, 0.0, 525.0, 239.5, 0.0, 0.0, 1.0};
  image.distortion_model = "plumb_bob";
  image.D = {0.1, -0.2, 0.001, -0.002, 0.05};
  image.timestamp_ns = -42;

  std::vector<uint8_t> bytes(static_cast<size_t>(image.width) * image.height * 4);
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>(i + 1);
  }
  const FieldDescriptor* data_field = findField(FieldTable<DepthImage>::view, "data");
  data_field->buffer_assign(&image, std::move(bytes));
  return image;
}

CameraInfo makeSampleCameraInfo() {
  CameraInfo info;
  info.timestamp_ns = 9'007'199'254'740'993;  // > 2^53: would lose precision as a double.
  info.frame_id = "camera_optical_frame";
  info.width = 640;
  info.height = 480;
  info.distortion_model = "plumb_bob";
  info.D = {0.1, -0.2, 0.001, -0.002, 0.05};
  info.K = {525.0, 0.0, 319.5, 0.0, 525.0, 239.5, 0.0, 0.0, 1.0};
  info.R = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  info.P = {525.0, 0.0, 319.5, 0.0, 0.0, 525.0, 239.5, 0.0, 0.0, 0.0, 1.0, 0.0};
  return info;
}

VideoFrame makeSampleVideoFrame() {
  VideoFrame frame;
  frame.timestamp_ns = 1'000'000'000'000'123;  // > 2^53.
  frame.frame_id = "cam0";
  frame.format = "h264";

  std::vector<uint8_t> bytes = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xC0, 0x1E};
  const FieldDescriptor* data_field = findField(FieldTable<VideoFrame>::view, "data");
  data_field->buffer_assign(&frame, std::move(bytes));
  return frame;
}

// A 3x2x2 grid of 8-byte voxels (uint8 "occupancy" + float32 "cost") with
// padding at every level: rows are 28 bytes (24 + 4) and slices 60 (56 + 4),
// so data holds slice_count * slice_stride = 120 bytes.
VoxelGrid makeSampleVoxelGrid() {
  VoxelGrid grid;
  grid.timestamp_ns = 9'007'199'254'740'993;  // > 2^53: would lose precision as a double.
  grid.frame_id = "map";
  grid.origin =
      Pose{.position = {.x = 1.0, .y = 2.0, .z = 3.0}, .orientation = {.x = 0.0, .y = 0.0, .z = 0.6, .w = 0.8}};
  grid.cell_size = {.x = 0.1, .y = 0.2, .z = 0.3};
  grid.column_count = 3;
  grid.row_count = 2;
  grid.slice_count = 2;
  grid.cell_stride = 8;
  grid.row_stride = 28;
  grid.slice_stride = 60;
  grid.fields = {
      PointField{.name = "occupancy", .offset = 0, .datatype = PointField::Datatype::kUint8, .count = 1},
      PointField{.name = "cost", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };

  std::vector<uint8_t> bytes(static_cast<size_t>(grid.slice_count) * grid.slice_stride);
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>(i + 1);
  }
  const FieldDescriptor* data_field = findField(FieldTable<VoxelGrid>::view, "data");
  data_field->buffer_assign(&grid, std::move(bytes));
  return grid;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(FieldTableTest, NamesUniqueAndKindsConsistent) {
  // Explicit roots so every specialization is exercised even when a struct
  // (Vector2, Pose) is not reachable from any other root.
  const std::array<const FieldTableView*, 11> roots{
      &FieldTable<FrameTransforms>::view, &FieldTable<ImageAnnotations>::view,
      &FieldTable<Vector2>::view,         &FieldTable<Pose>::view,
      &FieldTable<PointCloud>::view,      &FieldTable<SceneEntities>::view,
      &FieldTable<Image>::view,           &FieldTable<DepthImage>::view,
      &FieldTable<CameraInfo>::view,      &FieldTable<VideoFrame>::view,
      &FieldTable<VoxelGrid>::view,
  };

  std::set<std::string_view> visited;
  std::vector<const FieldTableView*> tables;
  for (const auto* root : roots) {
    collectTables(*root, visited, tables);
  }
  ASSERT_EQ(tables.size(), 33u) << "expected all 33 field_table specializations";

  for (const auto* table : tables) {
    SCOPED_TRACE(table->type_name);
    std::set<std::string_view> names;
    for (const auto& f : table->fields) {
      EXPECT_FALSE(f.name.empty());
      EXPECT_TRUE(names.insert(f.name).second) << "duplicate field name: " << f.name;

      switch (f.kind) {
        case FieldKind::kNumber:
        case FieldKind::kBool:
        case FieldKind::kEnum:
          EXPECT_NE(f.get_number, nullptr);
          EXPECT_NE(f.set_number, nullptr);
          EXPECT_EQ(f.get_int64, nullptr);
          EXPECT_EQ(f.set_int64, nullptr);
          EXPECT_EQ(f.get_string, nullptr);
          EXPECT_EQ(f.set_string, nullptr);
          EXPECT_EQ(f.struct_ptr, nullptr);
          EXPECT_EQ(f.struct_ptr_mut, nullptr);
          EXPECT_EQ(f.nested, nullptr);
          EXPECT_EQ(f.list_size, nullptr);
          EXPECT_EQ(f.buffer, nullptr);
          break;
        case FieldKind::kInt64:
          EXPECT_NE(f.get_int64, nullptr);
          EXPECT_NE(f.set_int64, nullptr);
          EXPECT_EQ(f.get_number, nullptr);
          EXPECT_EQ(f.set_number, nullptr);
          EXPECT_EQ(f.get_string, nullptr);
          EXPECT_EQ(f.struct_ptr, nullptr);
          EXPECT_EQ(f.nested, nullptr);
          EXPECT_EQ(f.list_size, nullptr);
          break;
        case FieldKind::kString:
          EXPECT_NE(f.get_string, nullptr);
          EXPECT_NE(f.set_string, nullptr);
          EXPECT_EQ(f.get_number, nullptr);
          EXPECT_EQ(f.get_int64, nullptr);
          EXPECT_EQ(f.struct_ptr, nullptr);
          EXPECT_EQ(f.nested, nullptr);
          EXPECT_EQ(f.list_size, nullptr);
          break;
        case FieldKind::kStruct:
          EXPECT_NE(f.struct_ptr, nullptr);
          EXPECT_NE(f.struct_ptr_mut, nullptr);
          EXPECT_NE(f.nested, nullptr);
          EXPECT_EQ(f.get_number, nullptr);
          EXPECT_EQ(f.get_int64, nullptr);
          EXPECT_EQ(f.get_string, nullptr);
          EXPECT_EQ(f.list_size, nullptr);
          break;
        case FieldKind::kOptionalNumber:
          EXPECT_NE(f.has_value, nullptr);
          EXPECT_NE(f.get_number, nullptr);
          EXPECT_NE(f.set_number, nullptr);
          EXPECT_EQ(f.get_int64, nullptr);
          EXPECT_EQ(f.get_string, nullptr);
          EXPECT_EQ(f.struct_ptr, nullptr);
          EXPECT_EQ(f.nested, nullptr);
          EXPECT_EQ(f.list_size, nullptr);
          EXPECT_EQ(f.buffer, nullptr);
          break;
        case FieldKind::kList:
          EXPECT_NE(f.list_size, nullptr);
          EXPECT_NE(f.list_at, nullptr);
          EXPECT_EQ(f.struct_ptr, nullptr);
          EXPECT_EQ(f.struct_ptr_mut, nullptr);
          EXPECT_EQ(f.buffer, nullptr);
          // Exactly one of the two write-accessor pairs is set: growable
          // (list_emplace/list_clear, e.g. std::vector) or fixed-size
          // (list_replace, e.g. std::array) — see field_table.hpp's kList
          // doc comment.
          if (f.list_replace != nullptr) {
            EXPECT_EQ(f.list_emplace, nullptr);
            EXPECT_EQ(f.list_clear, nullptr);
          } else {
            EXPECT_NE(f.list_emplace, nullptr);
            EXPECT_NE(f.list_clear, nullptr);
          }
          if (f.nested != nullptr) {
            // Struct-element list: nested table set, element_kind == kStruct,
            // no scalar accessor (the elements are read via `nested`, not get_*/set_*).
            EXPECT_EQ(f.element_kind, FieldKind::kStruct);
            EXPECT_EQ(f.get_number, nullptr);
            EXPECT_EQ(f.get_int64, nullptr);
            EXPECT_EQ(f.get_string, nullptr);
          } else {
            // Scalar-element list: exactly one get_*/set_* pair set, matching element_kind.
            EXPECT_NE(f.element_kind, FieldKind::kStruct);
            switch (f.element_kind) {
              case FieldKind::kNumber:
              case FieldKind::kBool:
              case FieldKind::kEnum:
                EXPECT_NE(f.get_number, nullptr);
                EXPECT_NE(f.set_number, nullptr);
                EXPECT_EQ(f.get_int64, nullptr);
                EXPECT_EQ(f.get_string, nullptr);
                break;
              case FieldKind::kInt64:
                EXPECT_NE(f.get_int64, nullptr);
                EXPECT_NE(f.set_int64, nullptr);
                EXPECT_EQ(f.get_number, nullptr);
                EXPECT_EQ(f.get_string, nullptr);
                break;
              case FieldKind::kString:
                EXPECT_NE(f.get_string, nullptr);
                EXPECT_NE(f.set_string, nullptr);
                EXPECT_EQ(f.get_number, nullptr);
                EXPECT_EQ(f.get_int64, nullptr);
                break;
              default:
                ADD_FAILURE() << "unexpected scalar element_kind";
            }
          }
          break;
        case FieldKind::kBuffer:
          EXPECT_NE(f.buffer, nullptr);
          EXPECT_NE(f.buffer_assign, nullptr);
          EXPECT_EQ(f.get_number, nullptr);
          EXPECT_EQ(f.get_int64, nullptr);
          EXPECT_EQ(f.get_string, nullptr);
          EXPECT_EQ(f.struct_ptr, nullptr);
          EXPECT_EQ(f.list_size, nullptr);
          EXPECT_EQ(f.nested, nullptr);
          break;
      }
    }
  }
}

TEST(FieldTableTest, GenericCopyMatchesCodecRoundTrip) {
  {
    const FrameTransforms src = makeSampleFrameTransforms();
    ASSERT_FALSE(src.transforms.empty());
    FrameTransforms dst;
    copyThroughTable(FieldTable<FrameTransforms>::view, &src, &dst);
    EXPECT_EQ(dst, src);

    expectSameWireBytes(src, dst);
  }
  {
    const ImageAnnotations src = makeSampleImageAnnotations();
    ASSERT_FALSE(src.points.empty());
    ASSERT_FALSE(src.circles.empty());
    ASSERT_FALSE(src.texts.empty());
    ImageAnnotations dst;
    copyThroughTable(FieldTable<ImageAnnotations>::view, &src, &dst);
    EXPECT_EQ(dst, src);

    expectSameWireBytes(src, dst);
  }
  {
    const SceneEntities src = makeSampleSceneEntities();
    ASSERT_FALSE(src.entities.empty());
    ASSERT_FALSE(src.deletions.empty());
    SceneEntities dst;
    copyThroughTable(FieldTable<SceneEntities>::view, &src, &dst);
    EXPECT_EQ(dst, src);

    expectSameWireBytes(src, dst);
  }
}

// Image has no operator== (its payload bytes only compare meaningfully
// through the canonical wire codec, same as PointCloud), so compare
// serialized bytes instead. Exercises kBuffer (data) and kOptionalNumber
// (compressed_depth_min/max, both set on the fixture).
TEST(FieldTableTest, ImageBufferAndOptionalRoundTrip) {
  const Image src = makeSampleImage();
  ASSERT_FALSE(src.data.empty());
  ASSERT_TRUE(src.compressed_depth_min.has_value());
  ASSERT_TRUE(src.compressed_depth_max.has_value());

  Image dst;
  copyThroughTable(FieldTable<Image>::view, &src, &dst);

  const FieldDescriptor* min_field = findField(FieldTable<Image>::view, "compressed_depth_min");
  ASSERT_NE(min_field, nullptr);
  EXPECT_EQ(min_field->kind, FieldKind::kOptionalNumber);
  EXPECT_TRUE(min_field->has_value(&dst));
  EXPECT_FLOAT_EQ(static_cast<float>(min_field->get_number(&dst)), *src.compressed_depth_min);

  expectSameWireBytes(src, dst);
}

// An Image with both optionals absent (the common case: only
// "compressedDepth" images carry them) must round-trip as absent too — a
// freshly-constructed dst starts absent and copyThroughTable's
// kOptionalNumber case only writes when has_value(src) is true.
TEST(FieldTableTest, ImageOptionalAbsentRoundTrip) {
  Image src = makeSampleImage();
  src.compressed_depth_min.reset();
  src.compressed_depth_max.reset();

  Image dst;
  copyThroughTable(FieldTable<Image>::view, &src, &dst);

  const FieldDescriptor* min_field = findField(FieldTable<Image>::view, "compressed_depth_min");
  ASSERT_NE(min_field, nullptr);
  EXPECT_FALSE(min_field->has_value(&dst));

  expectSameWireBytes(src, dst);
}

// DepthImage has no operator== either; exercises kBuffer (data, derived
// row_step/record_step) and the fixed-size kList (K, via list_replace).
TEST(FieldTableTest, DepthImageFixedArrayAndBufferRoundTrip) {
  const DepthImage src = makeSampleDepthImage();
  ASSERT_FALSE(src.data.empty());
  ASSERT_FALSE(src.D.empty());

  const FieldDescriptor* k_field = findField(FieldTable<DepthImage>::view, "K");
  ASSERT_NE(k_field, nullptr);
  EXPECT_EQ(k_field->kind, FieldKind::kList);
  EXPECT_NE(k_field->list_replace, nullptr);
  EXPECT_EQ(k_field->list_emplace, nullptr);
  EXPECT_EQ(k_field->list_size(&src), 9u);

  DepthImage dst;
  copyThroughTable(FieldTable<DepthImage>::view, &src, &dst);
  EXPECT_EQ(dst.K, src.K);
  EXPECT_EQ(dst.D, src.D);

  expectSameWireBytes(src, dst);
}

// CameraInfo has operator== (no byte blob), so both the value comparison and
// the serialized-bytes comparison apply. Exercises the fixed-size kList
// (K, R, P) alongside the ordinary growable list (D).
TEST(FieldTableTest, CameraInfoFixedArrayRoundTrip) {
  const CameraInfo src = makeSampleCameraInfo();
  ASSERT_FALSE(src.D.empty());

  const FieldDescriptor* r_field = findField(FieldTable<CameraInfo>::view, "R");
  ASSERT_NE(r_field, nullptr);
  EXPECT_EQ(r_field->list_size(&src), 9u);
  const FieldDescriptor* p_field = findField(FieldTable<CameraInfo>::view, "P");
  ASSERT_NE(p_field, nullptr);
  EXPECT_EQ(p_field->list_size(&src), 12u);
  const FieldDescriptor* d_field = findField(FieldTable<CameraInfo>::view, "D");
  ASSERT_NE(d_field, nullptr);
  EXPECT_NE(d_field->list_emplace, nullptr);  // D is growable (std::vector), unlike K/R/P.
  EXPECT_EQ(d_field->list_replace, nullptr);

  CameraInfo dst;
  copyThroughTable(FieldTable<CameraInfo>::view, &src, &dst);
  EXPECT_EQ(dst, src);

  expectSameWireBytes(src, dst);
}

// VideoFrame has no operator== either; exercises kBuffer for a format with
// no static per-record size (record_step/record_count/row_step all 0).
TEST(FieldTableTest, VideoFrameBufferRoundTrip) {
  const VideoFrame src = makeSampleVideoFrame();
  ASSERT_FALSE(src.data.empty());

  const FieldDescriptor* data_field = findField(FieldTable<VideoFrame>::view, "data");
  ASSERT_NE(data_field, nullptr);
  const BufferLayout layout = data_field->buffer(&src);
  EXPECT_EQ(layout.record_step, 0u);
  EXPECT_EQ(layout.record_count, 0u);
  EXPECT_EQ(layout.row_step, 0u);
  ASSERT_EQ(layout.channels.size(), 1u);
  EXPECT_EQ(layout.channels[0].name, "h264");

  VideoFrame dst;
  copyThroughTable(FieldTable<VideoFrame>::view, &src, &dst);

  expectSameWireBytes(src, dst);
}

// The buffer descriptor's layout for a raw, uncompressed encoding: a 4x3
// rgb8 image (3 bytes/pixel, no row padding).
TEST(FieldTableTest, ImageRgb8BufferLayout) {
  const Image image = makeSampleImage();
  ASSERT_EQ(image.width, 4u);
  ASSERT_EQ(image.height, 3u);
  ASSERT_EQ(image.encoding, "rgb8");

  const FieldDescriptor* data_field = findField(FieldTable<Image>::view, "data");
  ASSERT_NE(data_field, nullptr);
  const BufferLayout layout = data_field->buffer(&image);
  EXPECT_EQ(layout.record_step, 3u);    // 3 bytes/pixel.
  EXPECT_EQ(layout.record_count, 12u);  // 4 * 3 pixels.
  EXPECT_EQ(layout.row_step, 12u);      // 4 pixels * 3 bytes, no padding.
  EXPECT_FALSE(layout.is_bigendian);
  ASSERT_EQ(layout.channels.size(), 1u);
  EXPECT_EQ(layout.channels[0].name, "rgb8");
  EXPECT_EQ(layout.channels[0].count, 3u);
}

// A compressed encoding has no static per-pixel size: record_step and
// record_count are 0, but the encoding string still comes through the sole
// channel's name, and the full compressed payload is still in `bytes`.
TEST(FieldTableTest, ImageCompressedBufferLayoutHasNoStaticRecordSize) {
  Image image = makeSampleImage();
  image.encoding = "jpeg";

  const FieldDescriptor* data_field = findField(FieldTable<Image>::view, "data");
  ASSERT_NE(data_field, nullptr);
  const BufferLayout layout = data_field->buffer(&image);
  EXPECT_EQ(layout.record_step, 0u);
  EXPECT_EQ(layout.record_count, 0u);
  EXPECT_FALSE(layout.bytes.empty());
  ASSERT_EQ(layout.channels.size(), 1u);
  EXPECT_EQ(layout.channels[0].name, "jpeg");
}

TEST(FieldTableTest, Int64FieldsAreNotNumbers) {
  constexpr int64_t kBig = (int64_t{1} << 55) + 12345;  // > 2^53: exact only as int64.

  const FieldDescriptor* ft_timestamp = findField(FieldTable<FrameTransform>::view, "timestamp");
  ASSERT_NE(ft_timestamp, nullptr);
  EXPECT_EQ(ft_timestamp->kind, FieldKind::kInt64);
  EXPECT_EQ(ft_timestamp->get_number, nullptr);
  EXPECT_EQ(ft_timestamp->set_number, nullptr);
  ASSERT_NE(ft_timestamp->get_int64, nullptr);
  ASSERT_NE(ft_timestamp->set_int64, nullptr);

  FrameTransform ft;
  ft_timestamp->set_int64(&ft, kBig);
  EXPECT_EQ(ft.timestamp, kBig);
  EXPECT_EQ(ft_timestamp->get_int64(&ft), kBig);

  const FieldDescriptor* ia_timestamp = findField(FieldTable<ImageAnnotations>::view, "timestamp");
  ASSERT_NE(ia_timestamp, nullptr);
  EXPECT_EQ(ia_timestamp->kind, FieldKind::kInt64);
  EXPECT_EQ(ia_timestamp->get_number, nullptr);
  EXPECT_EQ(ia_timestamp->set_number, nullptr);

  ImageAnnotations ia;
  ia_timestamp->set_int64(&ia, kBig);
  EXPECT_EQ(ia.timestamp, kBig);
  EXPECT_EQ(ia_timestamp->get_int64(&ia), kBig);
}

TEST(FieldTableTest, PointCloudBufferRoundTrip) {
  const PointCloud src = makeSamplePointCloudUnorganized();
  const FieldDescriptor* data_field = findField(FieldTable<PointCloud>::view, "data");
  ASSERT_NE(data_field, nullptr);
  EXPECT_EQ(data_field->kind, FieldKind::kBuffer);
  ASSERT_NE(data_field->buffer, nullptr);
  ASSERT_NE(data_field->buffer_assign, nullptr);

  const BufferLayout layout = data_field->buffer(&src);
  EXPECT_EQ(layout.record_step, 16u);
  EXPECT_EQ(layout.record_count, 3u);
  EXPECT_EQ(layout.row_step, 48u);
  EXPECT_FALSE(layout.is_bigendian);
  ASSERT_EQ(layout.channels.size(), 4u);
  EXPECT_EQ(layout.channels[0].name, "x");
  EXPECT_EQ(layout.channels[0].offset, 0u);
  EXPECT_EQ(layout.channels[0].datatype, static_cast<uint8_t>(PointField::Datatype::kFloat32));
  EXPECT_EQ(layout.channels[0].count, 1u);
  EXPECT_EQ(layout.channels[1].name, "y");
  EXPECT_EQ(layout.channels[1].offset, 4u);
  EXPECT_EQ(layout.channels[2].name, "z");
  EXPECT_EQ(layout.channels[2].offset, 8u);
  EXPECT_EQ(layout.channels[3].name, "intensity");
  EXPECT_EQ(layout.channels[3].offset, 12u);

  // PointCloud has no operator== (payload bytes only compare meaningfully
  // through the canonical wire codec), so compare serialized bytes instead.
  PointCloud dst;
  copyThroughTable(FieldTable<PointCloud>::view, &src, &dst);

  expectSameWireBytes(src, dst);
}

TEST(FieldTableTest, PointCloudOrganizedBufferLayout) {
  PointCloud cloud;
  cloud.width = 2;
  cloud.height = 2;
  cloud.point_step = 16;
  cloud.row_step = 40;  // 2 * 16 + 8 bytes padding.
  cloud.frame_id = "camera_depth";
  cloud.fields = {
      PointField{.name = "x", .offset = 0, .datatype = PointField::Datatype::kFloat32, .count = 1},
      PointField{.name = "y", .offset = 4, .datatype = PointField::Datatype::kFloat32, .count = 1},
      PointField{.name = "z", .offset = 8, .datatype = PointField::Datatype::kFloat32, .count = 1},
      PointField{.name = "intensity", .offset = 12, .datatype = PointField::Datatype::kFloat32, .count = 1},
  };

  const FieldDescriptor* data_field = findField(FieldTable<PointCloud>::view, "data");
  ASSERT_NE(data_field, nullptr);
  std::vector<uint8_t> bytes(static_cast<size_t>(cloud.row_step) * cloud.height, 0);
  data_field->buffer_assign(&cloud, std::move(bytes));

  const BufferLayout layout = data_field->buffer(&cloud);
  EXPECT_EQ(layout.record_step, 16u);
  EXPECT_EQ(layout.record_count, 4u);  // width * height, not row-count via row_step.
  EXPECT_EQ(layout.row_step, 40u);
}

// Size fields that declare more records than `bytes` holds must not be
// promised: record_count falls back to 0 so a consumer never reads past the
// span.
TEST(FieldTableTest, PointCloudBufferLayoutClampsToBytes) {
  const FieldDescriptor* data_field = findField(FieldTable<PointCloud>::view, "data");
  ASSERT_NE(data_field, nullptr);

  PointCloud wider = makeSamplePointCloudUnorganized();  // 3 points, 48 bytes.
  wider.width = 4;
  EXPECT_EQ(data_field->buffer(&wider).record_count, 0u);

  PointCloud taller = makeSamplePointCloudUnorganized();
  taller.height = 2;
  EXPECT_EQ(data_field->buffer(&taller).record_count, 0u);

  // The last row needs no trailing padding: 40 + 2 * 16 = 72 bytes hold a 2x2 cloud with row_step 40.
  PointCloud padded;
  padded.width = 2;
  padded.height = 2;
  padded.point_step = 16;
  padded.row_step = 40;
  data_field->buffer_assign(&padded, std::vector<uint8_t>(72, 0));
  EXPECT_EQ(data_field->buffer(&padded).record_count, 4u);
  data_field->buffer_assign(&padded, std::vector<uint8_t>(71, 0));
  EXPECT_EQ(data_field->buffer(&padded).record_count, 0u);

  // A declared extent that overflows 64 bits is not satisfiable either.
  PointCloud huge = makeSamplePointCloudUnorganized();
  huge.width = UINT32_MAX;
  huge.height = UINT32_MAX;
  huge.point_step = UINT32_MAX;
  huge.row_step = UINT32_MAX;
  EXPECT_EQ(data_field->buffer(&huge).record_count, 0u);
}

TEST(FieldTableTest, ImageBufferLayoutClampsToBytes) {
  const FieldDescriptor* data_field = findField(FieldTable<Image>::view, "data");
  ASSERT_NE(data_field, nullptr);

  Image taller = makeSampleImage();  // 4x3 rgb8, 36 bytes.
  taller.height = 4;
  EXPECT_EQ(data_field->buffer(&taller).record_count, 0u);

  Image padded = makeSampleImage();
  padded.row_step = 16;  // 2 * 16 + 12 = 44 bytes needed.
  EXPECT_EQ(data_field->buffer(&padded).record_count, 0u);

  const Image consistent = makeSampleImage();
  EXPECT_EQ(data_field->buffer(&consistent).record_count, 12u);
}

TEST(FieldTableTest, DepthImageBufferLayoutClampsToBytes) {
  const FieldDescriptor* data_field = findField(FieldTable<DepthImage>::view, "data");
  ASSERT_NE(data_field, nullptr);

  DepthImage wider = makeSampleDepthImage();  // 2x2 32FC1, 16 bytes.
  wider.width = 3;
  EXPECT_EQ(data_field->buffer(&wider).record_count, 0u);

  const DepthImage consistent = makeSampleDepthImage();
  EXPECT_EQ(data_field->buffer(&consistent).record_count, 4u);
}

// VoxelGrid has no operator== (payload bytes only compare meaningfully through
// the canonical wire codec), so compare serialized bytes. Exercises the nested
// Pose/Vector3 tables, the PointField list and the kBuffer.
TEST(FieldTableTest, VoxelGridCopyMatchesCodecRoundTrip) {
  const VoxelGrid src = makeSampleVoxelGrid();
  ASSERT_FALSE(src.data.empty());
  ASSERT_FALSE(src.fields.empty());

  VoxelGrid dst;
  copyThroughTable(FieldTable<VoxelGrid>::view, &src, &dst);

  expectSameWireBytes(src, dst);
  EXPECT_EQ(dst.origin, src.origin);
  EXPECT_EQ(dst.fields.size(), src.fields.size());
}

// Every VoxelGrid member reads back through its descriptor, as a script binder
// would: scalars, int64 timestamp, the Pose/Vector3 structs, the PointField list.
TEST(FieldTableTest, VoxelGridFieldsReadBack) {
  const VoxelGrid grid = makeSampleVoxelGrid();
  const FieldTableView& table = FieldTable<VoxelGrid>::view;
  EXPECT_EQ(table.type_name, "VoxelGrid");
  EXPECT_EQ(table.fields.size(), 12u);

  const auto number = [&](std::string_view name) {
    const FieldDescriptor* f = findField(table, name);
    EXPECT_NE(f, nullptr) << name;
    EXPECT_EQ(f->kind, FieldKind::kNumber) << name;
    return f->get_number(&grid);
  };
  const auto nestedNumber = [&](std::string_view name, std::string_view member) {
    const FieldDescriptor* f = findField(table, name);
    EXPECT_NE(f, nullptr) << name;
    EXPECT_EQ(f->kind, FieldKind::kStruct) << name;
    const FieldDescriptor* m = findField(*f->nested, member);
    EXPECT_NE(m, nullptr) << member;
    return m->get_number(f->struct_ptr(&grid));
  };

  const FieldDescriptor* timestamp = findField(table, "timestamp_ns");
  ASSERT_NE(timestamp, nullptr);
  EXPECT_EQ(timestamp->kind, FieldKind::kInt64);
  EXPECT_EQ(timestamp->get_int64(&grid), 9'007'199'254'740'993);

  const FieldDescriptor* frame_id = findField(table, "frame_id");
  ASSERT_NE(frame_id, nullptr);
  EXPECT_EQ(frame_id->kind, FieldKind::kString);
  EXPECT_EQ(frame_id->get_string(&grid), "map");

  const FieldDescriptor* origin = findField(table, "origin");
  ASSERT_NE(origin, nullptr);
  EXPECT_EQ(origin->nested, &FieldTable<Pose>::view);
  const FieldDescriptor* position = findField(*origin->nested, "position");
  ASSERT_NE(position, nullptr);
  const FieldDescriptor* position_z = findField(*position->nested, "z");
  ASSERT_NE(position_z, nullptr);
  EXPECT_DOUBLE_EQ(position_z->get_number(position->struct_ptr(origin->struct_ptr(&grid))), 3.0);
  const FieldDescriptor* orientation = findField(*origin->nested, "orientation");
  ASSERT_NE(orientation, nullptr);
  const FieldDescriptor* orientation_w = findField(*orientation->nested, "w");
  ASSERT_NE(orientation_w, nullptr);
  EXPECT_DOUBLE_EQ(orientation_w->get_number(orientation->struct_ptr(origin->struct_ptr(&grid))), 0.8);

  EXPECT_DOUBLE_EQ(nestedNumber("cell_size", "x"), 0.1);
  EXPECT_DOUBLE_EQ(nestedNumber("cell_size", "y"), 0.2);
  EXPECT_DOUBLE_EQ(nestedNumber("cell_size", "z"), 0.3);

  EXPECT_EQ(number("column_count"), 3.0);
  EXPECT_EQ(number("row_count"), 2.0);
  EXPECT_EQ(number("slice_count"), 2.0);
  EXPECT_EQ(number("cell_stride"), 8.0);
  EXPECT_EQ(number("row_stride"), 28.0);
  EXPECT_EQ(number("slice_stride"), 60.0);

  const FieldDescriptor* fields = findField(table, "fields");
  ASSERT_NE(fields, nullptr);
  ASSERT_EQ(fields->kind, FieldKind::kList);
  EXPECT_EQ(fields->element_kind, FieldKind::kStruct);
  EXPECT_EQ(fields->nested, &FieldTable<PointField>::view);
  ASSERT_EQ(fields->list_size(&grid), 2u);
  const void* cost = fields->list_at(&grid, 1);
  EXPECT_EQ(findField(*fields->nested, "name")->get_string(cost), "cost");
  EXPECT_EQ(findField(*fields->nested, "offset")->get_number(cost), 4.0);
  EXPECT_EQ(
      findField(*fields->nested, "datatype")->get_number(cost),
      static_cast<double>(static_cast<uint8_t>(PointField::Datatype::kFloat32)));
  EXPECT_EQ(findField(*fields->nested, "count")->get_number(cost), 1.0);

  const FieldDescriptor* data_field = findField(table, "data");
  ASSERT_NE(data_field, nullptr);
  EXPECT_EQ(data_field->kind, FieldKind::kBuffer);
  EXPECT_EQ(data_field->buffer(&grid).bytes.size(), 120u);
}

TEST(FieldTableTest, VoxelGridBufferLayout) {
  const VoxelGrid grid = makeSampleVoxelGrid();
  const FieldDescriptor* data_field = findField(FieldTable<VoxelGrid>::view, "data");
  ASSERT_NE(data_field, nullptr);
  ASSERT_NE(data_field->buffer, nullptr);
  ASSERT_NE(data_field->buffer_assign, nullptr);

  const BufferLayout layout = data_field->buffer(&grid);
  EXPECT_EQ(layout.record_step, 8u);
  EXPECT_EQ(layout.record_count, 12u);  // 3 * 2 * 2.
  EXPECT_EQ(layout.row_step, 28u);
  EXPECT_FALSE(layout.is_bigendian);
  ASSERT_EQ(layout.channels.size(), 2u);
  EXPECT_EQ(layout.channels[0].name, "occupancy");
  EXPECT_EQ(layout.channels[0].offset, 0u);
  EXPECT_EQ(layout.channels[0].datatype, static_cast<uint8_t>(PointField::Datatype::kUint8));
  EXPECT_EQ(layout.channels[0].count, 1u);
  EXPECT_EQ(layout.channels[1].name, "cost");
  EXPECT_EQ(layout.channels[1].offset, 4u);
  EXPECT_EQ(layout.channels[1].datatype, static_cast<uint8_t>(PointField::Datatype::kFloat32));

  // Voxel (cx=2, ry=1, sz=1) sits at 1*60 + 1*28 + 2*8 = 104 (stored bytes are index + 1).
  ASSERT_EQ(layout.bytes.size(), 120u);
  EXPECT_EQ(layout.bytes[104], 105u);
}

// Size fields that declare more voxels than `data` holds must not be promised
// (record_count 0), while a buffer that only lacks the last row's and slice's
// trailing padding is complete.
TEST(FieldTableTest, VoxelGridBufferLayoutClampsToBytes) {
  const FieldDescriptor* data_field = findField(FieldTable<VoxelGrid>::view, "data");
  ASSERT_NE(data_field, nullptr);
  const VoxelGrid full = makeSampleVoxelGrid();  // 120 bytes; last voxel ends at 1*60 + 1*28 + 24 = 112.
  const auto withBytes = [&](size_t size) {
    VoxelGrid grid = full;
    data_field->buffer_assign(&grid, std::vector<uint8_t>(size));
    return data_field->buffer(&grid).record_count;
  };
  EXPECT_EQ(withBytes(120), 12u);
  EXPECT_EQ(withBytes(112), 12u);
  EXPECT_EQ(withBytes(111), 0u);
  EXPECT_EQ(withBytes(0), 0u);

  VoxelGrid deeper = full;
  deeper.slice_count = 3;
  EXPECT_EQ(data_field->buffer(&deeper).record_count, 0u);

  for (uint32_t VoxelGrid::* count : {&VoxelGrid::column_count, &VoxelGrid::row_count, &VoxelGrid::slice_count}) {
    VoxelGrid empty = full;
    empty.*count = 0;
    EXPECT_EQ(data_field->buffer(&empty).record_count, 0u);
  }
  VoxelGrid no_stride = full;
  no_stride.cell_stride = 0;
  EXPECT_EQ(data_field->buffer(&no_stride).record_count, 0u);

  // Hostile size fields must neither overflow nor be believed.
  VoxelGrid huge = full;
  huge.column_count = huge.row_count = huge.slice_count = UINT32_MAX;
  huge.cell_stride = huge.row_stride = huge.slice_stride = UINT32_MAX;
  EXPECT_EQ(data_field->buffer(&huge).record_count, 0u);
}

// Strides shorter than what they step over break VoxelGrid's documented layout: a zero
// row stride must not read as tightly packed, and overlapping rows/slices must not let
// the count product wrap past an extent check.
TEST(FieldTableTest, VoxelGridBufferLayoutRejectsUndersizedStrides) {
  const FieldDescriptor* data_field = findField(FieldTable<VoxelGrid>::view, "data");
  ASSERT_NE(data_field, nullptr);
  const auto recordsOf = [&](VoxelGrid grid, size_t size) {
    data_field->buffer_assign(&grid, std::vector<uint8_t>(size));
    return data_field->buffer(&grid).record_count;
  };
  VoxelGrid zero_row;
  zero_row.column_count = 1;
  zero_row.row_count = 2;
  zero_row.slice_count = 2;
  zero_row.cell_stride = 1;
  zero_row.row_stride = 0;
  zero_row.slice_stride = 2;
  EXPECT_EQ(recordsOf(zero_row, 3), 0u);
  EXPECT_EQ(recordsOf(zero_row, 4), 0u);

  VoxelGrid overlapping;
  overlapping.column_count = 4194304;
  overlapping.row_count = 4194304;
  overlapping.slice_count = 1048577;
  overlapping.cell_stride = overlapping.row_stride = overlapping.slice_stride = 1;
  EXPECT_EQ(recordsOf(overlapping, 9437183), 0u);

  VoxelGrid short_slice = makeSampleVoxelGrid();  // rows span 2 * 28 = 56 bytes
  short_slice.slice_stride = 55;
  EXPECT_EQ(recordsOf(short_slice, 120), 0u);
}

// set_number never performs an undefined float-to-integer conversion: NaN
// reads back 0 and out-of-range values clamp to the target type's limits.
TEST(FieldTableTest, SetNumberSaturatesIntegerTargets) {
  const FieldDescriptor* red = findField(FieldTable<ColorRGBA>::view, "r");
  ASSERT_NE(red, nullptr);
  ColorRGBA color;
  red->set_number(&color, 300.0);
  EXPECT_EQ(color.r, 255);
  red->set_number(&color, -5.0);
  EXPECT_EQ(color.r, 0);
  red->set_number(&color, 200.7);
  EXPECT_EQ(color.r, 200);
  color.r = 9;
  red->set_number(&color, std::numeric_limits<double>::quiet_NaN());
  EXPECT_EQ(color.r, 0);

  const FieldDescriptor* width = findField(FieldTable<Image>::view, "width");
  ASSERT_NE(width, nullptr);
  Image image;
  width->set_number(&image, -1.0);
  EXPECT_EQ(image.width, 0u);
  width->set_number(&image, 1e20);
  EXPECT_EQ(image.width, UINT32_MAX);
  width->set_number(&image, -std::numeric_limits<double>::infinity());
  EXPECT_EQ(image.width, 0u);
}

TEST(FieldTableTest, SetNumberSaturatesEnumUnderlyingRange) {
  const FieldDescriptor* datatype = findField(FieldTable<PointField>::view, "datatype");
  ASSERT_NE(datatype, nullptr);
  ASSERT_EQ(datatype->kind, FieldKind::kEnum);
  PointField field;
  datatype->set_number(&field, 300.0);
  EXPECT_EQ(static_cast<uint8_t>(field.datatype), 255);
  datatype->set_number(&field, -1.0);
  EXPECT_EQ(static_cast<uint8_t>(field.datatype), 0);
}

// A float target keeps its plain conversion: no clamping to an integer range.
TEST(FieldTableTest, SetNumberKeepsFloatTargets) {
  const FieldDescriptor* depth_min = findField(FieldTable<Image>::view, "compressed_depth_min");
  ASSERT_NE(depth_min, nullptr);
  Image image;
  depth_min->set_number(&image, 1e20);
  ASSERT_TRUE(image.compressed_depth_min.has_value());
  EXPECT_FLOAT_EQ(*image.compressed_depth_min, 1e20f);
}

TEST(FieldTableTest, DescribeCoversExactlyTheTabledTypes) {
  EXPECT_NE(PJ::sdk::describe(BuiltinObjectType::kFrameTransforms), nullptr);
  EXPECT_NE(PJ::sdk::describe(BuiltinObjectType::kImageAnnotations), nullptr);
  EXPECT_NE(PJ::sdk::describe(BuiltinObjectType::kPointCloud), nullptr);
  EXPECT_NE(PJ::sdk::describe(BuiltinObjectType::kSceneEntities), nullptr);
  EXPECT_NE(PJ::sdk::describe(BuiltinObjectType::kImage), nullptr);
  EXPECT_NE(PJ::sdk::describe(BuiltinObjectType::kDepthImage), nullptr);
  EXPECT_NE(PJ::sdk::describe(BuiltinObjectType::kCameraInfo), nullptr);
  EXPECT_NE(PJ::sdk::describe(BuiltinObjectType::kVideoFrame), nullptr);
  EXPECT_EQ(PJ::sdk::describe(BuiltinObjectType::kVoxelGrid), &FieldTable<VoxelGrid>::view);

  // Every other stable BuiltinObjectType value (mirrors the array in
  // builtin_object_codec_test.cpp) must resolve to nullptr.
  const std::array<BuiltinObjectType, 9> other_types{
      BuiltinObjectType::kOccupancyGrid,
      BuiltinObjectType::kCompressedPointCloud,
      BuiltinObjectType::kMesh3D,
      BuiltinObjectType::kRobotDescription,
      BuiltinObjectType::kOccupancyGridUpdate,
      BuiltinObjectType::kLog,
      BuiltinObjectType::kPosesInFrame,
      BuiltinObjectType::kGridMap,
      BuiltinObjectType::kPlotMarkers,
  };
  for (auto type : other_types) {
    EXPECT_EQ(PJ::sdk::describe(type), nullptr) << PJ::sdk::name(type);
  }
  EXPECT_EQ(PJ::sdk::describe(BuiltinObjectType::kNone), nullptr);
}

}  // namespace
