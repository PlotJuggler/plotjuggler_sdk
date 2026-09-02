#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file object_writer.hpp
 * @brief Fallible canonical-wire and module output-descriptor builders.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/parser_module/core.hpp"

namespace pj {

class WireWriter {
 public:
  [[nodiscard]] const Status& status() const noexcept {
    return status_;
  }

  [[nodiscard]] Status tag(uint32_t field_number, uint8_t wire_type) {
    if (field_number == 0 || field_number > UINT32_C(0x1FFFFFFF) || wire_type > 5 || wire_type == 3 || wire_type == 4) {
      return fail("invalid canonical-wire tag");
    }
    return varint((static_cast<uint64_t>(field_number) << 3U) | wire_type);
  }

  [[nodiscard]] Status varint(uint64_t value) {
    if (!status_.isOk()) {
      return status_;
    }
    do {
      uint8_t byte = static_cast<uint8_t>(value & UINT64_C(0x7F));
      value >>= 7U;
      if (value != 0) {
        byte |= UINT8_C(0x80);
      }
      Status pushed = bytes_.push(byte);
      if (!pushed.isOk()) {
        return fail(pushed.message());
      }
    } while (value != 0);
    return Status::ok();
  }

  [[nodiscard]] Status varintField(uint32_t field_number, uint64_t value) {
    Status tagged = tag(field_number, 0);
    return tagged.isOk() ? varint(value) : tagged;
  }

  [[nodiscard]] Status fixed32Field(uint32_t field_number, uint32_t value) {
    Status tagged = tag(field_number, 5);
    if (!tagged.isOk()) {
      return tagged;
    }
    return littleEndian(value, 4);
  }

  [[nodiscard]] Status fixed64Field(uint32_t field_number, uint64_t value) {
    Status tagged = tag(field_number, 1);
    if (!tagged.isOk()) {
      return tagged;
    }
    return littleEndian(value, 8);
  }

  [[nodiscard]] Status rawFixed64(uint64_t value) {
    return littleEndian(value, 8);
  }

  [[nodiscard]] Status doubleField(uint32_t field_number, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return fixed64Field(field_number, bits);
  }

  [[nodiscard]] Status floatField(uint32_t field_number, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return fixed32Field(field_number, bits);
  }

  [[nodiscard]] Status lengthDelimited(uint32_t field_number, ByteView value) {
    Status tagged = tag(field_number, 2);
    if (!tagged.isOk()) {
      return tagged;
    }
    Status length = varint(value.size);
    if (!length.isOk()) {
      return length;
    }
    Status appended = bytes_.append(value);
    return appended.isOk() ? Status::ok() : fail(appended.message());
  }

  [[nodiscard]] Status stringField(uint32_t field_number, std::string_view value) {
    return lengthDelimited(field_number, ByteView(reinterpret_cast<const uint8_t*>(value.data()), value.size()));
  }

  [[nodiscard]] Status messageField(uint32_t field_number, const WireWriter& nested) {
    if (!nested.status().isOk()) {
      return fail(nested.status().message());
    }
    return lengthDelimited(field_number, nested.view());
  }

  [[nodiscard]] ByteView view() const noexcept {
    return bytes_.view();
  }

  [[nodiscard]] Blob take() noexcept {
    return std::move(bytes_);
  }

 private:
  [[nodiscard]] Status littleEndian(uint64_t value, size_t width) {
    for (size_t index = 0; index < width; ++index) {
      Status pushed = bytes_.push(static_cast<uint8_t>(value >> (index * 8U)));
      if (!pushed.isOk()) {
        return fail(pushed.message());
      }
    }
    return Status::ok();
  }

  [[nodiscard]] Status fail(std::string_view message) {
    if (status_.isOk()) {
      status_ = Status::error(message);
    }
    return status_;
  }

  Blob bytes_;
  Status status_;
};

class ObjectWriter {
 public:
  static constexpr uint16_t kImageObjectType = 1;
  static constexpr uint16_t kPointCloudObjectType = 3;
  static constexpr uint16_t kDepthImageObjectType = 4;
  static constexpr uint16_t kOccupancyGridObjectType = 7;
  static constexpr uint16_t kCompressedPointCloudObjectType = 8;
  static constexpr uint16_t kMesh3DObjectType = 9;
  static constexpr uint16_t kVideoFrameObjectType = 10;
  static constexpr uint16_t kOccupancyGridUpdateObjectType = 15;
  static constexpr uint16_t kVoxelGridObjectType = 18;
  static constexpr uint16_t kGridMapObjectType = 20;
  static constexpr uint32_t kImageDataField = 7;
  static constexpr uint32_t kPointCloudDataField = 9;
  static constexpr uint32_t kDepthImageDataField = 5;
  static constexpr uint32_t kOccupancyGridDataField = 7;
  static constexpr uint32_t kCompressedPointCloudDataField = 4;
  static constexpr uint32_t kMesh3DDataField = 7;
  static constexpr uint32_t kVideoFrameDataField = 3;
  static constexpr uint32_t kOccupancyGridUpdateDataField = 7;
  static constexpr uint32_t kVoxelGridDataField = 12;
  static constexpr uint32_t kGridMapDataField = 10;

  enum class PointFieldDatatype : uint32_t {
    kUnknown = 0,
    kInt8 = 1,
    kUint8 = 2,
    kInt16 = 3,
    kUint16 = 4,
    kInt32 = 5,
    kUint32 = 6,
    kFloat32 = 7,
    kFloat64 = 8,
  };

  class PointCloudBuilder;
  class ImageBuilder;
  class DepthImageBuilder;
  class OccupancyGridBuilder;
  class CompressedPointCloudBuilder;
  class Mesh3DBuilder;
  class VideoFrameBuilder;
  class OccupancyGridUpdateBuilder;
  class VoxelGridBuilder;
  class GridMapBuilder;

  explicit ObjectWriter(PayloadView input_payload = {}) : input_payload_(input_payload) {}

  [[nodiscard]] PointCloudBuilder pointCloud();
  [[nodiscard]] ImageBuilder image();
  [[nodiscard]] DepthImageBuilder depthImage();
  [[nodiscard]] OccupancyGridBuilder occupancyGrid();
  [[nodiscard]] CompressedPointCloudBuilder compressedPointCloud();
  [[nodiscard]] Mesh3DBuilder mesh3D();
  [[nodiscard]] VideoFrameBuilder videoFrame();
  [[nodiscard]] OccupancyGridUpdateBuilder occupancyGridUpdate();
  [[nodiscard]] VoxelGridBuilder voxelGrid();
  [[nodiscard]] GridMapBuilder gridMap();

