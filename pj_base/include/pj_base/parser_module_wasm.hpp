#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_module_wasm.hpp
 * @brief Bounds-checked static inspection of parser-module wasm binaries.
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"

namespace PJ::parser_module {

inline constexpr uint8_t kWasmValueI32 = UINT8_C(0x7F);
inline constexpr uint8_t kWasmValueI64 = UINT8_C(0x7E);
inline constexpr uint64_t kWasmMemoryPageBytes = UINT64_C(65536);

enum class WasmExternalKind : uint8_t {
  kFunction = 0,
  kTable = 1,
  kMemory = 2,
  kGlobal = 3,
  kTag = 4,
};

struct WasmFunctionSignature {
  std::vector<uint8_t> parameters;
  std::vector<uint8_t> results;

  bool operator==(const WasmFunctionSignature&) const = default;
};

struct WasmMemoryLimits {
  uint32_t minimum_pages = 0;
  std::optional<uint32_t> maximum_pages;

  bool operator==(const WasmMemoryLimits&) const = default;
};

struct WasmImport {
  std::string module;
  std::string name;
  WasmExternalKind kind = WasmExternalKind::kFunction;
  std::optional<WasmFunctionSignature> function_signature;
};

struct WasmExport {
  std::string name;
  WasmExternalKind kind = WasmExternalKind::kFunction;
  uint32_t index = 0;
  std::optional<WasmFunctionSignature> function_signature;
  std::optional<WasmMemoryLimits> memory_limits;
};

struct WasmModuleInfo {
  std::vector<WasmImport> imports;
  std::vector<WasmExport> exports;
  std::vector<WasmMemoryLimits> memories;
  bool has_start_section = false;
  size_t section_count = 0;
  size_t function_type_count = 0;
  size_t function_count = 0;

  [[nodiscard]] const WasmExport* findExport(std::string_view name) const noexcept;
};

/** Inspect the sections needed for parser-module admission.
 *
 * Every section and variable-length integer is bounds-checked. Function
 * signatures are resolved for imported and exported functions. Malformed,
 * truncated, duplicate, or unsupported binary constructs return an error.
 */
[[nodiscard]] Expected<WasmModuleInfo> inspectWasmModule(Span<const uint8_t> wasm);

/** Validate the frozen operational exports and reactor constraints.
 *
 * Import policy and the exported linear memory needed for execution are loader
 * policy layered on top of this ABI-only validation.
 */
[[nodiscard]] Expected<void> validateParserModuleWasmAbi(const WasmModuleInfo& module);

/** Validate bounded linear memory and return its aggregate declared maximum.
 *
 * Every declared memory must provide a maximum no larger than
 * `maximum_bytes`. The operational `memory` export must resolve to one of
 * those declarations. Byte arithmetic is overflow-checked.
 */
[[nodiscard]] Expected<uint64_t> validateParserModuleWasmMemory(const WasmModuleInfo& module, uint64_t maximum_bytes);

}  // namespace PJ::parser_module
