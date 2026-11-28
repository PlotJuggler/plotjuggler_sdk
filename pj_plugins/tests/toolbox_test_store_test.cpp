// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/testing/toolbox_test_store.hpp"

#include <gtest/gtest.h>

#include "pj_base/sdk/metadata_document.hpp"

namespace PJ::testing {
namespace {

TEST(ToolboxTestStoreTest, KeepsFieldAndTopicMetadataPerHandle) {
  ToolboxTestStore store;
  const sdk::ToolboxHostView view(store.makeHost());
  ASSERT_TRUE(view.supportsFieldMetadata());
  ASSERT_TRUE(view.supportsTopicMetadata());

  const PJ_field_handle_t speed{.topic = {1}, .id = 7};
  const PJ_field_handle_t other{.topic = {2}, .id = 7};
  const PJ_topic_handle_t topic{1};
  const std::string doc = sdk::MetadataDocumentBuilder().description("Vehicle speed").build();

  ASSERT_TRUE(view.setFieldMetadata(speed, doc));
  ASSERT_TRUE(view.setTopicMetadata(topic, R"({"description":"IMU"})"));
  EXPECT_EQ(store.fieldMetadata(speed), doc);
  EXPECT_TRUE(store.fieldMetadata(other).empty());  // same field id, another topic
  EXPECT_EQ(store.topicMetadata(topic), R"({"description":"IMU"})");
  EXPECT_TRUE(store.topicMetadata(PJ_topic_handle_t{2}).empty());
}

TEST(ToolboxTestStoreTest, ReadsMetadataBackThroughTheHostView) {
  ToolboxTestStore store;
  const sdk::ToolboxHostView view(store.makeHost());
  ASSERT_TRUE(view.supportsMetadataRead());

  const PJ_field_handle_t field{.topic = {1}, .id = 3};
  const std::string doc = sdk::MetadataDocumentBuilder().description("Vehicle speed").unit("m/s").build();
  ASSERT_TRUE(view.setFieldMetadata(field, doc));
  ASSERT_TRUE(view.setTopicMetadata(PJ_topic_handle_t{1}, R"({"description":"IMU"})"));

  EXPECT_EQ(view.fieldMetadata(field).value(), doc);
  EXPECT_EQ(view.topicMetadata(PJ_topic_handle_t{1}).value(), R"({"description":"IMU"})");
  EXPECT_EQ(view.fieldMetadata(PJ_field_handle_t{.topic = {1}, .id = 4}).value(), "");
}

TEST(ToolboxTestStoreTest, SetReplacesAndEmptyTextClears) {
  ToolboxTestStore store;
  const sdk::ToolboxHostView view(store.makeHost());
  const PJ_field_handle_t field{.topic = {1}, .id = 3};
  const PJ_topic_handle_t topic{1};

  ASSERT_TRUE(view.setFieldMetadata(field, R"({"description":"first"})"));
  ASSERT_TRUE(view.setFieldMetadata(field, R"({"description":"second"})"));
  EXPECT_EQ(store.fieldMetadata(field), R"({"description":"second"})");
  ASSERT_TRUE(view.setFieldMetadata(field, ""));
  EXPECT_TRUE(store.fieldMetadata(field).empty());

  ASSERT_TRUE(view.setTopicMetadata(topic, R"({"description":"first"})"));
  ASSERT_TRUE(view.setTopicMetadata(topic, ""));
  EXPECT_TRUE(store.topicMetadata(topic).empty());
}

}  // namespace
}  // namespace PJ::testing
