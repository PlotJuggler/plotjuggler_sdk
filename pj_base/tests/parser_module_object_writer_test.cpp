// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "pj_base/builtin/builtin_object_codec.hpp"
#include "pj_base/builtin/compressed_point_cloud.hpp"
#include "pj_base/builtin/depth_image.hpp"
#include "pj_base/builtin/grid_map.hpp"
#include "pj_base/builtin/image.hpp"
#include "pj_base/builtin/mesh3d.hpp"
#include "pj_base/builtin/occupancy_grid.hpp"
#include "pj_base/builtin/occupancy_grid_update.hpp"
#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/builtin/video_frame.hpp"
#include "pj_base/builtin/voxel_grid.hpp"
#include "pj_base/parser_module/module.hpp"
#include "pj_base/parser_module_abi.h"

namespace {

class TokenAndTimestampParser final : public pj::FunctionalParser {
 public:
  pj::Status bind(const pj::BindingInfo&) override {
    return pj::Status::ok();
  }

  pj::Status parseScalars(pj::PayloadView, pj::Timestamp timestamp, pj::ScalarWriter& output) override {
    saw_timestamp = timestamp.has_value;
    timestamp_ns = timestamp.nanoseconds;
    return output.add("value", uint64_t{1});
  }

  static inline bool saw_timestamp = false;
  static inline int64_t timestamp_ns = 0;
};

class FinishFailureParser final : public pj::FunctionalParser {
 public:
  pj::Status bind(const pj::BindingInfo&) override {
    return pj::Status::ok();
  }

