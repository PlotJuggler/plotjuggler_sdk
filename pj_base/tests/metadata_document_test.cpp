// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/metadata_document.hpp"

#include <gtest/gtest.h>

namespace PJ::sdk {
namespace {

TEST(MetadataDocumentBuilderTest, EmptyBuilderYieldsEmptyObject) {
  EXPECT_EQ(MetadataDocumentBuilder().build(), "{}");
}

TEST(MetadataDocumentBuilderTest, DescriptionUsesCanonicalKey) {
  EXPECT_EQ(kDescriptionMetadataKey, "description");
  EXPECT_EQ(MetadataDocumentBuilder().description("Vehicle speed").build(), R"({"description":"Vehicle speed"})");
}

TEST(MetadataDocumentBuilderTest, KeysAreLexicographicAndLaterValueReplaces) {
  const std::string json =
      MetadataDocumentBuilder().string("unit", "m/s").description("first").description("second").build();
  EXPECT_EQ(json, R"({"description":"second","unit":"m/s"})");
}

TEST(MetadataDocumentBuilderTest, EscapesKeysAndValues) {
  const std::string json = MetadataDocumentBuilder().string("a\"b", "c\\d\ne\tf\x01").build();
  // Not a raw string: MSVC reads a "\u0001" inside one as a (forbidden) universal-character-name.
  EXPECT_EQ(json, "{\"a\\\"b\":\"c\\\\d\\ne\\tf\\u0001\"}");
}

}  // namespace
}  // namespace PJ::sdk