  [[nodiscard]] const Status& status() const noexcept {
    return status_;
  }

  /// Produce one object-route OutputDescriptor v1 block.
  [[nodiscard]] Expected<Blob> finish() {
    if (!status_.isOk()) {
      return status_;
    }
    if (kind_ == Kind::kNone) {
      return Status::error("ObjectWriter has no selected object type");
    }
    auto wire = writeSelected();
    if (!wire) {
      return wire.status();
    }
    Blob descriptor;
    Status reserved = descriptor.reserve(40 + wire->size());
    if (!reserved.isOk()) {
      return reserved;
    }
    const uint16_t object_type = selectedObjectType();
    const uint32_t splice_field = selectedSpliceField();
    const bool has_splice = splice_.has_value;
    Status result = appendLittle(descriptor, 1, 2);
    if (result.isOk()) {
      result = descriptor.push(2);
    }
    if (result.isOk()) {
      result = descriptor.push(0);
    }
    if (result.isOk()) {
      result = appendLittle(descriptor, object_type, 2);
    }
    if (result.isOk()) {
      result = appendLittle(descriptor, has_splice ? 1 : 0, 2);
    }
    if (result.isOk()) {
      result = appendLittle(descriptor, has_splice ? splice_field : 0, 4);
    }
    if (result.isOk()) {
      result = appendLittle(descriptor, has_splice ? splice_.reference.offset : 0, 8);
    }
    if (result.isOk()) {
      result = appendLittle(descriptor, has_splice ? splice_.reference.length : 0, 8);
    }
    if (result.isOk()) {
      result = appendLittle(descriptor, wire->size(), 8);
    }
    if (result.isOk()) {
      result = descriptor.append(wire->view());
    }
    return result.isOk() ? Expected<Blob>(std::move(descriptor)) : Expected<Blob>(result);
  }

 private:
  enum class Kind : uint8_t {
    kNone,
    kPointCloud,
    kImage,
    kDepthImage,
    kOccupancyGrid,
    kCompressedPointCloud,
    kMesh3D,
    kVideoFrame,
    kOccupancyGridUpdate,
    kVoxelGrid,
    kGridMap,
  };

  struct Vector2State {
    double x = 0;
    double y = 0;
  };

  struct Vector3State {
    double x = 0;
    double y = 0;
    double z = 0;
  };

  struct QuaternionState {
    double x = 0;
    double y = 0;
    double z = 0;
    double w = 1;
  };

  struct PoseState {
    Vector3State position;
    QuaternionState orientation;
  };

  struct ColorState {
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 1;
  };

  struct PointFieldState {
    std::string name;
    uint32_t offset = 0;
    PointFieldDatatype datatype = PointFieldDatatype::kUnknown;
    uint32_t count = 1;
  };

  struct PointCloudState {
    int64_t timestamp_ns = 0;
    uint32_t width = 0;
    uint32_t height = 1;
    uint32_t point_step = 0;
    uint32_t row_step = 0;
    bool is_bigendian = false;
    bool is_dense = true;
    std::vector<PointFieldState> fields;
    Blob data;
    std::string frame_id;
  };

  struct ImageState {
    int64_t timestamp_ns = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string encoding;
    uint32_t row_step = 0;
    bool is_bigendian = false;
    Blob data;
    bool has_compressed_depth_min = false;
    float compressed_depth_min = 0;
    bool has_compressed_depth_max = false;
    float compressed_depth_max = 0;
    std::string frame_id;
  };

  struct DepthImageState {
    int64_t timestamp_ns = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string encoding;
    Blob data;
    std::array<double, 9> intrinsics{};
    std::string distortion_model;
    std::vector<double> distortion;
  };

  struct OccupancyGridState {
    int64_t timestamp_ns = 0;
    std::string frame_id;
    PoseState origin;
    double resolution = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    Blob data;
  };

  struct CompressedPointCloudState {
    int64_t timestamp_ns = 0;
    std::string frame_id;
    std::string format;
    Blob data;
  };

  struct Mesh3DState {
    int64_t timestamp_ns = 0;
    std::string frame_id;
    std::string id;
    PoseState pose;
    Vector3State scale{1, 1, 1};
    std::string format;
    Blob data;
    std::string url;
    ColorState color;
    bool override_color = false;
  };

  struct VideoFrameState {
    int64_t timestamp_ns = 0;
    std::string frame_id;
    Blob data;
    std::string format;
  };

  struct OccupancyGridUpdateState {
    int64_t timestamp_ns = 0;
    std::string frame_id;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    Blob data;
  };

  struct VoxelGridState {
    int64_t timestamp_ns = 0;
    std::string frame_id;
    PoseState origin;
    Vector3State cell_size;
    uint32_t column_count = 0;
    uint32_t row_count = 0;
    uint32_t slice_count = 0;
    uint32_t cell_stride = 0;
    uint32_t row_stride = 0;
    uint32_t slice_stride = 0;
    std::vector<PointFieldState> fields;
    Blob data;
  };

  struct GridMapState {
    int64_t timestamp_ns = 0;
    std::string frame_id;
    PoseState origin;
    Vector2State cell_size;
    uint32_t column_count = 0;
    uint32_t row_count = 0;
    uint32_t cell_stride = 0;
    uint32_t row_stride = 0;
    std::vector<PointFieldState> fields;
    Blob data;
  };

  struct SpliceState {
    bool has_value = false;
    InputSpanRef reference;
  };

  enum class DataSource : uint8_t {
    kNone,
    kCopied,
    kSpliced,
  };

  [[nodiscard]] Status select(Kind kind) {
    if (kind_ != Kind::kNone && kind_ != kind) {
      return fail("ObjectWriter may emit only one object per parse call");
    }
    kind_ = kind;
    return status_;
  }

  [[nodiscard]] Status fail(std::string_view message) {
    if (status_.isOk()) {
      status_ = Status::error(message);
    }
    return status_;
  }

