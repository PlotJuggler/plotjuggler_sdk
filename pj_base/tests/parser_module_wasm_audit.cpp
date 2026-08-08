// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/parser_module_manifest.hpp"

namespace {

using PJ::Expected;
using PJ::Span;
using PJ::unexpected;

constexpr std::array<uint8_t, 8> kWasmPreamble{0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
constexpr uint8_t kI32 = 0x7F;
constexpr uint8_t kI64 = 0x7E;

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

struct FunctionType {
  std::vector<uint8_t> parameters;
  std::vector<uint8_t> results;
};

struct Export {
  std::string name;
  uint8_t kind = 0;
  uint32_t index = 0;
};

struct ModuleInfo {
  std::vector<FunctionType> types;
  std::vector<uint32_t> function_types;
  std::vector<Export> exports;
  bool has_start_section = false;
  size_t section_count = 0;
};

[[nodiscard]] bool validValueType(uint8_t value) {
  switch (value) {
    case 0x7F:  // i32
    case 0x7E:  // i64
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

[[nodiscard]] Expected<std::vector<uint8_t>> readValueTypes(Cursor* cursor) {
  auto count = cursor->varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  try {
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
  } catch (const std::bad_alloc&) {
    return unexpected(std::string("allocation failed while reading wasm value types"));
  }
}

[[nodiscard]] Expected<void> requireConsumed(const Cursor& cursor, std::string_view section) {
  if (!cursor.empty()) {
    return unexpected(std::string(section) + " section contains trailing bytes");
  }
  return {};
}

[[nodiscard]] Expected<void> parseTypeSection(Cursor cursor, ModuleInfo* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  try {
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
      module->types.push_back(FunctionType{std::move(*parameters), std::move(*results)});
    }
  } catch (const std::bad_alloc&) {
    return unexpected(std::string("allocation failed while reading the wasm type section"));
  }
  return requireConsumed(cursor, "type");
}

[[nodiscard]] Expected<void> readLimits(Cursor* cursor) {
  auto flags = cursor->varUint32();
  auto minimum = cursor->varUint32();
  if (!flags || !minimum) {
    return unexpected(std::string("truncated wasm limits"));
  }
  if (*flags > 1) {
    return unexpected(std::string("unsupported wasm limits flags"));
  }
  if ((*flags & 1U) != 0) {
    auto maximum = cursor->varUint32();
    if (!maximum) {
      return unexpected(maximum.error());
    }
  }
  return {};
}

[[nodiscard]] Expected<void> parseImportSection(Cursor cursor, ModuleInfo* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  for (uint32_t index = 0; index < *count; ++index) {
    auto module_name = cursor.name();
    auto field_name = cursor.name();
    auto kind = cursor.byte();
    if (!module_name || !field_name || !kind) {
      return unexpected(std::string("truncated wasm import entry"));
    }
    switch (*kind) {
      case 0: {
        auto type_index = cursor.varUint32();
        if (!type_index) {
          return unexpected(type_index.error());
        }
        module->function_types.push_back(*type_index);
        break;
      }
      case 1: {
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
      case 2: {
        auto limits = readLimits(&cursor);
        if (!limits) {
          return unexpected(limits.error());
        }
        break;
      }
      case 3: {
        auto value_type = cursor.byte();
        auto mutability = cursor.byte();
        if (!value_type || !mutability || !validValueType(*value_type) || *mutability > 1) {
          return unexpected(std::string("invalid wasm global import"));
        }
        break;
      }
      case 4: {
        auto attribute = cursor.varUint32();
        auto type_index = cursor.varUint32();
        if (!attribute || !type_index) {
          return unexpected(std::string("truncated wasm tag import"));
        }
        break;
      }
      default:
        return unexpected(std::string("unknown wasm import kind"));
    }
  }
  return requireConsumed(cursor, "import");
}

[[nodiscard]] Expected<void> parseFunctionSection(Cursor cursor, ModuleInfo* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  try {
    module->function_types.reserve(module->function_types.size() + *count);
    for (uint32_t index = 0; index < *count; ++index) {
      auto type_index = cursor.varUint32();
      if (!type_index) {
        return unexpected(type_index.error());
      }
      module->function_types.push_back(*type_index);
    }
  } catch (const std::bad_alloc&) {
    return unexpected(std::string("allocation failed while reading the wasm function section"));
  }
  return requireConsumed(cursor, "function");
}

[[nodiscard]] Expected<void> parseExportSection(Cursor cursor, ModuleInfo* module) {
  auto count = cursor.varUint32();
  if (!count) {
    return unexpected(count.error());
  }
  try {
    module->exports.reserve(*count);
    for (uint32_t index = 0; index < *count; ++index) {
      auto name = cursor.name();
      auto kind = cursor.byte();
      auto item_index = cursor.varUint32();
      if (!name || !kind || !item_index) {
        return unexpected(std::string("truncated wasm export entry"));
      }
      if (std::any_of(
              module->exports.begin(), module->exports.end(), [&](const Export& item) { return item.name == *name; })) {
        return unexpected(std::string("duplicate wasm export name: ") + *name);
      }
      module->exports.push_back(Export{std::move(*name), *kind, *item_index});
    }
  } catch (const std::bad_alloc&) {
    return unexpected(std::string("allocation failed while reading the wasm export section"));
  }
  return requireConsumed(cursor, "export");
}

[[nodiscard]] Expected<ModuleInfo> inspectModule(Span<const uint8_t> wasm) {
  if (wasm.size() < kWasmPreamble.size() || !std::equal(kWasmPreamble.begin(), kWasmPreamble.end(), wasm.begin())) {
    return unexpected(std::string("invalid wasm preamble"));
  }

  ModuleInfo module;
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
  if (!seen[1] || !seen[3] || !seen[7]) {
    return unexpected(std::string("wasm is missing a type, function, or export section"));
  }
  for (const uint32_t type_index : module.function_types) {
    if (type_index >= module.types.size()) {
      return unexpected(std::string("wasm function references an invalid type index"));
    }
  }
  return module;
}

[[nodiscard]] Expected<std::vector<uint8_t>> readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return unexpected(std::string("cannot open file: ") + path);
  }
  const std::streamoff end = input.tellg();
  if (end < 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max() ||
      end > std::numeric_limits<std::streamsize>::max()) {
    return unexpected(std::string("file size is invalid: ") + path);
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  input.seekg(0);
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!input) {
    return unexpected(std::string("cannot read complete file: ") + path);
  }
  return bytes;
}

[[nodiscard]] Expected<void> writeFile(const std::string& path, Span<const uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return unexpected(std::string("cannot create file: ") + path);
  }
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!output) {
    return unexpected(std::string("cannot write complete file: ") + path);
  }
  return {};
}

