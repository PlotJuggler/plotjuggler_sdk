// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/object_topic_metadata.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ::sdk {
namespace {

using SourceRawRegistration =
    Expected<ObjectTopicHandle> (SourceObjectWriteHostView::*)(std::string_view, std::string_view) const;
using ToolboxRawRegistration =
    Expected<ObjectTopicHandle> (ToolboxHostView::*)(DataSourceHandle, std::string_view, std::string_view) const;
using ToolboxDatasetRawRegistration =
    Expected<ObjectTopicHandle> (ToolboxHostView::*)(DatasetId, std::string_view, std::string_view) const;

static_assert(std::is_same_v<
              decltype(static_cast<SourceRawRegistration>(&SourceObjectWriteHostView::registerTopic)),
              SourceRawRegistration>);
static_assert(
    std::is_same_v<
        decltype(static_cast<ToolboxRawRegistration>(&ToolboxHostView::registerObjectTopic)), ToolboxRawRegistration>);
static_assert(std::is_same_v<
              decltype(static_cast<ToolboxDatasetRawRegistration>(&ToolboxHostView::registerObjectTopicOnDataset)),
              ToolboxDatasetRawRegistration>);

constexpr std::array kBuiltinObjectTypes{
    BuiltinObjectType::kImage,
    BuiltinObjectType::kPointCloud,
    BuiltinObjectType::kDepthImage,
    BuiltinObjectType::kImageAnnotations,
    BuiltinObjectType::kFrameTransforms,
    BuiltinObjectType::kOccupancyGrid,
    BuiltinObjectType::kCompressedPointCloud,
    BuiltinObjectType::kMesh3D,
    BuiltinObjectType::kVideoFrame,
    BuiltinObjectType::kSceneEntities,
    BuiltinObjectType::kRobotDescription,
    BuiltinObjectType::kCameraInfo,
    BuiltinObjectType::kOccupancyGridUpdate,
    BuiltinObjectType::kLog,
    BuiltinObjectType::kPosesInFrame,
    BuiltinObjectType::kVoxelGrid,
    BuiltinObjectType::kPlotMarkers,
};

struct RegistrationRecorder {
  uint32_t call_count = 0;
  uint32_t source_id = 0;
  DatasetId dataset_id = 0;
  std::string topic_name;
  std::string metadata_json;
};

std::string copyString(PJ_string_view_t value) {
  return std::string(value.data, static_cast<size_t>(value.size));
}

bool sourceRegisterTopic(
    void* ctx, PJ_string_view_t topic_name, PJ_string_view_t metadata_json, PJ_object_topic_handle_t* out_handle,
    PJ_error_t*) noexcept {
  auto& recorder = *static_cast<RegistrationRecorder*>(ctx);
  ++recorder.call_count;
  recorder.topic_name = copyString(topic_name);
  recorder.metadata_json = copyString(metadata_json);
  *out_handle = PJ_object_topic_handle_t{41};
  return true;
}

bool toolboxRegisterObjectTopic(
    void* ctx, PJ_data_source_handle_t source, PJ_string_view_t topic_name, PJ_string_view_t metadata_json,
    PJ_object_topic_handle_t* out_handle, PJ_error_t*) noexcept {
  auto& recorder = *static_cast<RegistrationRecorder*>(ctx);
  ++recorder.call_count;
  recorder.source_id = source.id;
  recorder.topic_name = copyString(topic_name);
  recorder.metadata_json = copyString(metadata_json);
  *out_handle = PJ_object_topic_handle_t{42};
  return true;
}

bool toolboxRegisterObjectTopicOnDataset(
    void* ctx, uint32_t dataset_id, PJ_string_view_t topic_name, PJ_string_view_t metadata_json,
    PJ_object_topic_handle_t* out_handle, PJ_error_t*) noexcept {
  auto& recorder = *static_cast<RegistrationRecorder*>(ctx);
  ++recorder.call_count;
  recorder.dataset_id = dataset_id;
  recorder.topic_name = copyString(topic_name);
  recorder.metadata_json = copyString(metadata_json);
  *out_handle = PJ_object_topic_handle_t{43};
  return true;
}

TEST(ObjectTopicMetadataBuilderTest, EveryBuiltinTypeUsesCanonicalRoundTrippableName) {
  for (const BuiltinObjectType type : kBuiltinObjectTypes) {
    const auto parsed = parseBuiltinObjectType(name(type));
    ASSERT_TRUE(parsed.has_value()) << name(type);
    EXPECT_EQ(*parsed, type) << name(type);

    const std::string expected = "{\"builtin_object_type\":\"" + std::string(name(type)) + "\"}";
    EXPECT_EQ(ObjectTopicMetadataBuilder().builtinObjectType(type).build(), expected);
  }
}

TEST(ObjectTopicMetadataBuilderTest, EscapesCustomKeysAndValuesInStableKeyOrder) {
  std::string key = "z\"\\";
  key.push_back('\n');
  key.push_back('\x01');

  std::string value = "value\"\\";
  value.push_back('\b');
  value.push_back('\f');
  value.push_back('\n');
  value.push_back('\r');
  value.push_back('\t');
  value.push_back('\x02');

  const std::string json = ObjectTopicMetadataBuilder()
                               .string(key, value)
                               .string("a_first", "original")
                               .string("a_first", "replaced")
                               .builtinObjectType(BuiltinObjectType::kImage)
                               .build();

  const std::string expected =
      "{\"builtin_object_type\":\"kImage\",\"a_first\":\"replaced\","
      "\"z\\\"\\\\\\n\\u0001\":\"value\\\"\\\\\\b\\f\\n\\r\\t\\u0002\"}";
  EXPECT_EQ(json, expected);
}

TEST(ObjectTopicMetadataRegistrationTest, SourceTypedOverloadForwardsBuiltJson) {
  RegistrationRecorder recorder;
  const PJ_object_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_object_write_host_vtable_t),
      .register_topic = sourceRegisterTopic,
  };
  const SourceObjectWriteHostView view(PJ_object_write_host_t{.ctx = &recorder, .vtable = &vtable});

  ObjectTopicMetadataBuilder extra_metadata;
  extra_metadata.string("image_codec", "pj_image_v1");
  const auto result = view.registerTopic("camera", BuiltinObjectType::kImage, extra_metadata);

  ASSERT_TRUE(result) << result.error();
  EXPECT_EQ(result->id, 41U);
  EXPECT_EQ(recorder.call_count, 1U);
  EXPECT_EQ(recorder.topic_name, "camera");
  EXPECT_EQ(recorder.metadata_json, R"({"builtin_object_type":"kImage","image_codec":"pj_image_v1"})");
}