  pj::Status parseObject(pj::PayloadView, pj::Timestamp, pj::ObjectWriter&) override {
    return pj::Status::ok();
  }
};

PJ::Span<const uint8_t> hostBytes(std::string_view value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}

std::string_view kitText(pj::ByteView value) {
  return {reinterpret_cast<const char*>(value.data), value.size};
}

TEST(ParserModuleAuthoringCodec, KitDecodesHostBindingAndParseFramingByteForByte) {
  const std::array<uint8_t, 3> schema{0x00, 0x7F, 0xFF};
  const PJ::parser_module::BindingInfoV1 binding{
      .route = PJ::parser_module::Route::kObject,
      .claim_index = UINT32_C(0x01020304),
      .expected_object_type = static_cast<uint16_t>(PJ::sdk::BuiltinObjectType::kPointCloud),
      .encoding = hostBytes("cdr"),
      .type_name = hostBytes("example/Type"),
      .schema = schema,
      .claim_id = hostBytes("claim"),
      .config_json = hostBytes("{}"),
      .schema_digest = hostBytes("digest"),
  };
  auto encoded_binding = PJ::parser_module::writeBindingInfoV1(binding);
  ASSERT_TRUE(encoded_binding.has_value()) << encoded_binding.error();
  auto decoded_binding = pj::readBindingInfo({encoded_binding->data(), encoded_binding->size()});
  ASSERT_TRUE(decoded_binding.hasValue()) << decoded_binding.status().message();
  EXPECT_EQ(decoded_binding->route(), pj::Route::kObject);
  EXPECT_EQ(decoded_binding->claimIndex(), UINT32_C(0x01020304));
  EXPECT_EQ(decoded_binding->expectedObjectType(), static_cast<uint16_t>(PJ::sdk::BuiltinObjectType::kPointCloud));
  EXPECT_EQ(kitText(decoded_binding->encoding()), "cdr");
  EXPECT_EQ(kitText(decoded_binding->typeName()), "example/Type");
  EXPECT_EQ(decoded_binding->schema().size, schema.size());
  EXPECT_EQ(kitText(decoded_binding->claimId()), "claim");
  EXPECT_EQ(kitText(decoded_binding->configJson()), "{}");
  EXPECT_EQ(kitText(decoded_binding->schemaDigest()), "digest");
  auto owned_binding = decoded_binding->owningCopy();
  ASSERT_TRUE(owned_binding.hasValue()) << owned_binding.status().message();
  encoded_binding->assign(encoded_binding->size(), 0);
  EXPECT_EQ(kitText(owned_binding->encoding()), "cdr");
  EXPECT_EQ(kitText(owned_binding->schema()), std::string_view("\0\x7f\xff", 3));

  const std::array<uint8_t, 3> payload{0xAA, 0x00, 0xFF};
  auto encoded_input =
      PJ::parser_module::writeParseInputV1({.has_timestamp = true, .timestamp_ns = -2, .payload = payload});
  ASSERT_TRUE(encoded_input.has_value()) << encoded_input.error();
  auto decoded_input = pj::readParseInput({encoded_input->data(), encoded_input->size()});
  ASSERT_TRUE(decoded_input.hasValue()) << decoded_input.status().message();
  EXPECT_TRUE(decoded_input->has_timestamp);
  EXPECT_EQ(decoded_input->timestamp_ns, -2);
  ASSERT_EQ(decoded_input->payload.size, payload.size());
  EXPECT_EQ(
      std::vector<uint8_t>(decoded_input->payload.data, decoded_input->payload.data + decoded_input->payload.size),
      std::vector<uint8_t>(payload.begin(), payload.end()));
}

TEST(ParserModuleExports, ThreadsPerMessageTimestampAndRejectsStaleGenerationalTokens) {
  using Exports = pj::detail::ModuleExports<TokenAndTimestampParser>;
  const uint64_t first = Exports::create(0);
  ASSERT_NE(first, 0U);
  Exports::destroy(first);
  const uint64_t second = Exports::create(0);
  ASSERT_NE(second, 0U);
  EXPECT_NE(first, second);
  EXPECT_EQ(Exports::bind(first, 0, 0), pj::kModuleBadToken);
  std::array<char, pj::kErrorBufferSize> error{};
  const uint64_t written =
      Exports::lastError(first, pj::detail::addressOf(error.data()), static_cast<uint64_t>(error.size()));
  EXPECT_NE(
      std::string_view(error.data(), static_cast<size_t>(written)).find("stale or unknown"), std::string_view::npos);

  const PJ::parser_module::BindingInfoV1 binding{
      .route = PJ::parser_module::Route::kScalar,
      .claim_index = 0,
      .expected_object_type = 0,
      .encoding = {},
      .type_name = {},
      .schema = {},
      .claim_id = {},
      .config_json = {},
      .schema_digest = {},
  };
  auto binding_bytes = PJ::parser_module::writeBindingInfoV1(binding);
  ASSERT_TRUE(binding_bytes.has_value()) << binding_bytes.error();
  ASSERT_EQ(Exports::bind(second, pj::detail::addressOf(binding_bytes->data()), binding_bytes->size()), pj::kModuleOk);
  const std::array<uint8_t, 1> payload{5};
  auto parse_bytes =
      PJ::parser_module::writeParseInputV1({.has_timestamp = true, .timestamp_ns = -123, .payload = payload});
  ASSERT_TRUE(parse_bytes.has_value()) << parse_bytes.error();
  uint64_t output_address = 0;
  uint64_t output_length = 0;
  ASSERT_EQ(
      Exports::parse(
          second, pj::detail::addressOf(parse_bytes->data()), parse_bytes->size(),
          pj::detail::addressOf(&output_address), pj::detail::addressOf(&output_length)),
      pj::kModuleOk);
  EXPECT_TRUE(TokenAndTimestampParser::saw_timestamp);
  EXPECT_EQ(TokenAndTimestampParser::timestamp_ns, -123);
  EXPECT_NE(output_address, 0U);
  EXPECT_NE(output_length, 0U);
  Exports::destroy(second);
}

TEST(ParserModuleExports, InstanceTableAllowsParsingWhileOtherTokensChange) {
  using Exports = pj::detail::ModuleExports<TokenAndTimestampParser>;
  const uint64_t parsing_token = Exports::create(0);
  ASSERT_NE(parsing_token, 0U);
  const PJ::parser_module::BindingInfoV1 binding{
      .route = PJ::parser_module::Route::kScalar,
      .claim_index = 0,
      .expected_object_type = 0,
      .encoding = {},
      .type_name = {},
      .schema = {},
      .claim_id = {},
      .config_json = {},
      .schema_digest = {},
  };
  auto binding_bytes = PJ::parser_module::writeBindingInfoV1(binding);
  ASSERT_TRUE(binding_bytes.has_value());
  ASSERT_EQ(
      Exports::bind(parsing_token, pj::detail::addressOf(binding_bytes->data()), binding_bytes->size()), pj::kModuleOk);
  const std::array<uint8_t, 1> payload{1};
  auto parse_bytes = PJ::parser_module::writeParseInputV1({.payload = payload});
  ASSERT_TRUE(parse_bytes.has_value());

  int32_t parse_result = pj::kModuleError;
  std::thread worker([&] {
    for (size_t index = 0; index < 200; ++index) {
      uint64_t output_address = 0;
      uint64_t output_length = 0;
      parse_result = Exports::parse(
          parsing_token, pj::detail::addressOf(parse_bytes->data()), parse_bytes->size(),
          pj::detail::addressOf(&output_address), pj::detail::addressOf(&output_length));
      if (parse_result != pj::kModuleOk) {
        return;
      }
    }
  });
  for (size_t index = 0; index < 200; ++index) {
    const uint64_t other = Exports::create(0);
    EXPECT_NE(other, 0U);
    if (other != 0) {
      Exports::destroy(other);
    }
  }
  worker.join();
  EXPECT_EQ(parse_result, pj::kModuleOk);
  Exports::destroy(parsing_token);
}

TEST(ParserModuleExports, WriterFinishFailureUsesGenericErrorCode) {
  using Exports = pj::detail::ModuleExports<FinishFailureParser>;
  const uint64_t token = Exports::create(0);
  ASSERT_NE(token, 0U);
  const PJ::parser_module::BindingInfoV1 binding{
      .route = PJ::parser_module::Route::kObject,
      .claim_index = 0,
      .expected_object_type = 3,
      .encoding = {},
      .type_name = {},
      .schema = {},
      .claim_id = {},
      .config_json = {},
      .schema_digest = {},
  };
  auto binding_bytes = PJ::parser_module::writeBindingInfoV1(binding);
  ASSERT_TRUE(binding_bytes.has_value());
  ASSERT_EQ(Exports::bind(token, pj::detail::addressOf(binding_bytes->data()), binding_bytes->size()), pj::kModuleOk);
  const std::array<uint8_t, 1> payload{1};
  auto parse_bytes = PJ::parser_module::writeParseInputV1({.payload = payload});
  ASSERT_TRUE(parse_bytes.has_value());
  uint64_t output_address = 0;
  uint64_t output_length = 0;
  EXPECT_EQ(
      Exports::parse(
          token, pj::detail::addressOf(parse_bytes->data()), parse_bytes->size(),
          pj::detail::addressOf(&output_address), pj::detail::addressOf(&output_length)),
      pj::kModuleError);
  Exports::destroy(token);
}

TEST(ParserModuleObjectWriter, PointCloudFullWireMatchesGoldenDescriptorAndHostCodec) {
  pj::ObjectWriter writer;
  auto cloud = writer.pointCloud();
  ASSERT_TRUE(cloud.setTimestamp(0).isOk());
  ASSERT_TRUE(cloud.setWidth(1).isOk());
  ASSERT_TRUE(cloud.setHeight(1).isOk());
  ASSERT_TRUE(cloud.setPointStep(1).isOk());
  ASSERT_TRUE(cloud.setRowStep(2).isOk());
  ASSERT_TRUE(cloud.setBigEndian(false).isOk());
  ASSERT_TRUE(cloud.setDense(true).isOk());
  const std::array<uint8_t, 2> data{0xAA, 0xBB};
  ASSERT_TRUE(cloud.setData({data.data(), data.size()}).isOk());
  ASSERT_TRUE(cloud.setFrameId("f").isOk());
  auto descriptor = writer.finish();
  ASSERT_TRUE(descriptor.hasValue()) << descriptor.status().message();

  const std::vector<uint8_t> expected{
      0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x0A, 0x04, 0x08, 0x00, 0x10, 0x00, 0x10, 0x01, 0x18, 0x01, 0x20, 0x01,
      0x28, 0x02, 0x30, 0x00, 0x38, 0x01, 0x4A, 0x02, 0xAA, 0xBB, 0x52, 0x01, 0x66,
  };
  EXPECT_EQ(std::vector<uint8_t>(descriptor->data(), descriptor->data() + descriptor->size()), expected);

  auto decoded = PJ::parser_module::readOutputDescriptorV1({descriptor->data(), descriptor->size()});
  ASSERT_TRUE(decoded.has_value()) << decoded.error();
  const auto* object = std::get_if<PJ::parser_module::ObjectOutputV1>(&*decoded);
  ASSERT_NE(object, nullptr);
  auto canonical =
      PJ::deserializeBuiltinObject(PJ::sdk::BuiltinObjectType::kPointCloud, object->wire.data(), object->wire.size());
  ASSERT_TRUE(canonical.has_value()) << canonical.error();
  const auto* result = std::any_cast<PJ::sdk::PointCloud>(&*canonical);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->data.size(), 2U);
  EXPECT_EQ(result->data[1], 0xBB);
  EXPECT_EQ(result->frame_id, "f");
}