  [[nodiscard]] Status setString(std::string& destination, std::string_view value) {
    if (!status_.isOk()) {
      return status_;
    }
    PJ_PARSER_MODULE_TRY {
      destination.assign(value.data(), value.size());
      return Status::ok();
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return fail("ObjectWriter string allocation failed");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return fail("ObjectWriter string assignment failed");
    }
  }

  [[nodiscard]] Status setData(Blob& destination, PayloadView value) {
    if (data_source_ != DataSource::kNone) {
      return fail("pj.parser.contract_violation: object bulk data source was selected more than once");
    }
    auto allocated = allocateBlob(value.size);
    if (!allocated) {
      return fail(allocated.status().message());
    }
    if (value.size != 0) {
      if (value.data == nullptr) {
        return fail("ObjectWriter data source is null");
      }
      std::memcpy(allocated->data(), value.data, value.size);
    }
    destination = std::move(*allocated);
    data_source_ = DataSource::kCopied;
    return Status::ok();
  }

  [[nodiscard]] Status setDataFromInput(InputSpanRef reference) {
    if (data_source_ != DataSource::kNone) {
      return fail("pj.parser.contract_violation: object bulk data source was selected more than once");
    }
    if (reference.offset > input_payload_.size || reference.length > input_payload_.size - reference.offset) {
      return fail("pj.parser.contract_violation: object splice range is outside the parse payload");
    }
    if (input_payload_.data == nullptr && input_payload_.size != 0) {
      return fail("pj.parser.contract_violation: object splice payload storage is null");
    }
    splice_.has_value = true;
    splice_.reference = reference;
    data_source_ = DataSource::kSpliced;
    return Status::ok();
  }

  [[nodiscard]] static Status appendLittle(Blob& output, uint64_t value, size_t width) {
    for (size_t index = 0; index < width; ++index) {
      Status status = output.push(static_cast<uint8_t>(value >> (index * 8U)));
      if (!status.isOk()) {
        return status;
      }
    }
    return Status::ok();
  }

  [[nodiscard]] static Status writeTimestamp(WireWriter& writer, int64_t timestamp_ns) {
    constexpr int64_t kNanosPerSecond = INT64_C(1000000000);
    int64_t seconds = timestamp_ns / kNanosPerSecond;
    int32_t nanos = static_cast<int32_t>(timestamp_ns % kNanosPerSecond);
    if (nanos < 0) {
      --seconds;
      nanos += static_cast<int32_t>(kNanosPerSecond);
    }
    WireWriter nested;
    Status status = nested.varintField(1, static_cast<uint64_t>(seconds));
    if (status.isOk()) {
      status = nested.varintField(2, static_cast<uint32_t>(nanos));
    }
    return status.isOk() ? writer.messageField(1, nested) : status;
  }

  [[nodiscard]] static Status writeVector2(WireWriter& writer, uint32_t field, const Vector2State& value) {
    WireWriter nested;
    Status status = nested.doubleField(1, value.x);
    if (status.isOk()) {
      status = nested.doubleField(2, value.y);
    }
    return status.isOk() ? writer.messageField(field, nested) : status;
  }

  [[nodiscard]] static Status writeVector3(WireWriter& writer, uint32_t field, const Vector3State& value) {
    WireWriter nested;
    Status status = nested.doubleField(1, value.x);
    if (status.isOk()) {
      status = nested.doubleField(2, value.y);
    }
    if (status.isOk()) {
      status = nested.doubleField(3, value.z);
    }
    return status.isOk() ? writer.messageField(field, nested) : status;
  }

  [[nodiscard]] static Status writePose(WireWriter& writer, uint32_t field, const PoseState& value) {
    WireWriter nested;
    Status status = writeVector3(nested, 1, value.position);
    if (status.isOk()) {
      WireWriter quaternion;
      status = quaternion.doubleField(1, value.orientation.x);
      if (status.isOk()) {
        status = quaternion.doubleField(2, value.orientation.y);
      }
      if (status.isOk()) {
        status = quaternion.doubleField(3, value.orientation.z);
      }
      if (status.isOk()) {
        status = quaternion.doubleField(4, value.orientation.w);
      }
      if (status.isOk()) {
        status = nested.messageField(2, quaternion);
      }
    }
    return status.isOk() ? writer.messageField(field, nested) : status;
  }

  [[nodiscard]] static Status writeColor(WireWriter& writer, uint32_t field, const ColorState& value) {
    WireWriter nested;
    Status status = nested.doubleField(1, value.r);
    if (status.isOk()) {
      status = nested.doubleField(2, value.g);
    }
    if (status.isOk()) {
      status = nested.doubleField(3, value.b);
    }
    if (status.isOk()) {
      status = nested.doubleField(4, value.a);
    }
    return status.isOk() ? writer.messageField(field, nested) : status;
  }

  [[nodiscard]] static Status writePointField(WireWriter& writer, uint32_t field_number, const PointFieldState& field) {
    WireWriter nested;
    Status status = nested.stringField(1, field.name);
    if (status.isOk()) {
      status = nested.varintField(2, field.offset);
    }
    if (status.isOk()) {
      status = nested.varintField(3, static_cast<uint32_t>(field.datatype));
    }
    if (status.isOk()) {
      status = nested.varintField(4, field.count);
    }
    return status.isOk() ? writer.messageField(field_number, nested) : status;
  }

  [[nodiscard]] static Status writePackedDoubles(
      WireWriter& writer, uint32_t field, const double* values, size_t count) {
    WireWriter packed;
    Status status;
    for (size_t index = 0; status.isOk() && index < count; ++index) {
      uint64_t bits = 0;
      std::memcpy(&bits, values + index, sizeof(bits));
      status = packed.rawFixed64(bits);
    }
    return status.isOk() ? writer.lengthDelimited(field, packed.view()) : status;
  }

  [[nodiscard]] Expected<Blob> writeSelected() {
    switch (kind_) {
      case Kind::kPointCloud:
        return writePointCloud();
      case Kind::kImage:
        return writeImage();
      case Kind::kDepthImage:
        return writeDepthImage();
      case Kind::kOccupancyGrid:
        return writeOccupancyGrid();
      case Kind::kCompressedPointCloud:
        return writeCompressedPointCloud();
      case Kind::kMesh3D:
        return writeMesh3D();
      case Kind::kVideoFrame:
        return writeVideoFrame();
      case Kind::kOccupancyGridUpdate:
        return writeOccupancyGridUpdate();
      case Kind::kVoxelGrid:
        return writeVoxelGrid();
      case Kind::kGridMap:
        return writeGridMap();
      case Kind::kNone:
        break;
    }
    return Status::error("ObjectWriter has no selected object type");
  }

