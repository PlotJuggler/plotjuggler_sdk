// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/field_metadata.hpp"

#include <gtest/gtest.h>

namespace PJ::sdk {
namespace {

TEST(FieldMetadataBuilderTest, EmptyBuilderYieldsEmptyObject) {
  EXPECT_EQ(FieldMetadataBuilder().build(), "{}");
}

TEST(FieldMetadataBuilderTest, DescriptionUsesCanonicalKey) {
  EXPECT_EQ(kFieldDescriptionMetadataKey, "description");
  EXPECT_EQ(FieldMetadataBuilder().description("Vehicle speed").build(), R"({"description":"Vehicle speed"})");
}

TEST(FieldMetadataBuilderTest, KeysAreLexicographicAndLaterValueReplaces) {
  const std::string json =
      FieldMetadataBuilder().string("unit", "m/s").description("first").description("second").build();
  EXPECT_EQ(json, R"({"description":"second","unit":"m/s"})");
}

TEST(FieldMetadataBuilderTest, EscapesKeysAndValues) {
  const std::string json = FieldMetadataBuilder().string("a\"b", "c\\d\ne\tf\x01").build();
  // Not a raw string: MSVC reads a "\u0001" inside one as a (forbidden) universal-character-name.
  EXPECT_EQ(json, "{\"a\\\"b\":\"c\\\\d\\ne\\tf\\u0001\"}");
}

}  // namespace
}  // namespace PJ::sdk