TEST(ParserModuleObjectWriter, PointCloudAndImageSplicesUseFrozenEligibleFields) {
  const std::array<uint8_t, 32> payload{};
  pj::ObjectWriter cloud_writer({payload.data(), payload.size()});
  auto cloud = cloud_writer.pointCloud();
  ASSERT_TRUE(cloud.setWidth(2).isOk());
  ASSERT_TRUE(cloud.setDataFromInput({11, 8}).isOk());
  auto cloud_descriptor = cloud_writer.finish();
  ASSERT_TRUE(cloud_descriptor.hasValue());
  auto decoded_cloud = PJ::parser_module::readOutputDescriptorV1({cloud_descriptor->data(), cloud_descriptor->size()});
  ASSERT_TRUE(decoded_cloud.has_value()) << decoded_cloud.error();
  const auto& cloud_output = std::get<PJ::parser_module::ObjectOutputV1>(*decoded_cloud);
  ASSERT_TRUE(cloud_output.splice.has_value());
  EXPECT_EQ(cloud_output.splice->field_number, 9U);
  EXPECT_EQ(cloud_output.splice->input_offset, 11U);
  EXPECT_EQ(cloud_output.splice->input_length, 8U);

  pj::ObjectWriter image_writer({payload.data(), payload.size()});
  auto image = image_writer.image();
  ASSERT_TRUE(image.setWidth(4).isOk());
  ASSERT_TRUE(image.setHeight(3).isOk());
  ASSERT_TRUE(image.setEncoding("mono8").isOk());
  ASSERT_TRUE(image.setDataFromInput({5, 12}).isOk());
  auto image_descriptor = image_writer.finish();
  ASSERT_TRUE(image_descriptor.hasValue());
  auto decoded_image = PJ::parser_module::readOutputDescriptorV1({image_descriptor->data(), image_descriptor->size()});
  ASSERT_TRUE(decoded_image.has_value()) << decoded_image.error();
  const auto& image_output = std::get<PJ::parser_module::ObjectOutputV1>(*decoded_image);
  ASSERT_TRUE(image_output.splice.has_value());
  EXPECT_EQ(image_output.object_type, 1U);
  EXPECT_EQ(image_output.splice->field_number, 7U);
}

