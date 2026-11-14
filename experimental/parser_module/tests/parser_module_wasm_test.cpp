// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/parser_module_wasm.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <tuple>
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

void appendVarUint32(std::vector<uint8_t>* bytes, uint32_t value) {
  do {
    const auto low = static_cast<uint8_t>(value & 0x7FU);
    value >>= 7U;
    bytes->push_back(value == 0 ? low : static_cast<uint8_t>(low | 0x80U));
  } while (value != 0);
}

void appendLargeSection(std::vector<uint8_t>* wasm, uint8_t id, const std::vector<uint8_t>& payload) {
  wasm->push_back(id);
  appendVarUint32(wasm, static_cast<uint32_t>(payload.size()));
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

TEST(ParserModuleWasm, SharesOneSignatureAcrossEveryExportOfAFunction) {
  // One 64 KiB-parameter signature exported under 4096 names: copying it per
  // export would expand a ~100 KiB artifact into 256 MiB during inspection.
  constexpr uint32_t kParameters = 64 * 1024;
  constexpr uint32_t kExports = 4096;
  std::vector<uint8_t> types{1, 0x60};
  appendVarUint32(&types, kParameters);
  types.insert(types.end(), kParameters, kWasmValueI32);
  types.push_back(0);
  std::vector<uint8_t> exports;
  appendVarUint32(&exports, kExports);
  for (uint32_t index = 0; index < kExports; ++index) {
    const std::string name = "e" + std::to_string(index);
    appendVarUint32(&exports, static_cast<uint32_t>(name.size()));
    exports.insert(exports.end(), name.begin(), name.end());
    exports.insert(exports.end(), {0x00, 0x00});  // function 0
  }
  auto wasm = module();
  appendLargeSection(&wasm, 1, types);
  appendSection(&wasm, 3, {1, 0});
  appendLargeSection(&wasm, 7, exports);

  auto inspected = inspectWasmModule(wasm);
  ASSERT_TRUE(inspected.has_value()) << inspected.error();
  ASSERT_EQ(inspected->exports.size(), kExports);
  const WasmFunctionSignature* first = inspected->functionSignature(inspected->exports.front());
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->parameters.size(), kParameters);
  EXPECT_EQ(inspected->functionSignature(inspected->exports.back()), first);
}

TEST(ParserModuleWasm, RejectsDuplicateExportNamesAndOverLimitEntryCounts) {
  auto duplicate = module();
  appendSection(&duplicate, 1, {1, 0x60, 0, 0});
  appendSection(&duplicate, 3, {1, 0});
  appendSection(&duplicate, 7, {2, 1, 'f', 0, 0, 1, 'f', 0, 0});
  expectRejected(std::move(duplicate), "duplicate wasm export name: f");

  // The count is checked before any entry, so filler bytes suffice.
  for (const auto& [id, limit, minimum_entry_bytes] :
       {std::tuple<uint8_t, uint32_t, size_t>{1, kWasmMaximumTypes, 3},
        {2, kWasmMaximumImports, 4},
        {7, kWasmMaximumExports, 3}}) {
    std::vector<uint8_t> payload;
    appendVarUint32(&payload, limit + 1);
    payload.resize(payload.size() + (size_t{limit} + 1) * minimum_entry_bytes, 0);
    auto over_limit = module();
    appendLargeSection(&over_limit, id, payload);
    expectRejected(std::move(over_limit), "count exceeds the limit of " + std::to_string(limit));
  }
}

}  // namespace
}  // namespace PJ::parser_module
