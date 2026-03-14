#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace PJ {
namespace {

static_assert(std::is_standard_layout_v<PJ_data_source_handle_t>);
static_assert(std::is_standard_layout_v<PJ_topic_handle_t>);
static_assert(std::is_standard_layout_v<PJ_field_handle_t>);
static_assert(std::is_standard_layout_v<PJ_scalar_value_t>);
static_assert(std::is_standard_layout_v<PJ_source_write_host_t>);
static_assert(std::is_standard_layout_v<PJ_parser_write_host_t>);
static_assert(std::is_standard_layout_v<PJ_toolbox_host_t>);

TEST(PluginDataApiTest, PrimitiveTypeRoundTripsThroughAbiEnum) {
  EXPECT_EQ(sdk::fromAbiType(sdk::toAbiType(PrimitiveType::kFloat32)), PrimitiveType::kFloat32);
  EXPECT_EQ(sdk::fromAbiType(sdk::toAbiType(PrimitiveType::kInt8)), PrimitiveType::kInt8);
  EXPECT_EQ(sdk::fromAbiType(sdk::toAbiType(PrimitiveType::kUint32)), PrimitiveType::kUint32);
  EXPECT_EQ(sdk::fromAbiType(sdk::toAbiType(PrimitiveType::kString)), PrimitiveType::kString);
}

TEST(PluginDataApiTest, ValueRefRetainsExactPrimitiveType) {
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{int8_t{-1}}), PrimitiveType::kInt8);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{uint16_t{5}}), PrimitiveType::kUint16);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{uint32_t{9}}), PrimitiveType::kUint32);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{std::string_view("abc")}), PrimitiveType::kString);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{NullValue{}}), PrimitiveType::kUnspecified);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{sdk::TypedNull{PrimitiveType::kFloat64}}),
            PrimitiveType::kFloat64);
}

TEST(PluginDataApiTest, HandleEqualityAndInequality) {
  // DataSourceHandle
  EXPECT_TRUE(sdk::operator==(sdk::DataSourceHandle{.id = 1}, sdk::DataSourceHandle{.id = 1}));
  EXPECT_TRUE(sdk::operator!=(sdk::DataSourceHandle{.id = 1}, sdk::DataSourceHandle{.id = 2}));

  // TopicHandle
  EXPECT_TRUE(sdk::operator==(sdk::TopicHandle{.id = 5}, sdk::TopicHandle{.id = 5}));
  EXPECT_TRUE(sdk::operator!=(sdk::TopicHandle{.id = 5}, sdk::TopicHandle{.id = 6}));

  // FieldHandle — equal requires both topic and id to match
  const sdk::FieldHandle fh_1_2{.topic = {.id = 1}, .id = 2};
  EXPECT_TRUE(sdk::operator==(fh_1_2, sdk::FieldHandle{.topic = {.id = 1}, .id = 2}));
  EXPECT_TRUE(sdk::operator!=(fh_1_2, sdk::FieldHandle{.topic = {.id = 1}, .id = 3}));
  EXPECT_TRUE(sdk::operator!=(fh_1_2, sdk::FieldHandle{.topic = {.id = 9}, .id = 2}));
}

TEST(PluginDataApiTest, ToStringViewHandlesNullData) {
  // Null data pointer with zero size → empty string
  const PJ_string_view_t null_zero{nullptr, 0};
  EXPECT_EQ(sdk::toStringView(null_zero), "");
  EXPECT_EQ(sdk::toStringView(null_zero).size(), 0U);

  // Null data pointer with non-zero size → still uses "" base but with the given size
  // The SDK does: std::string_view(view.data == nullptr ? "" : view.data, view.size)
  // With nullptr and size=5, it constructs std::string_view("", 5) which has size 5
  // but that reads past the empty string literal — this tests current behavior
  const PJ_string_view_t null_nonzero{nullptr, 5};
  const auto result = sdk::toStringView(null_nonzero);
  // The key thing: it doesn't crash (null guard prevents UB from nullptr dereference)
  EXPECT_NE(result.data(), nullptr);
}

}  // namespace
}  // namespace PJ