TEST(ParserModuleObjectWriter, ImageFullWireDecodesWithHostCodec) {
  pj::ObjectWriter writer;
  auto image = writer.image();
  ASSERT_TRUE(image.setTimestamp(-1).isOk());
  ASSERT_TRUE(image.setWidth(2).isOk());
  ASSERT_TRUE(image.setHeight(1).isOk());
  ASSERT_TRUE(image.setEncoding("mono8").isOk());
  ASSERT_TRUE(image.setRowStep(2).isOk());
  ASSERT_TRUE(image.setBigEndian(false).isOk());
  ASSERT_TRUE(image.setFrameId("camera").isOk());
  ASSERT_TRUE(image.setCompressedDepthMin(0.5F).isOk());
  ASSERT_TRUE(image.setCompressedDepthMax(3.5F).isOk());
  const std::array<uint8_t, 2> data{0x10, 0x20};
  ASSERT_TRUE(image.setData({data.data(), data.size()}).isOk());

  auto descriptor = writer.finish();
  ASSERT_TRUE(descriptor.hasValue()) << descriptor.status().message();
  auto decoded = PJ::parser_module::readOutputDescriptorV1({descriptor->data(), descriptor->size()});
  ASSERT_TRUE(decoded.has_value()) << decoded.error();
  const auto& object = std::get<PJ::parser_module::ObjectOutputV1>(*decoded);
  EXPECT_FALSE(object.splice.has_value());
  auto canonical =
      PJ::deserializeBuiltinObject(PJ::sdk::BuiltinObjectType::kImage, object.wire.data(), object.wire.size());
  ASSERT_TRUE(canonical.has_value()) << canonical.error();
  const auto* result = std::any_cast<PJ::sdk::Image>(&*canonical);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->timestamp_ns, -1);
  EXPECT_EQ(result->width, 2U);
  EXPECT_EQ(result->height, 1U);
  EXPECT_EQ(result->encoding, "mono8");
  EXPECT_EQ(result->data.size(), 2U);
  EXPECT_EQ(result->data[1], 0x20);
  EXPECT_EQ(result->compressed_depth_min, 0.5F);
  EXPECT_EQ(result->compressed_depth_max, 3.5F);
  EXPECT_EQ(result->frame_id, "camera");
}

TEST(ParserModuleObjectWriter, ScalarWriterUsesHostReadableDescriptor) {
  pj::ScalarWriter writer;
  ASSERT_TRUE(writer.setTimestamp(44).isOk());
  ASSERT_TRUE(writer.add("temperature", 21.5).isOk());
  ASSERT_TRUE(writer.add("ready", true).isOk());
  ASSERT_TRUE(writer.add("label", "ok").isOk());
  auto descriptor = writer.finish();
  ASSERT_TRUE(descriptor.hasValue()) << descriptor.status().message();
  auto decoded = PJ::parser_module::readOutputDescriptorV1({descriptor->data(), descriptor->size()});
  ASSERT_TRUE(decoded.has_value()) << decoded.error();
  const auto& scalar = std::get<PJ::parser_module::ScalarOutputV1>(*decoded);
  ASSERT_EQ(scalar.fields.size(), 3U);
  EXPECT_EQ(scalar.timestamp_ns, 44);
  EXPECT_EQ(scalar.fields[2].name, "label");
  EXPECT_EQ(std::get<std::string_view>(scalar.fields[2].value), "ok");
}

