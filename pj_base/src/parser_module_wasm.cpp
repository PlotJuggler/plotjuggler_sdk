// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/parser_module_wasm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/parser_module_abi.h"

namespace PJ::parser_module {
namespace {

constexpr std::array<uint8_t, 8> kWasmPreamble{0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};

class Cursor {
 public:
  explicit Cursor(Span<const uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool empty() const noexcept {
    return position_ == bytes_.size();
  }

  [[nodiscard]] size_t remaining() const noexcept {
    return position_ <= bytes_.size() ? bytes_.size() - position_ : 0;
  }

  [[nodiscard]] Expected<uint8_t> byte() {
    if (position_ >= bytes_.size()) {
      return unexpected(std::string("truncated wasm byte"));
    }
    return bytes_[position_++];
  }

  [[nodiscard]] Expected<uint32_t> varUint32() {
    uint32_t value = 0;
    for (size_t index = 0; index < 5; ++index) {
      auto next = byte();
      if (!next) {
        return unexpected(next.error());
      }
      if (index == 4 && (*next & UINT8_C(0xF0)) != 0) {
        return unexpected(std::string("wasm varuint32 overflows uint32"));
      }
      value |= static_cast<uint32_t>(*next & UINT8_C(0x7F)) << (index * 7U);
      if ((*next & UINT8_C(0x80)) == 0) {
        return value;
      }
    }
    return unexpected(std::string("wasm varuint32 exceeds five bytes"));
  }

  [[nodiscard]] Expected<std::string> name() {
    auto length = varUint32();
    if (!length) {
      return unexpected(length.error());
    }
    if (static_cast<size_t>(*length) > remaining()) {
      return unexpected(std::string("wasm name exceeds the remaining section bytes"));
    }
    const char* begin = reinterpret_cast<const char*>(bytes_.data() + position_);
    position_ += *length;
    return std::string(begin, *length);
  }

  [[nodiscard]] Expected<Cursor> take(uint32_t size) {
    if (static_cast<size_t>(size) > remaining()) {
      return unexpected(std::string("wasm section exceeds the remaining module bytes"));
    }
    Cursor result(bytes_.subspan(position_, size));
    position_ += size;
    return result;
  }

 private:
  Span<const uint8_t> bytes_;
  size_t position_ = 0;
};

struct PendingImport {
  WasmImport value;
  std::optional<uint32_t> function_type;
};

struct ModuleBuilder {
  std::vector<WasmFunctionSignature> types;
  std::vector<uint32_t> imported_function_types;
  std::vector<uint32_t> defined_function_types;
  std::vector<WasmMemoryLimits> imported_memories;
  std::vector<WasmMemoryLimits> defined_memories;
  std::vector<PendingImport> imports;
  std::vector<WasmExport> exports;
  bool has_start_section = false;
  size_t section_count = 0;
};

[[nodiscard]] bool validValueType(uint8_t value) {
  switch (value) {
    case kWasmValueI32:
    case kWasmValueI64:
    case 0x7D:  // f32
    case 0x7C:  // f64
    case 0x7B:  // v128
    case 0x70:  // funcref
    case 0x6F:  // externref
      return true;
    default:
      return false;
  }
}

[[nodiscard]] Expected<void> requireCountFits(
    uint32_t count, size_t remaining, size_t minimum_entry_size, std::string_view section) {
  if (minimum_entry_size == 0 || static_cast<uint64_t>(count) > remaining / minimum_entry_size) {
    return unexpected(std::string("wasm ") + std::string(section) + " count exceeds the remaining section bytes");
  }
  return {};
}

[[nodiscard]] Expected<std::vector<uint8_t>> readValueTypes(Cursor* cursor) {
  auto count = cursor->varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  auto bounded = requireCountFits(*count, cursor->remaining(), 1, "value-type");
  if (!bounded) {
    return unexpected(bounded.error());
  }
  std::vector<uint8_t> result;
  result.reserve(*count);
  for (uint32_t index = 0; index < *count; ++index) {
    auto value = cursor->byte();
    if (!value) {
      return unexpected(value.error());
    }
    if (!validValueType(*value)) {
      return unexpected(std::string("wasm function type contains an invalid value type"));
    }
    result.push_back(*value);
  }
  return result;
}

[[nodiscard]] Expected<void> requireConsumed(const Cursor& cursor, std::string_view section) {
  if (!cursor.empty()) {
    return unexpected(std::string(section) + " section contains trailing bytes");
  }
  return {};
}

[[nodiscard]] Expected<void> parseTypeSection(Cursor cursor, ModuleBuilder* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  auto bounded = requireCountFits(*count, cursor.remaining(), 3, "type-section entry");
  if (!bounded) {
    return unexpected(bounded.error());
  }
  module->types.reserve(*count);
  for (uint32_t index = 0; index < *count; ++index) {
    auto form = cursor.byte();
    if (!form || *form != UINT8_C(0x60)) {
      return unexpected(std::string("wasm type section contains a non-function type"));
    }
    auto parameters = readValueTypes(&cursor);
    auto results = readValueTypes(&cursor);
    if (!parameters) {
      return unexpected(parameters.error());
    }
    if (!results) {
      return unexpected(results.error());
    }
    module->types.push_back(WasmFunctionSignature{std::move(*parameters), std::move(*results)});
  }
  return requireConsumed(cursor, "type");
}

[[nodiscard]] Expected<WasmMemoryLimits> readLimits(Cursor* cursor) {
  auto flags = cursor->varUint32();
  auto minimum = cursor->varUint32();
  if (!flags || !minimum) {
    return unexpected(std::string("truncated wasm limits"));
  }
  if (*flags > 1) {
    return unexpected(std::string("unsupported wasm limits flags"));
  }
  WasmMemoryLimits limits{.minimum_pages = *minimum, .maximum_pages = std::nullopt};
  if ((*flags & 1U) != 0) {
    auto maximum = cursor->varUint32();
    if (!maximum) {
      return unexpected(maximum.error());
    }
    if (*maximum < *minimum) {
      return unexpected(std::string("wasm limits maximum is smaller than its minimum"));
    }
    limits.maximum_pages = *maximum;
  }
  return limits;
}

[[nodiscard]] Expected<void> parseImportSection(Cursor cursor, ModuleBuilder* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  auto bounded = requireCountFits(*count, cursor.remaining(), 4, "import-section entry");
  if (!bounded) {
    return unexpected(bounded.error());
  }
  module->imports.reserve(*count);
  for (uint32_t index = 0; index < *count; ++index) {
    auto module_name = cursor.name();
    auto field_name = cursor.name();
    auto kind = cursor.byte();
    if (!module_name || !field_name || !kind || *kind > static_cast<uint8_t>(WasmExternalKind::kTag)) {
      return unexpected(std::string("truncated or invalid wasm import entry"));
    }

    PendingImport imported{
        .value =
            WasmImport{
                .module = std::move(*module_name),
                .name = std::move(*field_name),
                .kind = static_cast<WasmExternalKind>(*kind),
                .function_signature = std::nullopt,
            },
        .function_type = std::nullopt,
    };
    switch (imported.value.kind) {
      case WasmExternalKind::kFunction: {
        auto type_index = cursor.varUint32();
        if (!type_index) {
          return unexpected(type_index.error());
        }
        imported.function_type = *type_index;
        module->imported_function_types.push_back(*type_index);
        break;
      }
      case WasmExternalKind::kTable: {
        auto element_type = cursor.byte();
        if (!element_type || (*element_type != UINT8_C(0x70) && *element_type != UINT8_C(0x6F))) {
          return unexpected(std::string("invalid wasm table import"));
        }
        auto limits = readLimits(&cursor);
        if (!limits) {
          return unexpected(limits.error());
        }
        break;
      }
      case WasmExternalKind::kMemory: {
        auto limits = readLimits(&cursor);
        if (!limits) {
          return unexpected(limits.error());
        }
        module->imported_memories.push_back(*limits);
        break;
      }
      case WasmExternalKind::kGlobal: {
        auto value_type = cursor.byte();
        auto mutability = cursor.byte();
        if (!value_type || !mutability || !validValueType(*value_type) || *mutability > 1) {
          return unexpected(std::string("invalid wasm global import"));
        }
        break;
      }
      case WasmExternalKind::kTag: {
        auto attribute = cursor.varUint32();
        auto type_index = cursor.varUint32();
        if (!attribute || !type_index) {
          return unexpected(std::string("truncated wasm tag import"));
        }
        break;
      }
    }
    module->imports.push_back(std::move(imported));
  }
  return requireConsumed(cursor, "import");
}

[[nodiscard]] Expected<void> parseFunctionSection(Cursor cursor, ModuleBuilder* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  auto bounded = requireCountFits(*count, cursor.remaining(), 1, "function-section entry");
  if (!bounded) {
    return unexpected(bounded.error());
  }
  module->defined_function_types.reserve(*count);
  for (uint32_t index = 0; index < *count; ++index) {
    auto type_index = cursor.varUint32();
    if (!type_index) {
      return unexpected(type_index.error());
    }
    module->defined_function_types.push_back(*type_index);
  }
  return requireConsumed(cursor, "function");
}

[[nodiscard]] Expected<void> parseMemorySection(Cursor cursor, ModuleBuilder* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  auto bounded = requireCountFits(*count, cursor.remaining(), 2, "memory-section entry");
  if (!bounded) {
    return unexpected(bounded.error());
  }
  module->defined_memories.reserve(*count);
  for (uint32_t index = 0; index < *count; ++index) {
    auto limits = readLimits(&cursor);
    if (!limits) {
      return unexpected(limits.error());
    }
    module->defined_memories.push_back(*limits);
  }
  return requireConsumed(cursor, "memory");
}

[[nodiscard]] Expected<void> parseExportSection(Cursor cursor, ModuleBuilder* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  auto bounded = requireCountFits(*count, cursor.remaining(), 3, "export-section entry");
  if (!bounded) {
    return unexpected(bounded.error());
  }
  module->exports.reserve(*count);
  for (uint32_t index = 0; index < *count; ++index) {
    auto name = cursor.name();
    auto kind = cursor.byte();
    auto item_index = cursor.varUint32();
    if (!name || !kind || !item_index || *kind > static_cast<uint8_t>(WasmExternalKind::kTag)) {
      return unexpected(std::string("truncated or invalid wasm export entry"));
    }
    if (std::any_of(module->exports.begin(), module->exports.end(), [&](const WasmExport& item) {
          return item.name == *name;
        })) {
      return unexpected(std::string("duplicate wasm export name: ") + *name);
    }
    module->exports.push_back(
        WasmExport{
            .name = std::move(*name),
            .kind = static_cast<WasmExternalKind>(*kind),
            .index = *item_index,
            .function_signature = std::nullopt,
            .memory_limits = std::nullopt,
        });
  }
  return requireConsumed(cursor, "export");
}

[[nodiscard]] Expected<WasmModuleInfo> finishModule(ModuleBuilder builder) {
  std::vector<uint32_t> function_types;
  function_types.reserve(builder.imported_function_types.size() + builder.defined_function_types.size());
  function_types.insert(
      function_types.end(), builder.imported_function_types.begin(), builder.imported_function_types.end());
  function_types.insert(
      function_types.end(), builder.defined_function_types.begin(), builder.defined_function_types.end());
  for (const uint32_t type_index : function_types) {
    if (type_index >= builder.types.size()) {
      return unexpected(std::string("wasm function references an invalid type index"));
    }
  }

  WasmModuleInfo result;
  result.has_start_section = builder.has_start_section;
  result.section_count = builder.section_count;
  result.function_type_count = builder.types.size();
  result.function_count = function_types.size();
  result.memories.reserve(builder.imported_memories.size() + builder.defined_memories.size());
  result.memories.insert(result.memories.end(), builder.imported_memories.begin(), builder.imported_memories.end());
  result.memories.insert(result.memories.end(), builder.defined_memories.begin(), builder.defined_memories.end());
  result.imports.reserve(builder.imports.size());
  for (auto& imported : builder.imports) {
    if (imported.function_type.has_value()) {
      if (*imported.function_type >= builder.types.size()) {
        return unexpected(std::string("wasm function import references an invalid type index"));
      }
      imported.value.function_signature = builder.types[*imported.function_type];
    }
    result.imports.push_back(std::move(imported.value));
  }
  result.exports.reserve(builder.exports.size());
  for (auto& exported : builder.exports) {
    if (exported.kind == WasmExternalKind::kFunction) {
      if (exported.index >= function_types.size()) {
        return unexpected(std::string("wasm function export references an invalid function index"));
      }
      exported.function_signature = builder.types[function_types[exported.index]];
    } else if (exported.kind == WasmExternalKind::kMemory) {
      if (exported.index >= result.memories.size()) {
        return unexpected(std::string("wasm memory export references an invalid memory index"));
      }
      exported.memory_limits = result.memories[exported.index];
    }
    result.exports.push_back(std::move(exported));
  }
  return result;
}

[[nodiscard]] Expected<WasmModuleInfo> inspectWasmModuleImpl(Span<const uint8_t> wasm) {
  if (wasm.size() < kWasmPreamble.size() || !std::equal(kWasmPreamble.begin(), kWasmPreamble.end(), wasm.begin())) {
    return unexpected(std::string("invalid wasm preamble"));
  }

  ModuleBuilder module;
  Cursor cursor(wasm.subspan(kWasmPreamble.size()));
  std::array<bool, 13> seen{};
  while (!cursor.empty()) {
    auto section_id = cursor.byte();
    auto section_size = cursor.varUint32();
    if (!section_id || !section_size) {
      return unexpected(std::string("truncated wasm section header"));
    }
    if (*section_id > 12) {
      return unexpected(std::string("unknown wasm section id"));
    }
    auto section = cursor.take(*section_size);
    if (!section) {
      return unexpected(section.error());
    }
    ++module.section_count;
    if (*section_id != 0) {
      if (seen[*section_id]) {
        return unexpected(std::string("duplicate standard wasm section"));
      }
      seen[*section_id] = true;
    }

    Expected<void> parsed;
    switch (*section_id) {
      case 1:
        parsed = parseTypeSection(*section, &module);
        break;
      case 2:
        parsed = parseImportSection(*section, &module);
        break;
      case 3:
        parsed = parseFunctionSection(*section, &module);
        break;
      case 5:
        parsed = parseMemorySection(*section, &module);
        break;
      case 7:
        parsed = parseExportSection(*section, &module);
        break;
      case 8:
        module.has_start_section = true;
        break;
      default:
        break;
    }
    if (!parsed) {
      return unexpected(parsed.error());
    }
  }
  return finishModule(std::move(module));
}

struct ExpectedExport {
  std::string_view name;
  WasmFunctionSignature signature;
};

}  // namespace

const WasmExport* WasmModuleInfo::findExport(std::string_view name) const noexcept {
  const auto found =
      std::find_if(exports.begin(), exports.end(), [&](const WasmExport& item) { return item.name == name; });
  return found == exports.end() ? nullptr : &*found;
}

Expected<WasmModuleInfo> inspectWasmModule(Span<const uint8_t> wasm) {
  try {
    return inspectWasmModuleImpl(wasm);
  } catch (const std::bad_alloc&) {
    return unexpected(std::string("allocation failed while inspecting the wasm module"));
  } catch (...) {
    return unexpected(std::string("unexpected failure while inspecting the wasm module"));
  }
}

Expected<void> validateParserModuleWasmAbi(const WasmModuleInfo& module) {
  if (module.has_start_section) {
    return unexpected(std::string("wasm reactor contains a forbidden start section"));
  }
  if (module.findExport("_start") != nullptr) {
    return unexpected(std::string("wasm reactor exports forbidden _start"));
  }
  if (module.findExport(PJ_MODULE_MANIFEST_ADDR_EXPORT_NAME) != nullptr ||
      module.findExport(PJ_MODULE_MANIFEST_LEN_EXPORT_NAME) != nullptr) {
    return unexpected(std::string("wasm reactor exports native-only manifest metadata"));
  }

  const std::array<ExpectedExport, 9> expected{{
      {PJ_MODULE_ABI_EXPORT_NAME, {{}, {kWasmValueI32}}},
      {PJ_MODULE_CREATE_EXPORT_NAME, {{kWasmValueI32}, {kWasmValueI64}}},
      {PJ_MODULE_DESTROY_EXPORT_NAME, {{kWasmValueI64}, {}}},
      {PJ_MODULE_BIND_EXPORT_NAME, {{kWasmValueI64, kWasmValueI64, kWasmValueI64}, {kWasmValueI32}}},
      {PJ_MODULE_PARSE_EXPORT_NAME,
       {{kWasmValueI64, kWasmValueI64, kWasmValueI64, kWasmValueI64, kWasmValueI64}, {kWasmValueI32}}},
      {PJ_MODULE_LAST_ERROR_EXPORT_NAME, {{kWasmValueI64, kWasmValueI64, kWasmValueI64}, {kWasmValueI64}}},
      {PJ_MODULE_ALLOC_EXPORT_NAME, {{kWasmValueI64}, {kWasmValueI64}}},
      {PJ_MODULE_FREE_EXPORT_NAME, {{kWasmValueI64, kWasmValueI64}, {}}},
      {"_initialize", {{}, {}}},
  }};
  for (const auto& entry : expected) {
    const WasmExport* exported = module.findExport(entry.name);
    if (exported == nullptr) {
      return unexpected(std::string("missing function export: ") + std::string(entry.name));
    }
    if (exported->kind != WasmExternalKind::kFunction || !exported->function_signature.has_value()) {
      return unexpected(std::string("export is not a valid function: ") + std::string(entry.name));
    }
    if (*exported->function_signature != entry.signature) {
      return unexpected(std::string("function export has the wrong wasm signature: ") + std::string(entry.name));
    }
  }

  for (const auto& exported : module.exports) {
    if (exported.name.rfind("pj_module_", 0) != 0) {
      continue;
    }
    const auto found = std::find_if(
        expected.begin(), expected.end(), [&](const ExpectedExport& entry) { return entry.name == exported.name; });
    if (found == expected.end()) {
      return unexpected(std::string("unexpected parser-module export: ") + exported.name);
    }
  }
  return {};
}

Expected<uint64_t> validateParserModuleWasmMemory(const WasmModuleInfo& module, uint64_t maximum_bytes) {
  const WasmExport* memory_export = module.findExport("memory");
  if (memory_export == nullptr || memory_export->kind != WasmExternalKind::kMemory ||
      !memory_export->memory_limits.has_value()) {
    return unexpected(std::string("wasm parser module must export declared linear memory as 'memory'"));
  }
  if (module.memories.empty()) {
    return unexpected(std::string("wasm parser module declares no linear memory"));
  }

  uint64_t aggregate_maximum = 0;
  for (const auto& memory : module.memories) {
    if (!memory.maximum_pages.has_value()) {
      return unexpected(std::string("wasm parser-module memory has no declared maximum"));
    }
    const uint64_t pages = *memory.maximum_pages;
    if (pages > std::numeric_limits<uint64_t>::max() / kWasmMemoryPageBytes) {
      return unexpected(std::string("wasm parser-module memory maximum overflows bytes"));
    }
    const uint64_t bytes = pages * kWasmMemoryPageBytes;
    if (bytes > maximum_bytes) {
      return unexpected(
          "wasm parser-module memory maximum " + std::to_string(bytes) + " exceeds configured cap " +
          std::to_string(maximum_bytes));
    }
    if (bytes > std::numeric_limits<uint64_t>::max() - aggregate_maximum) {
      return unexpected(std::string("aggregate wasm parser-module memory maximum overflows bytes"));
    }
    aggregate_maximum += bytes;
  }
  return aggregate_maximum;
}

}  // namespace PJ::parser_module