TEST(ObjectTopicMetadataRegistrationTest, ToolboxTypedOverloadsForwardBuiltJson) {
  RegistrationRecorder recorder;
  const PJ_toolbox_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_toolbox_host_vtable_t),
      .register_object_topic = toolboxRegisterObjectTopic,
      .register_object_topic_on_dataset = toolboxRegisterObjectTopicOnDataset,
  };
  const ToolboxHostView view(PJ_toolbox_host_t{.ctx = &recorder, .vtable = &vtable});

  const auto source_result = view.registerObjectTopic(DataSourceHandle{7}, "points", BuiltinObjectType::kPointCloud);
  ASSERT_TRUE(source_result) << source_result.error();
  EXPECT_EQ(source_result->id, 42U);
  EXPECT_EQ(recorder.call_count, 1U);
  EXPECT_EQ(recorder.source_id, 7U);
  EXPECT_EQ(recorder.topic_name, "points");
  EXPECT_EQ(recorder.metadata_json, R"({"builtin_object_type":"kPointCloud"})");

  ObjectTopicMetadataBuilder extra_metadata;
  extra_metadata.string("producer", "annotations");
  const auto dataset_result =
      view.registerObjectTopicOnDataset(9, "markers", BuiltinObjectType::kPlotMarkers, extra_metadata);
  ASSERT_TRUE(dataset_result) << dataset_result.error();
  EXPECT_EQ(dataset_result->id, 43U);
  EXPECT_EQ(recorder.call_count, 2U);
  EXPECT_EQ(recorder.dataset_id, 9U);
  EXPECT_EQ(recorder.topic_name, "markers");
  EXPECT_EQ(recorder.metadata_json, R"({"builtin_object_type":"kPlotMarkers","producer":"annotations"})");
}

}  // namespace
}  // namespace PJ::sdk