TEST(ParserModuleObjectWriter, AllAdditionalSpliceEligibleBuildersRoundTripFullWire) {
  const std::array<uint8_t, 3> data{7, 8, 9};

  pj::ObjectWriter depth_writer;
  auto depth = depth_writer.depthImage();
  ASSERT_TRUE(depth.setTimestamp(11).isOk());
  ASSERT_TRUE(depth.setWidth(2).isOk());
  ASSERT_TRUE(depth.setHeight(3).isOk());
  ASSERT_TRUE(depth.setEncoding("16UC1").isOk());
  ASSERT_TRUE(depth.setIntrinsics({1, 0, 2, 0, 3, 4, 0, 0, 1}).isOk());
  ASSERT_TRUE(depth.setDistortionModel("plumb_bob").isOk());
  ASSERT_TRUE(depth.addDistortionCoefficient(0.25).isOk());
  ASSERT_TRUE(depth.setData({data.data(), data.size()}).isOk());
  auto depth_descriptor = depth_writer.finish();
  ASSERT_TRUE(depth_descriptor.hasValue()) << depth_descriptor.status().message();
  auto depth_output = PJ::parser_module::readOutputDescriptorV1({depth_descriptor->data(), depth_descriptor->size()});
  ASSERT_TRUE(depth_output.has_value()) << depth_output.error();
  const auto& depth_wire = std::get<PJ::parser_module::ObjectOutputV1>(*depth_output).wire;
  auto decoded_depth =
      PJ::deserializeBuiltinObject(PJ::sdk::BuiltinObjectType::kDepthImage, depth_wire.data(), depth_wire.size());
  ASSERT_TRUE(decoded_depth.has_value()) << decoded_depth.error();
  const auto* depth_value = std::any_cast<PJ::sdk::DepthImage>(&*decoded_depth);
  ASSERT_NE(depth_value, nullptr);
  EXPECT_EQ(depth_value->data[2], 9U);
  EXPECT_EQ(depth_value->K[4], 3.0);
  ASSERT_EQ(depth_value->D.size(), 1U);

  pj::ObjectWriter grid_writer;
  auto grid = grid_writer.occupancyGrid();
  ASSERT_TRUE(grid.setTimestamp(12).isOk());
  ASSERT_TRUE(grid.setFrameId("map").isOk());
  ASSERT_TRUE(grid.setOrigin(1, 2, 3).isOk());
  ASSERT_TRUE(grid.setResolution(0.5).isOk());
  ASSERT_TRUE(grid.setWidth(3).isOk());
  ASSERT_TRUE(grid.setHeight(1).isOk());
  ASSERT_TRUE(grid.setData({data.data(), data.size()}).isOk());
  auto grid_descriptor = grid_writer.finish();
  ASSERT_TRUE(grid_descriptor.hasValue()) << grid_descriptor.status().message();
  auto grid_output = PJ::parser_module::readOutputDescriptorV1({grid_descriptor->data(), grid_descriptor->size()});
  ASSERT_TRUE(grid_output.has_value()) << grid_output.error();
  const auto& grid_wire = std::get<PJ::parser_module::ObjectOutputV1>(*grid_output).wire;
  auto decoded_grid =
      PJ::deserializeBuiltinObject(PJ::sdk::BuiltinObjectType::kOccupancyGrid, grid_wire.data(), grid_wire.size());
  ASSERT_TRUE(decoded_grid.has_value()) << decoded_grid.error();
  const auto* grid_value = std::any_cast<PJ::sdk::OccupancyGrid>(&*decoded_grid);
  ASSERT_NE(grid_value, nullptr);
  EXPECT_EQ(grid_value->frame_id, "map");
  EXPECT_EQ(grid_value->data.size(), 3U);

  pj::ObjectWriter compressed_writer;
  auto compressed = compressed_writer.compressedPointCloud();
  ASSERT_TRUE(compressed.setTimestamp(13).isOk());
  ASSERT_TRUE(compressed.setFrameId("lidar").isOk());
  ASSERT_TRUE(compressed.setFormat("draco").isOk());
  ASSERT_TRUE(compressed.setData({data.data(), data.size()}).isOk());
  auto compressed_descriptor = compressed_writer.finish();
  ASSERT_TRUE(compressed_descriptor.hasValue()) << compressed_descriptor.status().message();
  auto compressed_output =
      PJ::parser_module::readOutputDescriptorV1({compressed_descriptor->data(), compressed_descriptor->size()});
  ASSERT_TRUE(compressed_output.has_value()) << compressed_output.error();
  const auto& compressed_wire = std::get<PJ::parser_module::ObjectOutputV1>(*compressed_output).wire;
  auto decoded_compressed = PJ::deserializeBuiltinObject(
      PJ::sdk::BuiltinObjectType::kCompressedPointCloud, compressed_wire.data(), compressed_wire.size());
  ASSERT_TRUE(decoded_compressed.has_value()) << decoded_compressed.error();
  const auto* compressed_value = std::any_cast<PJ::sdk::CompressedPointCloud>(&*decoded_compressed);
  ASSERT_NE(compressed_value, nullptr);
  EXPECT_EQ(compressed_value->format, "draco");
  EXPECT_EQ(compressed_value->data[0], 7U);

  pj::ObjectWriter mesh_writer;
  auto mesh = mesh_writer.mesh3D();
  ASSERT_TRUE(mesh.setTimestamp(14).isOk());
  ASSERT_TRUE(mesh.setFrameId("world").isOk());
  ASSERT_TRUE(mesh.setId("mesh").isOk());
  ASSERT_TRUE(mesh.setPose(1, 2, 3).isOk());
  ASSERT_TRUE(mesh.setScale(2, 3, 4).isOk());
  ASSERT_TRUE(mesh.setFormat("glb").isOk());
  ASSERT_TRUE(mesh.setData({data.data(), data.size()}).isOk());
  ASSERT_TRUE(mesh.setColor(1, 0.5, 0, 1).isOk());
  ASSERT_TRUE(mesh.setOverrideColor(true).isOk());
  auto mesh_descriptor = mesh_writer.finish();
  ASSERT_TRUE(mesh_descriptor.hasValue()) << mesh_descriptor.status().message();
  auto mesh_output = PJ::parser_module::readOutputDescriptorV1({mesh_descriptor->data(), mesh_descriptor->size()});
  ASSERT_TRUE(mesh_output.has_value()) << mesh_output.error();
  const auto& mesh_wire = std::get<PJ::parser_module::ObjectOutputV1>(*mesh_output).wire;
  auto decoded_mesh =
      PJ::deserializeBuiltinObject(PJ::sdk::BuiltinObjectType::kMesh3D, mesh_wire.data(), mesh_wire.size());
  ASSERT_TRUE(decoded_mesh.has_value()) << decoded_mesh.error();
  const auto* mesh_value = std::any_cast<PJ::sdk::Mesh3D>(&*decoded_mesh);
  ASSERT_NE(mesh_value, nullptr);
  EXPECT_EQ(mesh_value->data.size(), 3U);
  EXPECT_EQ(mesh_value->scale.y, 3.0);
  EXPECT_TRUE(mesh_value->override_color);

  pj::ObjectWriter video_writer;
  auto video = video_writer.videoFrame();
  ASSERT_TRUE(video.setTimestamp(15).isOk());
  ASSERT_TRUE(video.setFrameId("camera").isOk());
  ASSERT_TRUE(video.setFormat("h264").isOk());
  ASSERT_TRUE(video.setData({data.data(), data.size()}).isOk());
  auto video_descriptor = video_writer.finish();
  ASSERT_TRUE(video_descriptor.hasValue()) << video_descriptor.status().message();
  auto video_output = PJ::parser_module::readOutputDescriptorV1({video_descriptor->data(), video_descriptor->size()});
  ASSERT_TRUE(video_output.has_value()) << video_output.error();
  const auto& video_wire = std::get<PJ::parser_module::ObjectOutputV1>(*video_output).wire;
  auto decoded_video =
      PJ::deserializeBuiltinObject(PJ::sdk::BuiltinObjectType::kVideoFrame, video_wire.data(), video_wire.size());
  ASSERT_TRUE(decoded_video.has_value()) << decoded_video.error();
  const auto* video_value = std::any_cast<PJ::sdk::VideoFrame>(&*decoded_video);
  ASSERT_NE(video_value, nullptr);
  EXPECT_EQ(video_value->format, "h264");
  EXPECT_EQ(video_value->data[1], 8U);

  pj::ObjectWriter update_writer;
  auto update = update_writer.occupancyGridUpdate();
  ASSERT_TRUE(update.setTimestamp(16).isOk());
  ASSERT_TRUE(update.setFrameId("map").isOk());
  ASSERT_TRUE(update.setX(-2).isOk());
  ASSERT_TRUE(update.setY(4).isOk());
  ASSERT_TRUE(update.setWidth(3).isOk());
  ASSERT_TRUE(update.setHeight(1).isOk());
  ASSERT_TRUE(update.setData({data.data(), data.size()}).isOk());
  auto update_descriptor = update_writer.finish();
  ASSERT_TRUE(update_descriptor.hasValue()) << update_descriptor.status().message();
  auto update_output =
      PJ::parser_module::readOutputDescriptorV1({update_descriptor->data(), update_descriptor->size()});
  ASSERT_TRUE(update_output.has_value()) << update_output.error();
  const auto& update_wire = std::get<PJ::parser_module::ObjectOutputV1>(*update_output).wire;
  auto decoded_update = PJ::deserializeBuiltinObject(
      PJ::sdk::BuiltinObjectType::kOccupancyGridUpdate, update_wire.data(), update_wire.size());
  ASSERT_TRUE(decoded_update.has_value()) << decoded_update.error();
  const auto* update_value = std::any_cast<PJ::sdk::OccupancyGridUpdate>(&*decoded_update);
  ASSERT_NE(update_value, nullptr);
  EXPECT_EQ(update_value->x, -2);
  EXPECT_EQ(update_value->data[2], 9U);

  pj::ObjectWriter voxel_writer;
  auto voxel = voxel_writer.voxelGrid();
  ASSERT_TRUE(voxel.setTimestamp(17).isOk());
  ASSERT_TRUE(voxel.setFrameId("map").isOk());
  ASSERT_TRUE(voxel.setOrigin(1, 2, 3).isOk());
  ASSERT_TRUE(voxel.setCellSize(0.1, 0.2, 0.3).isOk());
  ASSERT_TRUE(voxel.setColumnCount(3).isOk());
  ASSERT_TRUE(voxel.setRowCount(1).isOk());
  ASSERT_TRUE(voxel.setSliceCount(1).isOk());
  ASSERT_TRUE(voxel.setCellStride(1).isOk());
  ASSERT_TRUE(voxel.setRowStride(3).isOk());
  ASSERT_TRUE(voxel.setSliceStride(3).isOk());
  ASSERT_TRUE(voxel.addField("occupancy", 0, pj::ObjectWriter::PointFieldDatatype::kUint8).isOk());
  ASSERT_TRUE(voxel.setData({data.data(), data.size()}).isOk());
  auto voxel_descriptor = voxel_writer.finish();
  ASSERT_TRUE(voxel_descriptor.hasValue()) << voxel_descriptor.status().message();
  auto voxel_output = PJ::parser_module::readOutputDescriptorV1({voxel_descriptor->data(), voxel_descriptor->size()});
  ASSERT_TRUE(voxel_output.has_value()) << voxel_output.error();
  const auto& voxel_wire = std::get<PJ::parser_module::ObjectOutputV1>(*voxel_output).wire;
  auto decoded_voxel =
      PJ::deserializeBuiltinObject(PJ::sdk::BuiltinObjectType::kVoxelGrid, voxel_wire.data(), voxel_wire.size());
  ASSERT_TRUE(decoded_voxel.has_value()) << decoded_voxel.error();
  const auto* voxel_value = std::any_cast<PJ::sdk::VoxelGrid>(&*decoded_voxel);
  ASSERT_NE(voxel_value, nullptr);
  ASSERT_EQ(voxel_value->fields.size(), 1U);
  EXPECT_EQ(voxel_value->fields[0].name, "occupancy");
  EXPECT_EQ(voxel_value->data.size(), 3U);

  pj::ObjectWriter grid_map_writer;
  auto grid_map = grid_map_writer.gridMap();
  ASSERT_TRUE(grid_map.setTimestamp(7).isOk());
  ASSERT_TRUE(grid_map.setFrameId("odom").isOk());
  ASSERT_TRUE(grid_map.setOrigin(1, 2, 3).isOk());
  ASSERT_TRUE(grid_map.setCellSize(0.1, 0.2).isOk());
  ASSERT_TRUE(grid_map.setColumnCount(3).isOk());
  ASSERT_TRUE(grid_map.setRowCount(1).isOk());
  ASSERT_TRUE(grid_map.setCellStride(1).isOk());
  ASSERT_TRUE(grid_map.setRowStride(3).isOk());
  ASSERT_TRUE(grid_map.addField("cost", 0, pj::ObjectWriter::PointFieldDatatype::kUint8).isOk());
  ASSERT_TRUE(grid_map.setData({data.data(), data.size()}).isOk());
  auto grid_map_descriptor = grid_map_writer.finish();
  ASSERT_TRUE(grid_map_descriptor.hasValue()) << grid_map_descriptor.status().message();
  auto grid_map_output =
      PJ::parser_module::readOutputDescriptorV1({grid_map_descriptor->data(), grid_map_descriptor->size()});
  ASSERT_TRUE(grid_map_output.has_value()) << grid_map_output.error();
  const auto& grid_map_wire = std::get<PJ::parser_module::ObjectOutputV1>(*grid_map_output).wire;
  auto decoded_grid_map =
      PJ::deserializeBuiltinObject(PJ::sdk::BuiltinObjectType::kGridMap, grid_map_wire.data(), grid_map_wire.size());
  ASSERT_TRUE(decoded_grid_map.has_value()) << decoded_grid_map.error();
  const auto* grid_map_value = std::any_cast<PJ::sdk::GridMap>(&*decoded_grid_map);
  ASSERT_NE(grid_map_value, nullptr);
  EXPECT_EQ(grid_map_value->frame_id, "odom");
  EXPECT_DOUBLE_EQ(grid_map_value->cell_size.x, 0.1);
  EXPECT_DOUBLE_EQ(grid_map_value->cell_size.y, 0.2);
  EXPECT_EQ(grid_map_value->column_count, 3U);
  ASSERT_EQ(grid_map_value->fields.size(), 1U);
  EXPECT_EQ(grid_map_value->fields[0].name, "cost");
  EXPECT_EQ(grid_map_value->data.size(), 3U);
}

