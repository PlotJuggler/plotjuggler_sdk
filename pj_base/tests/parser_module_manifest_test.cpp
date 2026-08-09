// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/parser_module_manifest.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PJ::parser_module {
namespace {

constexpr std::array<uint8_t, 8> kMinimalWasm{0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
constexpr std::array<uint8_t, 7> kManifest{'{', '"', 'x', '"', ':', '1', '}'};

const std::vector<uint8_t> kEmbeddedGolden{
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,  // wasm preamble
    0x00, 0x21,                                      // custom section, 33-byte payload
    0x19,                                            // 25-byte custom-section name
    'p',  'j',  '_',  'p',  'a',  'r',  's',  'e',  'r', '_', 'm', 'o', 'd', 'u', 'l', 'e',
    '_',  'm',  'a',  'n',  'i',  'f',  'e',  's',  't', '{', '"', 'x', '"', ':', '1', '}',
};

TEST(ParserModuleManifest, AppendsGoldenSectionAndReadsExactBytes) {
  auto embedded = appendManifestSection(kMinimalWasm, kManifest);
  ASSERT_TRUE(embedded.has_value()) << embedded.error();
  EXPECT_EQ(*embedded, kEmbeddedGolden);

  auto decoded = readManifestSection(*embedded);
  ASSERT_TRUE(decoded.has_value()) << decoded.error();
  EXPECT_TRUE(std::equal(decoded->begin(), decoded->end(), kManifest.begin(), kManifest.end()));
}

TEST(ParserModuleManifest, RejectsZeroOrMultipleManifestSections) {
  auto missing = readManifestSection(kMinimalWasm);
  ASSERT_FALSE(missing.has_value());
  EXPECT_NE(missing.error().find("no parser-module manifest"), std::string::npos);

  std::vector<uint8_t> duplicate = kEmbeddedGolden;
  duplicate.insert(
      duplicate.end(), kEmbeddedGolden.begin() + static_cast<std::ptrdiff_t>(kMinimalWasm.size()),
      kEmbeddedGolden.end());
  auto decoded = readManifestSection(duplicate);
  ASSERT_FALSE(decoded.has_value());
  EXPECT_NE(decoded.error().find("multiple parser-module manifest sections"), std::string::npos);
  EXPECT_FALSE(appendManifestSection(kEmbeddedGolden, kManifest).has_value());
}

TEST(ParserModuleManifest, RejectsTruncatedPreamblesAndSections) {
  for (size_t size = 0; size < kMinimalWasm.size(); ++size) {
    EXPECT_FALSE(readManifestSection(Span<const uint8_t>(kMinimalWasm.data(), size)).has_value()) << size;
  }

  auto wrong_magic = kMinimalWasm;
  wrong_magic[1] = 0;
  EXPECT_FALSE(readManifestSection(wrong_magic).has_value());

  for (size_t size = kMinimalWasm.size(); size < kEmbeddedGolden.size(); ++size) {
    EXPECT_FALSE(readManifestSection(Span<const uint8_t>(kEmbeddedGolden.data(), size)).has_value()) << size;
  }

  std::vector<uint8_t> overflowing_leb(kMinimalWasm.begin(), kMinimalWasm.end());
  overflowing_leb.insert(overflowing_leb.end(), {0, 0x80, 0x80, 0x80, 0x80, 0x10});
  EXPECT_FALSE(readManifestSection(overflowing_leb).has_value());
}

}  // namespace
}  // namespace PJ::parser_module