[[nodiscard]] const Export* findExport(const ModuleInfo& module, std::string_view name) {
  const Export* result = nullptr;
  for (const auto& item : module.exports) {
    if (item.name == name) {
      if (result != nullptr) {
        return nullptr;
      }
      result = &item;
    }
  }
  return result;
}

/// One mandatory reactor export and its exact wasm function signature.
struct ExpectedExport {
  std::string_view name;
  std::vector<uint8_t> parameters;
  std::vector<uint8_t> results;
};

[[nodiscard]] Expected<void> requireFunction(
    const ModuleInfo& module, std::string_view name, const std::vector<uint8_t>& parameters,
    const std::vector<uint8_t>& results) {
  const Export* item = findExport(module, name);
  if (item == nullptr) {
    return unexpected(std::string("missing or duplicate function export: ") + std::string(name));
  }
  if (item->kind != 0 || item->index >= module.function_types.size()) {
    return unexpected(std::string("export is not a valid function: ") + std::string(name));
  }
  const FunctionType& type = module.types[module.function_types[item->index]];
  if (type.parameters != parameters || type.results != results) {
    return unexpected(std::string("function export has the wrong wasm signature: ") + std::string(name));
  }
  return {};
}

[[nodiscard]] Expected<void> audit(const std::string& wasm_path, const std::string& manifest_path) {
  auto wasm = readFile(wasm_path);
  auto manifest = readFile(manifest_path);
  if (!wasm) {
    return unexpected(wasm.error());
  }
  if (!manifest) {
    return unexpected(manifest.error());
  }
  auto embedded = PJ::parser_module::readManifestSection(*wasm);
  if (!embedded) {
    return unexpected(embedded.error());
  }
  if (embedded->size() != manifest->size() || !std::equal(embedded->begin(), embedded->end(), manifest->begin())) {
    return unexpected(std::string("embedded parser-module manifest bytes do not match the source file"));
  }

  auto module = inspectModule(*wasm);
  if (!module) {
    return unexpected(module.error());
  }
  if (module->has_start_section) {
    return unexpected(std::string("wasm reactor contains a forbidden start section"));
  }
  if (findExport(*module, "_start") != nullptr) {
    return unexpected(std::string("wasm reactor exports forbidden _start"));
  }
  if (findExport(*module, PJ_MODULE_MANIFEST_ADDR_EXPORT_NAME) != nullptr ||
      findExport(*module, PJ_MODULE_MANIFEST_LEN_EXPORT_NAME) != nullptr) {
    return unexpected(std::string("wasm reactor exports native-only manifest metadata"));
  }

  const std::array<ExpectedExport, 9> expected{{
      {PJ_MODULE_ABI_EXPORT_NAME, {}, {kI32}},
      {PJ_MODULE_CREATE_EXPORT_NAME, {kI32}, {kI64}},
      {PJ_MODULE_DESTROY_EXPORT_NAME, {kI64}, {}},
      {PJ_MODULE_BIND_EXPORT_NAME, {kI64, kI64, kI64}, {kI32}},
      {PJ_MODULE_PARSE_EXPORT_NAME, {kI64, kI64, kI64, kI64, kI64}, {kI32}},
      {PJ_MODULE_LAST_ERROR_EXPORT_NAME, {kI64, kI64, kI64}, {kI64}},
      {PJ_MODULE_ALLOC_EXPORT_NAME, {kI64}, {kI64}},
      {PJ_MODULE_FREE_EXPORT_NAME, {kI64, kI64}, {}},
      {"_initialize", {}, {}},
  }};
  for (const auto& entry : expected) {
    auto valid = requireFunction(*module, entry.name, entry.parameters, entry.results);
    if (!valid) {
      return unexpected(valid.error());
    }
  }

  for (const auto& item : module->exports) {
    if (item.name.rfind("pj_module_", 0) != 0) {
      continue;
    }
    const auto found = std::find_if(
        expected.begin(), expected.end(), [&](const ExpectedExport& entry) { return entry.name == item.name; });
    if (found == expected.end()) {
      return unexpected(std::string("unexpected parser-module export: ") + item.name);
    }
  }

  std::cout << "WASM parser-module ABI conformance: PASS\n"
            << "  sections enumerated: " << module->section_count << '\n'
            << "  function types: " << module->types.size() << ", functions: " << module->function_types.size()
            << ", exports: " << module->exports.size() << '\n'
            << "  operational exports: 8 exact signatures verified\n"
            << "  reactor: _initialize exported; _start/start section absent\n"
            << "  native-only metadata exports: absent\n"
            << "  manifest section: exactly one, " << embedded->size() << " exact bytes\n";
  return {};
}