TEST(ParserModuleObjectWriter, EveryEligibleBuilderSupportsValidatedSpliceOutput) {
  const std::array<uint8_t, 8> payload{};
  const auto expect_splice = [](pj::ObjectWriter& writer, uint16_t type, uint32_t field) {
    auto descriptor = writer.finish();
    ASSERT_TRUE(descriptor.hasValue()) << descriptor.status().message();
    auto decoded = PJ::parser_module::readOutputDescriptorV1({descriptor->data(), descriptor->size()});
    ASSERT_TRUE(decoded.has_value()) << decoded.error();
    const auto& object = std::get<PJ::parser_module::ObjectOutputV1>(*decoded);
    EXPECT_EQ(object.object_type, type);
    ASSERT_TRUE(object.splice.has_value());
    EXPECT_EQ(object.splice->field_number, field);
    EXPECT_EQ(object.splice->input_offset, 2U);
    EXPECT_EQ(object.splice->input_length, 3U);
  };

  pj::ObjectWriter depth({payload.data(), payload.size()});
  ASSERT_TRUE(depth.depthImage().setDataFromInput({2, 3}).isOk());
  expect_splice(depth, 4, 5);
  pj::ObjectWriter grid({payload.data(), payload.size()});
  ASSERT_TRUE(grid.occupancyGrid().setDataFromInput({2, 3}).isOk());
  expect_splice(grid, 7, 7);
  pj::ObjectWriter compressed({payload.data(), payload.size()});
  ASSERT_TRUE(compressed.compressedPointCloud().setDataFromInput({2, 3}).isOk());
  expect_splice(compressed, 8, 4);
  pj::ObjectWriter mesh({payload.data(), payload.size()});
  ASSERT_TRUE(mesh.mesh3D().setDataFromInput({2, 3}).isOk());
  expect_splice(mesh, 9, 7);
  pj::ObjectWriter video({payload.data(), payload.size()});
  ASSERT_TRUE(video.videoFrame().setDataFromInput({2, 3}).isOk());
  expect_splice(video, 10, 3);
  pj::ObjectWriter update({payload.data(), payload.size()});
  ASSERT_TRUE(update.occupancyGridUpdate().setDataFromInput({2, 3}).isOk());
  expect_splice(update, 15, 7);
  pj::ObjectWriter voxel({payload.data(), payload.size()});
  ASSERT_TRUE(voxel.voxelGrid().setDataFromInput({2, 3}).isOk());
  expect_splice(voxel, 18, 12);
  pj::ObjectWriter grid_map({payload.data(), payload.size()});
  ASSERT_TRUE(grid_map.gridMap().setDataFromInput({2, 3}).isOk());
  expect_splice(grid_map, 20, 10);
}

TEST(ParserModuleObjectWriter, RejectsConflictingOrOutOfBoundsBulkDataSelection) {
  const std::array<uint8_t, 2> payload{1, 2};
  pj::ObjectWriter copied({payload.data(), payload.size()});
  auto image = copied.image();
  ASSERT_TRUE(image.setData({payload.data(), payload.size()}).isOk());
  EXPECT_FALSE(image.setDataFromInput({0, 1}).isOk());
  EXPECT_NE(copied.status().message().find("contract_violation"), std::string_view::npos);

  pj::ObjectWriter spliced({payload.data(), payload.size()});
  auto cloud = spliced.pointCloud();
  ASSERT_TRUE(cloud.setDataFromInput({0, 1}).isOk());
  EXPECT_FALSE(cloud.setDataFromInput({1, 1}).isOk());

  pj::ObjectWriter out_of_bounds({payload.data(), payload.size()});
  EXPECT_FALSE(out_of_bounds.videoFrame().setDataFromInput({1, 2}).isOk());
}

}  // namespace
