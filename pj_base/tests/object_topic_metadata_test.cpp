// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/object_topic_metadata.hpp"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
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

// The typed overload is a constrained template, so an untyped braced-init-list
// cannot deduce it. This expression can therefore only select the raw
// std::string_view overload.
static_assert(requires(const SourceObjectWriteHostView& view) {
  { view.registerTopic("x", {}) } -> std::same_as<Expected<ObjectTopicHandle>>;
});

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
  if (value.data == nullptr) {
    return {};
  }
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
    const auto metadata = ObjectTopicMetadataBuilder().builtinObjectType(type).build();
    ASSERT_TRUE(metadata) << name(type);
    EXPECT_EQ(*metadata, expected);
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

  const auto json = ObjectTopicMetadataBuilder()
                        .string(key, value)
                        .string("a_first", "original")
                        .string("a_first", "replaced")
                        .builtinObjectType(BuiltinObjectType::kImage)
                        .build();

  const std::string expected =
      "{\"builtin_object_type\":\"kImage\",\"a_first\":\"replaced\","
      "\"z\\\"\\\\\\n\\u0001\":\"value\\\"\\\\\\b\\f\\n\\r\\t\\u0002\"}";
  ASSERT_TRUE(json);
  EXPECT_EQ(*json, expected);
}

TEST(ObjectTopicMetadataBuilderTest, InvalidTypesAreErrorsWhenAssertionsAreDisabled) {
#if !defined(NDEBUG) || defined(PJ_ASSERT_THROWS)
  GTEST_SKIP() << "release-mode behavior requires elided debug assertions";
#else
  for (const BuiltinObjectType type : {
           BuiltinObjectType::kNone,
           static_cast<BuiltinObjectType>(2),
           static_cast<BuiltinObjectType>(12),
           static_cast<BuiltinObjectType>(999),
       }) {
    ObjectTopicMetadataBuilder metadata;
    metadata.builtinObjectType(type);
    const auto result = metadata.build();

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("known, non-reserved"), std::string::npos);
  }
#endif
}

TEST(ObjectTopicMetadataBuilderTest, InvalidTypeClearsEarlierValidTypeAndLeavesError) {
#if !defined(NDEBUG) || defined(PJ_ASSERT_THROWS)
  GTEST_SKIP() << "release-mode behavior requires elided debug assertions";
#else
  ObjectTopicMetadataBuilder metadata;
  metadata.builtinObjectType(BuiltinObjectType::kImage);
  metadata.builtinObjectType(static_cast<BuiltinObjectType>(12));
  metadata.builtinObjectType(BuiltinObjectType::kPointCloud);

  const auto result = metadata.build();
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("known, non-reserved"), std::string::npos);
#endif
}

TEST(ObjectTopicMetadataBuilderTest, CanonicalKeyCannotBeInsertedAsCustomMetadata) {
#if !defined(NDEBUG) || defined(PJ_ASSERT_THROWS)
  GTEST_SKIP() << "release-mode behavior requires elided debug assertions";
#else
  ObjectTopicMetadataBuilder metadata;
  metadata.builtinObjectType(BuiltinObjectType::kImage);
  metadata.string(kBuiltinObjectTypeMetadataKey, "kPointCloud");

  const auto result = metadata.build();
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("reserved"), std::string::npos);
#endif
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

TEST(ObjectTopicMetadataRegistrationTest, EmptyBracesUseRawMetadataOverload) {
  RegistrationRecorder recorder;
  const PJ_object_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_object_write_host_vtable_t),
      .register_topic = sourceRegisterTopic,
  };
  const SourceObjectWriteHostView view(PJ_object_write_host_t{.ctx = &recorder, .vtable = &vtable});

  const auto result = view.registerTopic("raw", {});

  ASSERT_TRUE(result);
  EXPECT_EQ(recorder.call_count, 1U);
  EXPECT_EQ(recorder.topic_name, "raw");
  EXPECT_TRUE(recorder.metadata_json.empty());
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

TEST(ObjectTopicMetadataRegistrationTest, InvalidTypedRegistrationsNeverReachRawVtables) {
#if !defined(NDEBUG) || defined(PJ_ASSERT_THROWS)
  GTEST_SKIP() << "release-mode behavior requires elided debug assertions";
#else
  RegistrationRecorder source_recorder;
  const PJ_object_write_host_vtable_t source_vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_object_write_host_vtable_t),
      .register_topic = sourceRegisterTopic,
  };
  const SourceObjectWriteHostView source_view(
      PJ_object_write_host_t{.ctx = &source_recorder, .vtable = &source_vtable});

  const auto source_result = source_view.registerTopic("invalid", BuiltinObjectType::kNone);
  ASSERT_FALSE(source_result);
  EXPECT_NE(source_result.error().find("known, non-reserved"), std::string::npos);
  EXPECT_EQ(source_recorder.call_count, 0U);

  RegistrationRecorder toolbox_recorder;
  const PJ_toolbox_host_vtable_t toolbox_vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_toolbox_host_vtable_t),
      .register_object_topic = toolboxRegisterObjectTopic,
      .register_object_topic_on_dataset = toolboxRegisterObjectTopicOnDataset,
  };
  const ToolboxHostView toolbox_view(PJ_toolbox_host_t{.ctx = &toolbox_recorder, .vtable = &toolbox_vtable});

  const auto toolbox_result =
      toolbox_view.registerObjectTopic(DataSourceHandle{7}, "invalid", static_cast<BuiltinObjectType>(12));
  ASSERT_FALSE(toolbox_result);
  EXPECT_NE(toolbox_result.error().find("known, non-reserved"), std::string::npos);
  EXPECT_EQ(toolbox_recorder.call_count, 0U);

  const auto dataset_result =
      toolbox_view.registerObjectTopicOnDataset(9, "invalid", static_cast<BuiltinObjectType>(999));
  ASSERT_FALSE(dataset_result);
  EXPECT_NE(dataset_result.error().find("known, non-reserved"), std::string::npos);
  EXPECT_EQ(toolbox_recorder.call_count, 0U);
#endif
}

}  // namespace
}  // namespace PJ::sdk
