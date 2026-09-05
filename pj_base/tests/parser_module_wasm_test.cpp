// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/parser_module_wasm.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace PJ::parser_module {
namespace {

std::vector<uint8_t> module() {
  return {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
}

void appendSection(std::vector<uint8_t>* wasm, uint8_t id, const std::vector<uint8_t>& payload) {
  ASSERT_LT(payload.size(), 128U);
  wasm->push_back(id);
  wasm->push_back(static_cast<uint8_t>(payload.size()));
  wasm->insert(wasm->end(), payload.begin(), payload.end());
}

void expectRejected(std::vector<uint8_t> wasm, std::string expected) {
  auto inspected = inspectWasmModule(wasm);
  ASSERT_FALSE(inspected.has_value());
  EXPECT_NE(inspected.error().find(expected), std::string::npos) << inspected.error();
}

TEST(ParserModuleWasm, RejectsMalformedSyntheticSections) {
  auto truncated = module();
  truncated.insert(truncated.end(), {1, 4, 0});
  expectRejected(std::move(truncated), "section exceeds");

  auto overflowing_varuint = module();
  overflowing_varuint.insert(overflowing_varuint.end(), {1, 0x80, 0x80, 0x80, 0x80, 0x10});
  expectRejected(std::move(overflowing_varuint), "section header");

  auto duplicate = module();
  appendSection(&duplicate, 1, {0});
  appendSection(&duplicate, 1, {0});
  expectRejected(std::move(duplicate), "duplicate standard");

  auto trailing = module();
  appendSection(&trailing, 1, {0, 0});
  expectRejected(std::move(trailing), "trailing bytes");

  auto invalid_value_type = module();
  appendSection(&invalid_value_type, 1, {1, 0x60, 1, 0x01, 0});
  expectRejected(std::move(invalid_value_type), "invalid value type");

  auto unknown_section = module();
  appendSection(&unknown_section, 13, {});
  expectRejected(std::move(unknown_section), "unknown wasm section id");
}

TEST(ParserModuleWasm, RejectsInvalidIndicesAndUnboundedVectorCounts) {
  auto invalid_type_index = module();
  appendSection(&invalid_type_index, 1, {1, 0x60, 0, 0});
  appendSection(&invalid_type_index, 3, {1, 1});
  expectRejected(std::move(invalid_type_index), "invalid type index");

  auto invalid_function_index = module();
  appendSection(&invalid_function_index, 1, {1, 0x60, 0, 0});
  appendSection(&invalid_function_index, 3, {1, 0});
  appendSection(&invalid_function_index, 7, {1, 1, 'f', 0, 1});
  expectRejected(std::move(invalid_function_index), "invalid function index");

  auto hostile_count = module();
  appendSection(&hostile_count, 1, {0xFF, 0xFF, 0xFF, 0xFF, 0x0F});
  expectRejected(std::move(hostile_count), "count exceeds the remaining section bytes");
}

TEST(ParserModuleWasm, ParsesAndBoundsTableSections) {
  auto unsupported_form = module();
  appendSection(&unsupported_form, 4, {1, 0x40, 0x00, 0x70, 0, 1});
  expectRejected(std::move(unsupported_form), "unsupported wasm table form");

  auto unbounded = module();
  appendSection(&unbounded, 4, {1, 0x70, 0, 25});
  auto inspected_unbounded = inspectWasmModule(unbounded);
  ASSERT_TRUE(inspected_unbounded.has_value()) << inspected_unbounded.error();
  ASSERT_EQ(inspected_unbounded->tables.size(), 1U);
  EXPECT_EQ(inspected_unbounded->tables.front().minimum_elements, 25U);
  EXPECT_FALSE(inspected_unbounded->tables.front().maximum_elements.has_value());
  auto unbounded_tables = validateParserModuleWasmTables(*inspected_unbounded, 1000);
  ASSERT_FALSE(unbounded_tables.has_value());
  EXPECT_NE(unbounded_tables.error().find("no declared maximum"), std::string::npos);

  auto bounded = module();
  appendSection(&bounded, 4, {2, 0x70, 1, 25, 100, 0x6F, 1, 0, 50});
  auto inspected_bounded = inspectWasmModule(bounded);
  ASSERT_TRUE(inspected_bounded.has_value()) << inspected_bounded.error();
  ASSERT_EQ(inspected_bounded->tables.size(), 2U);
  auto within_cap = validateParserModuleWasmTables(*inspected_bounded, 150);
  ASSERT_TRUE(within_cap.has_value()) << within_cap.error();
  EXPECT_EQ(*within_cap, 150U);
  auto over_cap = validateParserModuleWasmTables(*inspected_bounded, 149);
  ASSERT_FALSE(over_cap.has_value());
  EXPECT_NE(over_cap.error().find("exceeds configured cap"), std::string::npos);

  auto none = module();
  auto inspected_none = inspectWasmModule(none);
  ASSERT_TRUE(inspected_none.has_value());
  auto no_tables = validateParserModuleWasmTables(*inspected_none, 0);
  ASSERT_TRUE(no_tables.has_value());
  EXPECT_EQ(*no_tables, 0U);
}

}  // namespace
}  // namespace PJ::parser_module