  [[nodiscard]] uint16_t selectedObjectType() const noexcept {
    switch (kind_) {
      case Kind::kImage:
        return kImageObjectType;
      case Kind::kPointCloud:
        return kPointCloudObjectType;
      case Kind::kDepthImage:
        return kDepthImageObjectType;
      case Kind::kOccupancyGrid:
        return kOccupancyGridObjectType;
      case Kind::kCompressedPointCloud:
        return kCompressedPointCloudObjectType;
      case Kind::kMesh3D:
        return kMesh3DObjectType;
      case Kind::kVideoFrame:
        return kVideoFrameObjectType;
      case Kind::kOccupancyGridUpdate:
        return kOccupancyGridUpdateObjectType;
      case Kind::kVoxelGrid:
        return kVoxelGridObjectType;
      case Kind::kGridMap:
        return kGridMapObjectType;
      case Kind::kNone:
        return 0;
    }
    return 0;
  }

  [[nodiscard]] uint32_t selectedSpliceField() const noexcept {
    switch (kind_) {
      case Kind::kImage:
        return kImageDataField;
      case Kind::kPointCloud:
        return kPointCloudDataField;
      case Kind::kDepthImage:
        return kDepthImageDataField;
      case Kind::kOccupancyGrid:
        return kOccupancyGridDataField;
      case Kind::kCompressedPointCloud:
        return kCompressedPointCloudDataField;
      case Kind::kMesh3D:
        return kMesh3DDataField;
      case Kind::kVideoFrame:
        return kVideoFrameDataField;
      case Kind::kOccupancyGridUpdate:
        return kOccupancyGridUpdateDataField;
      case Kind::kVoxelGrid:
        return kVoxelGridDataField;
      case Kind::kGridMap:
        return kGridMapDataField;
      case Kind::kNone:
        return 0;
    }
    return 0;
  }