[[nodiscard]] Expected<void> embed(
    const std::string& input_path, const std::string& manifest_path, const std::string& output_path) {
  auto wasm = readFile(input_path);
  auto manifest = readFile(manifest_path);
  if (!wasm) {
    return unexpected(wasm.error());
  }
  if (!manifest) {
    return unexpected(manifest.error());
  }
  auto output = PJ::parser_module::appendManifestSection(*wasm, *manifest);
  if (!output) {
    return unexpected(output.error());
  }
  return writeFile(output_path, *output);
}

}  // namespace

int main(int argc, char** argv) {
  Expected<void> result = unexpected(std::string("invalid arguments"));
  if (argc == 5 && std::string_view(argv[1]) == "--embed") {
    result = embed(argv[2], argv[3], argv[4]);
  } else if (argc == 4 && std::string_view(argv[1]) == "--audit") {
    result = audit(argv[2], argv[3]);
  } else {
    std::cerr << "usage: parser_module_wasm_audit --embed INPUT.wasm MANIFEST.json OUTPUT.wasm\n"
                 "   or: parser_module_wasm_audit --audit MODULE.wasm MANIFEST.json\n";
    return 2;
  }
  if (!result) {
    std::cerr << "parser-module wasm audit failed: " << result.error() << '\n';
    return 1;
  }
  return 0;
}