  [[nodiscard]] Expected<Blob> writePointCloud() {
    WireWriter writer;
    Status status = writeTimestamp(writer, point_cloud_.timestamp_ns);
    if (status.isOk()) {
      status = writer.varintField(2, point_cloud_.width);
    }
    if (status.isOk()) {
      status = writer.varintField(3, point_cloud_.height);
    }
    if (status.isOk()) {
      status = writer.varintField(4, point_cloud_.point_step);
    }
    if (status.isOk()) {
      status = writer.varintField(5, point_cloud_.row_step);
    }
    if (status.isOk()) {
      status = writer.varintField(6, point_cloud_.is_bigendian ? 1 : 0);
    }
    if (status.isOk()) {
      status = writer.varintField(7, point_cloud_.is_dense ? 1 : 0);
    }
    for (const auto& field : point_cloud_.fields) {
      if (!status.isOk()) {
        break;
      }
      WireWriter nested;
      status = nested.stringField(1, field.name);
      if (status.isOk()) {
        status = nested.varintField(2, field.offset);
      }
      if (status.isOk()) {
        status = nested.varintField(3, static_cast<uint32_t>(field.datatype));
      }
      if (status.isOk()) {
        status = nested.varintField(4, field.count);
      }
      if (status.isOk()) {
        status = writer.messageField(8, nested);
      }
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(9, point_cloud_.data.view());
    }
    if (status.isOk()) {
      status = writer.stringField(10, point_cloud_.frame_id);
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeImage() {
    WireWriter writer;
    Status status = writeTimestamp(writer, image_.timestamp_ns);
    if (status.isOk()) {
      status = writer.varintField(2, image_.width);
    }
    if (status.isOk()) {
      status = writer.varintField(3, image_.height);
    }
    if (status.isOk()) {
      status = writer.stringField(4, image_.encoding);
    }
    if (status.isOk()) {
      status = writer.varintField(5, image_.row_step);
    }
    if (status.isOk()) {
      status = writer.varintField(6, image_.is_bigendian ? 1 : 0);
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(7, image_.data.view());
    }
    if (status.isOk() && image_.has_compressed_depth_min) {
      uint32_t bits = 0;
      std::memcpy(&bits, &image_.compressed_depth_min, sizeof(bits));
      status = writer.fixed32Field(8, bits);
    }
    if (status.isOk() && image_.has_compressed_depth_max) {
      uint32_t bits = 0;
      std::memcpy(&bits, &image_.compressed_depth_max, sizeof(bits));
      status = writer.fixed32Field(9, bits);
    }
    if (status.isOk()) {
      status = writer.stringField(10, image_.frame_id);
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeDepthImage() {
    WireWriter writer;
    Status status = writeTimestamp(writer, depth_image_.timestamp_ns);
    if (status.isOk()) {
      status = writer.varintField(2, depth_image_.width);
    }
    if (status.isOk()) {
      status = writer.varintField(3, depth_image_.height);
    }
    if (status.isOk()) {
      status = writer.stringField(4, depth_image_.encoding);
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(5, depth_image_.data.view());
    }
    if (status.isOk()) {
      status = writePackedDoubles(writer, 6, depth_image_.intrinsics.data(), depth_image_.intrinsics.size());
    }
    if (status.isOk()) {
      status = writer.stringField(7, depth_image_.distortion_model);
    }
    if (status.isOk()) {
      status = writePackedDoubles(writer, 8, depth_image_.distortion.data(), depth_image_.distortion.size());
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeOccupancyGrid() {
    WireWriter writer;
    Status status = writeTimestamp(writer, occupancy_grid_.timestamp_ns);
    if (status.isOk()) {
      status = writer.stringField(2, occupancy_grid_.frame_id);
    }
    if (status.isOk()) {
      status = writePose(writer, 3, occupancy_grid_.origin);
    }
    if (status.isOk()) {
      status = writer.doubleField(4, occupancy_grid_.resolution);
    }
    if (status.isOk()) {
      status = writer.varintField(5, occupancy_grid_.width);
    }
    if (status.isOk()) {
      status = writer.varintField(6, occupancy_grid_.height);
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(7, occupancy_grid_.data.view());
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeCompressedPointCloud() {
    WireWriter writer;
    Status status = writeTimestamp(writer, compressed_point_cloud_.timestamp_ns);
    if (status.isOk()) {
      status = writer.stringField(2, compressed_point_cloud_.frame_id);
    }
    if (status.isOk()) {
      status = writer.stringField(3, compressed_point_cloud_.format);
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(4, compressed_point_cloud_.data.view());
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeMesh3D() {
    WireWriter writer;
    Status status = writeTimestamp(writer, mesh3d_.timestamp_ns);
    if (status.isOk()) {
      status = writer.stringField(2, mesh3d_.frame_id);
    }
    if (status.isOk()) {
      status = writer.stringField(3, mesh3d_.id);
    }
    if (status.isOk()) {
      status = writePose(writer, 4, mesh3d_.pose);
    }
    if (status.isOk()) {
      status = writeVector3(writer, 5, mesh3d_.scale);
    }
    if (status.isOk()) {
      status = writer.stringField(6, mesh3d_.format);
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(7, mesh3d_.data.view());
    }
    if (status.isOk()) {
      status = writer.stringField(8, mesh3d_.url);
    }
    if (status.isOk()) {
      status = writeColor(writer, 9, mesh3d_.color);
    }
    if (status.isOk()) {
      status = writer.varintField(10, mesh3d_.override_color ? 1 : 0);
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeVideoFrame() {
    WireWriter writer;
    Status status = writeTimestamp(writer, video_frame_.timestamp_ns);
    if (status.isOk()) {
      status = writer.stringField(2, video_frame_.frame_id);
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(3, video_frame_.data.view());
    }
    if (status.isOk()) {
      status = writer.stringField(4, video_frame_.format);
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeOccupancyGridUpdate() {
    WireWriter writer;
    Status status = writeTimestamp(writer, occupancy_grid_update_.timestamp_ns);
    if (status.isOk()) {
      status = writer.stringField(2, occupancy_grid_update_.frame_id);
    }
    if (status.isOk()) {
      status = writer.varintField(3, static_cast<uint32_t>(occupancy_grid_update_.x));
    }
    if (status.isOk()) {
      status = writer.varintField(4, static_cast<uint32_t>(occupancy_grid_update_.y));
    }
    if (status.isOk()) {
      status = writer.varintField(5, occupancy_grid_update_.width);
    }
    if (status.isOk()) {
      status = writer.varintField(6, occupancy_grid_update_.height);
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(7, occupancy_grid_update_.data.view());
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeGridMap() {
    WireWriter writer;
    Status status = writeTimestamp(writer, grid_map_.timestamp_ns);
    if (status.isOk()) {
      status = writer.stringField(2, grid_map_.frame_id);
    }
    if (status.isOk()) {
      status = writePose(writer, 3, grid_map_.origin);
    }
    if (status.isOk()) {
      status = writeVector2(writer, 4, grid_map_.cell_size);
    }
    if (status.isOk()) {
      status = writer.varintField(5, grid_map_.column_count);
    }
    if (status.isOk()) {
      status = writer.varintField(6, grid_map_.row_count);
    }
    if (status.isOk()) {
      status = writer.varintField(7, grid_map_.cell_stride);
    }
    if (status.isOk()) {
      status = writer.varintField(8, grid_map_.row_stride);
    }
    for (const auto& field : grid_map_.fields) {
      if (status.isOk()) {
        status = writePointField(writer, 9, field);
      }
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(10, grid_map_.data.view());
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  [[nodiscard]] Expected<Blob> writeVoxelGrid() {
    WireWriter writer;
    Status status = writeTimestamp(writer, voxel_grid_.timestamp_ns);
    if (status.isOk()) {
      status = writer.stringField(2, voxel_grid_.frame_id);
    }
    if (status.isOk()) {
      status = writePose(writer, 3, voxel_grid_.origin);
    }
    if (status.isOk()) {
      status = writeVector3(writer, 4, voxel_grid_.cell_size);
    }
    if (status.isOk()) {
      status = writer.varintField(5, voxel_grid_.column_count);
    }
    if (status.isOk()) {
      status = writer.varintField(6, voxel_grid_.row_count);
    }
    if (status.isOk()) {
      status = writer.varintField(7, voxel_grid_.slice_count);
    }
    if (status.isOk()) {
      status = writer.varintField(8, voxel_grid_.cell_stride);
    }
    if (status.isOk()) {
      status = writer.varintField(9, voxel_grid_.row_stride);
    }
    if (status.isOk()) {
      status = writer.varintField(10, voxel_grid_.slice_stride);
    }
    for (const auto& field : voxel_grid_.fields) {
      if (status.isOk()) {
        status = writePointField(writer, 11, field);
      }
    }
    if (status.isOk() && !splice_.has_value) {
      status = writer.lengthDelimited(12, voxel_grid_.data.view());
    }
    return status.isOk() ? Expected<Blob>(writer.take()) : Expected<Blob>(status);
  }

  Kind kind_ = Kind::kNone;
  Status status_;
  PayloadView input_payload_;
  DataSource data_source_ = DataSource::kNone;
  PointCloudState point_cloud_;
  ImageState image_;
  DepthImageState depth_image_;
  OccupancyGridState occupancy_grid_;
  CompressedPointCloudState compressed_point_cloud_;
  Mesh3DState mesh3d_;
  VideoFrameState video_frame_;
  OccupancyGridUpdateState occupancy_grid_update_;
  VoxelGridState voxel_grid_;
  GridMapState grid_map_;
  SpliceState splice_;

  friend class PointCloudBuilder;
  friend class ImageBuilder;
  friend class DepthImageBuilder;
  friend class OccupancyGridBuilder;
  friend class CompressedPointCloudBuilder;
  friend class Mesh3DBuilder;
  friend class VideoFrameBuilder;
  friend class OccupancyGridUpdateBuilder;
  friend class VoxelGridBuilder;
  friend class GridMapBuilder;
  friend class ScalarWriter;
};

class ObjectWriter::PointCloudBuilder {
 public:
  explicit PointCloudBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kPointCloud);
  }

  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->point_cloud_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setWidth(uint32_t value) {
    owner_->point_cloud_.width = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setHeight(uint32_t value) {
    owner_->point_cloud_.height = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setPointStep(uint32_t value) {
    owner_->point_cloud_.point_step = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setRowStep(uint32_t value) {
    owner_->point_cloud_.row_step = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setBigEndian(bool value) {
    owner_->point_cloud_.is_bigendian = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setDense(bool value) {
    owner_->point_cloud_.is_dense = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->point_cloud_.frame_id, value);
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->point_cloud_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef reference) {
    return owner_->setDataFromInput(reference);
  }
  [[nodiscard]] Status addField(
      std::string_view name, uint32_t offset, PointFieldDatatype datatype, uint32_t count = 1) {
    if (!owner_->status_.isOk()) {
      return owner_->status_;
    }
    PJ_PARSER_MODULE_TRY {
      PointFieldState field;
      field.name.assign(name.data(), name.size());
      field.offset = offset;
      field.datatype = datatype;
      field.count = count;
      owner_->point_cloud_.fields.push_back(std::move(field));
      return Status::ok();
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return owner_->fail("ObjectWriter point-field allocation failed");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return owner_->fail("ObjectWriter point-field creation failed");
    }
  }

 private:
  ObjectWriter* owner_;
};

class ObjectWriter::ImageBuilder {
 public:
  explicit ImageBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kImage);
  }

  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->image_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setWidth(uint32_t value) {
    owner_->image_.width = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setHeight(uint32_t value) {
    owner_->image_.height = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setEncoding(std::string_view value) {
    return owner_->setString(owner_->image_.encoding, value);
  }
  [[nodiscard]] Status setRowStep(uint32_t value) {
    owner_->image_.row_step = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setBigEndian(bool value) {
    owner_->image_.is_bigendian = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->image_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef reference) {
    return owner_->setDataFromInput(reference);
  }
  [[nodiscard]] Status setCompressedDepthMin(float value) {
    owner_->image_.has_compressed_depth_min = true;
    owner_->image_.compressed_depth_min = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setCompressedDepthMax(float value) {
    owner_->image_.has_compressed_depth_max = true;
    owner_->image_.compressed_depth_max = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->image_.frame_id, value);
  }

 private:
  ObjectWriter* owner_;
};

class ObjectWriter::DepthImageBuilder {
 public:
  explicit DepthImageBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kDepthImage);
  }
  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->depth_image_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setWidth(uint32_t value) {
    owner_->depth_image_.width = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setHeight(uint32_t value) {
    owner_->depth_image_.height = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setEncoding(std::string_view value) {
    return owner_->setString(owner_->depth_image_.encoding, value);
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->depth_image_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef value) {
    return owner_->setDataFromInput(value);
  }
  [[nodiscard]] Status setIntrinsics(const std::array<double, 9>& value) {
    owner_->depth_image_.intrinsics = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setDistortionModel(std::string_view value) {
    return owner_->setString(owner_->depth_image_.distortion_model, value);
  }
  [[nodiscard]] Status addDistortionCoefficient(double value) {
    if (!owner_->status_.isOk()) {
      return owner_->status_;
    }
    PJ_PARSER_MODULE_TRY {
      owner_->depth_image_.distortion.push_back(value);
      return Status::ok();
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return owner_->fail("ObjectWriter distortion allocation failed");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return owner_->fail("ObjectWriter distortion creation failed");
    }
  }

 private:
  ObjectWriter* owner_;
};

class ObjectWriter::OccupancyGridBuilder {
 public:
  explicit OccupancyGridBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kOccupancyGrid);
  }
  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->occupancy_grid_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->occupancy_grid_.frame_id, value);
  }
  [[nodiscard]] Status setOrigin(
      double px, double py, double pz, double qx = 0, double qy = 0, double qz = 0, double qw = 1) {
    owner_->occupancy_grid_.origin = {{px, py, pz}, {qx, qy, qz, qw}};
    return owner_->status_;
  }
  [[nodiscard]] Status setResolution(double value) {
    owner_->occupancy_grid_.resolution = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setWidth(uint32_t value) {
    owner_->occupancy_grid_.width = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setHeight(uint32_t value) {
    owner_->occupancy_grid_.height = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->occupancy_grid_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef value) {
    return owner_->setDataFromInput(value);
  }

 private:
  ObjectWriter* owner_;
};

class ObjectWriter::CompressedPointCloudBuilder {
 public:
  explicit CompressedPointCloudBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kCompressedPointCloud);
  }
  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->compressed_point_cloud_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->compressed_point_cloud_.frame_id, value);
  }
  [[nodiscard]] Status setFormat(std::string_view value) {
    return owner_->setString(owner_->compressed_point_cloud_.format, value);
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->compressed_point_cloud_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef value) {
    return owner_->setDataFromInput(value);
  }

 private:
  ObjectWriter* owner_;
};

class ObjectWriter::Mesh3DBuilder {
 public:
  explicit Mesh3DBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kMesh3D);
  }
  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->mesh3d_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->mesh3d_.frame_id, value);
  }
  [[nodiscard]] Status setId(std::string_view value) {
    return owner_->setString(owner_->mesh3d_.id, value);
  }
  [[nodiscard]] Status setPose(
      double px, double py, double pz, double qx = 0, double qy = 0, double qz = 0, double qw = 1) {
    owner_->mesh3d_.pose = {{px, py, pz}, {qx, qy, qz, qw}};
    return owner_->status_;
  }
  [[nodiscard]] Status setScale(double x, double y, double z) {
    owner_->mesh3d_.scale = {x, y, z};
    return owner_->status_;
  }
  [[nodiscard]] Status setFormat(std::string_view value) {
    return owner_->setString(owner_->mesh3d_.format, value);
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->mesh3d_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef value) {
    return owner_->setDataFromInput(value);
  }
  [[nodiscard]] Status setUrl(std::string_view value) {
    return owner_->setString(owner_->mesh3d_.url, value);
  }
  [[nodiscard]] Status setColor(double r, double g, double b, double a) {
    owner_->mesh3d_.color = {r, g, b, a};
    return owner_->status_;
  }
  [[nodiscard]] Status setOverrideColor(bool value) {
    owner_->mesh3d_.override_color = value;
    return owner_->status_;
  }

 private:
  ObjectWriter* owner_;
};

class ObjectWriter::VideoFrameBuilder {
 public:
  explicit VideoFrameBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kVideoFrame);
  }
  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->video_frame_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->video_frame_.frame_id, value);
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->video_frame_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef value) {
    return owner_->setDataFromInput(value);
  }
  [[nodiscard]] Status setFormat(std::string_view value) {
    return owner_->setString(owner_->video_frame_.format, value);
  }

 private:
  ObjectWriter* owner_;
};

class ObjectWriter::OccupancyGridUpdateBuilder {
 public:
  explicit OccupancyGridUpdateBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kOccupancyGridUpdate);
  }
  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->occupancy_grid_update_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->occupancy_grid_update_.frame_id, value);
  }
  [[nodiscard]] Status setX(int32_t value) {
    owner_->occupancy_grid_update_.x = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setY(int32_t value) {
    owner_->occupancy_grid_update_.y = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setWidth(uint32_t value) {
    owner_->occupancy_grid_update_.width = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setHeight(uint32_t value) {
    owner_->occupancy_grid_update_.height = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->occupancy_grid_update_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef value) {
    return owner_->setDataFromInput(value);
  }

 private:
  ObjectWriter* owner_;
};

class ObjectWriter::VoxelGridBuilder {
 public:
  explicit VoxelGridBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kVoxelGrid);
  }
  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->voxel_grid_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->voxel_grid_.frame_id, value);
  }
  [[nodiscard]] Status setOrigin(
      double px, double py, double pz, double qx = 0, double qy = 0, double qz = 0, double qw = 1) {
    owner_->voxel_grid_.origin = {{px, py, pz}, {qx, qy, qz, qw}};
    return owner_->status_;
  }
  [[nodiscard]] Status setCellSize(double x, double y, double z) {
    owner_->voxel_grid_.cell_size = {x, y, z};
    return owner_->status_;
  }
  [[nodiscard]] Status setColumnCount(uint32_t value) {
    owner_->voxel_grid_.column_count = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setRowCount(uint32_t value) {
    owner_->voxel_grid_.row_count = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setSliceCount(uint32_t value) {
    owner_->voxel_grid_.slice_count = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setCellStride(uint32_t value) {
    owner_->voxel_grid_.cell_stride = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setRowStride(uint32_t value) {
    owner_->voxel_grid_.row_stride = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setSliceStride(uint32_t value) {
    owner_->voxel_grid_.slice_stride = value;
    return owner_->status_;
  }
  [[nodiscard]] Status addField(
      std::string_view name, uint32_t offset, PointFieldDatatype datatype, uint32_t count = 1) {
    if (!owner_->status_.isOk()) {
      return owner_->status_;
    }
    PJ_PARSER_MODULE_TRY {
      PointFieldState field;
      field.name.assign(name.data(), name.size());
      field.offset = offset;
      field.datatype = datatype;
      field.count = count;
      owner_->voxel_grid_.fields.push_back(std::move(field));
      return Status::ok();
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return owner_->fail("ObjectWriter voxel-field allocation failed");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return owner_->fail("ObjectWriter voxel-field creation failed");
    }
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->voxel_grid_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef value) {
    return owner_->setDataFromInput(value);
  }

 private:
  ObjectWriter* owner_;
};

inline ObjectWriter::PointCloudBuilder ObjectWriter::pointCloud() {
  return PointCloudBuilder(*this);
}

inline ObjectWriter::ImageBuilder ObjectWriter::image() {
  return ImageBuilder(*this);
}

inline ObjectWriter::DepthImageBuilder ObjectWriter::depthImage() {
  return DepthImageBuilder(*this);
}

inline ObjectWriter::OccupancyGridBuilder ObjectWriter::occupancyGrid() {
  return OccupancyGridBuilder(*this);
}

inline ObjectWriter::CompressedPointCloudBuilder ObjectWriter::compressedPointCloud() {
  return CompressedPointCloudBuilder(*this);
}

inline ObjectWriter::Mesh3DBuilder ObjectWriter::mesh3D() {
  return Mesh3DBuilder(*this);
}

inline ObjectWriter::VideoFrameBuilder ObjectWriter::videoFrame() {
  return VideoFrameBuilder(*this);
}

inline ObjectWriter::OccupancyGridUpdateBuilder ObjectWriter::occupancyGridUpdate() {
  return OccupancyGridUpdateBuilder(*this);
}

inline ObjectWriter::VoxelGridBuilder ObjectWriter::voxelGrid() {
  return VoxelGridBuilder(*this);
}

class ObjectWriter::GridMapBuilder {
 public:
  explicit GridMapBuilder(ObjectWriter& owner) : owner_(&owner) {
    (void)owner_->select(Kind::kGridMap);
  }
  [[nodiscard]] Status setTimestamp(int64_t value) {
    owner_->grid_map_.timestamp_ns = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setFrameId(std::string_view value) {
    return owner_->setString(owner_->grid_map_.frame_id, value);
  }
  [[nodiscard]] Status setOrigin(
      double px, double py, double pz, double qx = 0, double qy = 0, double qz = 0, double qw = 1) {
    owner_->grid_map_.origin = {{px, py, pz}, {qx, qy, qz, qw}};
    return owner_->status_;
  }
  [[nodiscard]] Status setCellSize(double x, double y) {
    owner_->grid_map_.cell_size = {x, y};
    return owner_->status_;
  }
  [[nodiscard]] Status setColumnCount(uint32_t value) {
    owner_->grid_map_.column_count = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setRowCount(uint32_t value) {
    owner_->grid_map_.row_count = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setCellStride(uint32_t value) {
    owner_->grid_map_.cell_stride = value;
    return owner_->status_;
  }
  [[nodiscard]] Status setRowStride(uint32_t value) {
    owner_->grid_map_.row_stride = value;
    return owner_->status_;
  }
  [[nodiscard]] Status addField(
      std::string_view name, uint32_t offset, PointFieldDatatype datatype, uint32_t count = 1) {
    if (!owner_->status_.isOk()) {
      return owner_->status_;
    }
    PJ_PARSER_MODULE_TRY {
      PointFieldState field;
      field.name.assign(name.data(), name.size());
      field.offset = offset;
      field.datatype = datatype;
      field.count = count;
      owner_->grid_map_.fields.push_back(std::move(field));
      return Status::ok();
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return owner_->fail("ObjectWriter grid-map-field allocation failed");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return owner_->fail("ObjectWriter grid-map-field creation failed");
    }
  }
  [[nodiscard]] Status setData(PayloadView value) {
    return owner_->setData(owner_->grid_map_.data, value);
  }
  [[nodiscard]] Status setDataFromInput(InputSpanRef value) {
    return owner_->setDataFromInput(value);
  }

 private:
  ObjectWriter* owner_;
};

inline ObjectWriter::GridMapBuilder ObjectWriter::gridMap() {
  return GridMapBuilder(*this);
}

class ScalarWriter {
 public:
  [[nodiscard]] Status setTimestamp(int64_t timestamp_ns) {
    has_timestamp_ = true;
    timestamp_ns_ = timestamp_ns;
    return status_;
  }

  [[nodiscard]] Status add(std::string_view name, double value) {
    return addValue(name, value);
  }
  [[nodiscard]] Status add(std::string_view name, int64_t value) {
    return addValue(name, value);
  }
  [[nodiscard]] Status add(std::string_view name, uint64_t value) {
    return addValue(name, value);
  }
  [[nodiscard]] Status add(std::string_view name, bool value) {
    return addValue(name, value);
  }
  [[nodiscard]] Status add(std::string_view name, const char* value) {
    return add(name, std::string_view(value == nullptr ? "" : value));
  }
  [[nodiscard]] Status add(std::string_view name, std::string_view value) {
    PJ_PARSER_MODULE_TRY {
      return addValue(name, std::string(value));
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return fail("ScalarWriter string allocation failed");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return fail("ScalarWriter string creation failed");
    }
  }

  [[nodiscard]] Expected<Blob> finish() {
    if (!status_.isOk()) {
      return status_;
    }
    size_t names_offset = 24;
    for (const auto& field : fields_) {
      size_t value_size = 8;
      if (std::holds_alternative<bool>(field.value)) {
        value_size = 1;
      } else if (const auto* string = std::get_if<std::string>(&field.value)) {
        if (string->size() > std::numeric_limits<uint32_t>::max()) {
          return Status::error("scalar string exceeds the descriptor length range");
        }
        value_size = 4 + string->size();
      }
      if (names_offset > std::numeric_limits<size_t>::max() - 9 - value_size) {
        return Status::error("scalar output descriptor size overflow");
      }
      names_offset += 9 + value_size;
    }
    size_t total_size = names_offset;
    for (const auto& field : fields_) {
      if (field.name.size() > std::numeric_limits<uint32_t>::max() ||
          field.name.size() > std::numeric_limits<size_t>::max() - total_size) {
        return Status::error("scalar field names exceed the descriptor offset range");
      }
      total_size += field.name.size();
    }
    if (total_size > std::numeric_limits<uint32_t>::max() || fields_.size() > std::numeric_limits<uint32_t>::max()) {
      return Status::error("scalar output descriptor exceeds the v1 offset range");
    }

    Blob output;
    Status status = output.reserve(total_size);
    if (status.isOk()) {
      status = ObjectWriter::appendLittle(output, 1, 2);
    }
    if (status.isOk()) {
      status = output.push(1);
    }
    if (status.isOk()) {
      status = output.push(0);
    }
    if (status.isOk()) {
      status = output.push(has_timestamp_ ? 1 : 0);
    }
    for (size_t index = 0; status.isOk() && index < 7; ++index) {
      status = output.push(0);
    }
    if (status.isOk()) {
      status = ObjectWriter::appendLittle(output, static_cast<uint64_t>(timestamp_ns_), 8);
    }
    if (status.isOk()) {
      status = ObjectWriter::appendLittle(output, fields_.size(), 4);
    }
    size_t current_name = names_offset;
    for (const auto& field : fields_) {
      if (!status.isOk()) {
        break;
      }
      status = ObjectWriter::appendLittle(output, current_name, 4);
      if (status.isOk()) {
        status = ObjectWriter::appendLittle(output, field.name.size(), 4);
      }
      if (const auto* floating_value = std::get_if<double>(&field.value)) {
        status = status.isOk() ? output.push(0) : status;
        uint64_t bits = 0;
        std::memcpy(&bits, floating_value, sizeof(bits));
        status = status.isOk() ? ObjectWriter::appendLittle(output, bits, 8) : status;
      } else if (const auto* signed_value = std::get_if<int64_t>(&field.value)) {
        status = status.isOk() ? output.push(1) : status;
        status = status.isOk() ? ObjectWriter::appendLittle(output, static_cast<uint64_t>(*signed_value), 8) : status;
      } else if (const auto* unsigned_value = std::get_if<uint64_t>(&field.value)) {
        status = status.isOk() ? output.push(2) : status;
        status = status.isOk() ? ObjectWriter::appendLittle(output, *unsigned_value, 8) : status;
      } else if (const auto* bool_value = std::get_if<bool>(&field.value)) {
        status = status.isOk() ? output.push(3) : status;
        status = status.isOk() ? output.push(*bool_value ? 1 : 0) : status;
      } else {
        const auto& string_value = std::get<std::string>(field.value);
        status = status.isOk() ? output.push(4) : status;
        status = status.isOk() ? ObjectWriter::appendLittle(output, string_value.size(), 4) : status;
        status =
            status.isOk()
                ? output.append(ByteView(reinterpret_cast<const uint8_t*>(string_value.data()), string_value.size()))
                : status;
      }
      current_name += field.name.size();
    }
    for (const auto& field : fields_) {
      status = status.isOk()
                   ? output.append(ByteView(reinterpret_cast<const uint8_t*>(field.name.data()), field.name.size()))
                   : status;
    }
    return status.isOk() ? Expected<Blob>(std::move(output)) : Expected<Blob>(status);
  }

 private:
  using Value = std::variant<double, int64_t, uint64_t, bool, std::string>;
  struct Field {
    std::string name;
    Value value;
  };

  template <typename ValueType>
  [[nodiscard]] Status addValue(std::string_view name, ValueType value) {
    if (!status_.isOk()) {
      return status_;
    }
    PJ_PARSER_MODULE_TRY {
      Field field;
      field.name.assign(name.data(), name.size());
      field.value = std::move(value);
      fields_.push_back(std::move(field));
      return Status::ok();
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return fail("ScalarWriter allocation failed");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return fail("ScalarWriter field creation failed");
    }
  }

  [[nodiscard]] Status fail(std::string_view message) {
    if (status_.isOk()) {
      status_ = Status::error(message);
    }
    return status_;
  }

  bool has_timestamp_ = false;
  int64_t timestamp_ns_ = 0;
  std::vector<Field> fields_;
  Status status_;

  friend class ObjectWriter;
};

}  // namespace pj
