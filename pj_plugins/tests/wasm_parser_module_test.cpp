// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/wasm_parser_module.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/builtin_object_abi.h"
#include "pj_base/parser_module_abi.h"
#include "pj_base/parser_module_manifest.hpp"
#include "pj_base/parser_module_wasm.hpp"
#include "pj_plugins/host/parser_claim_catalog.hpp"
#include "pj_plugins/host/wasm_parser_module_runtime.hpp"

namespace PJ {
namespace {

constexpr std::string_view kSchema = "uint32 width\nstring frame_id\nuint8[] data\n";

Span<const uint8_t> bytes(std::string_view text) {
  return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

std::vector<uint8_t> readFile(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary | std::ios::ate);
  EXPECT_TRUE(input.good());
  const std::streamoff size = input.tellg();
  EXPECT_GT(size, 0);
  std::vector<uint8_t> result(static_cast<size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
  EXPECT_TRUE(input.good());
  return result;
}

std::optional<uint32_t> readVarUint32(const std::vector<uint8_t>& wasm, size_t* position) {
  uint32_t value = 0;
  for (size_t index = 0; index < 5; ++index) {
    if (*position >= wasm.size()) {
      return std::nullopt;
    }
    const uint8_t next = wasm[(*position)++];
    if (index == 4 && (next & UINT8_C(0xF0)) != 0) {
      return std::nullopt;
    }
    value |= static_cast<uint32_t>(next & UINT8_C(0x7F)) << (index * 7U);
    if ((next & UINT8_C(0x80)) == 0) {
      return value;
    }
  }
  return std::nullopt;
}

std::vector<uint8_t> encodeVarUint32(uint32_t value) {
  std::vector<uint8_t> output;
  do {
    uint8_t next = static_cast<uint8_t>(value & UINT32_C(0x7F));
    value >>= 7U;
    if (value != 0) {
      next |= UINT8_C(0x80);
    }
    output.push_back(next);
  } while (value != 0);
  return output;
}

template <typename Bytes>
void append(std::vector<uint8_t>* output, const Bytes& bytes) {
  output->insert(output->end(), std::begin(bytes), std::end(bytes));
}

/// Encode one wasm section: id byte, varuint32 payload size, payload.
std::vector<uint8_t> encodeSection(uint8_t id, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> section{id};
  append(&section, encodeVarUint32(static_cast<uint32_t>(payload.size())));
  append(&section, payload);
  return section;
}

struct SectionRange {
  uint8_t id = 0;
  size_t begin = 0;
  size_t size_begin = 0;
  size_t size_end = 0;
  size_t payload_begin = 0;
  size_t end = 0;
};

std::vector<SectionRange> sections(const std::vector<uint8_t>& wasm) {
  std::vector<SectionRange> result;
  size_t position = 8;
  while (position < wasm.size()) {
    const size_t begin = position;
    const uint8_t id = wasm[position++];
    const size_t size_begin = position;
    auto size = readVarUint32(wasm, &position);
    if (!size || static_cast<size_t>(*size) > wasm.size() - position) {
      ADD_FAILURE() << "malformed wasm fixture section";
      return {};
    }
    const size_t end = position + *size;
    result.push_back(
        SectionRange{
            .id = id,
            .begin = begin,
            .size_begin = size_begin,
            .size_end = position,
            .payload_begin = position,
            .end = end,
        });
    position = end;
  }
  return result;
}

std::optional<SectionRange> findSection(const std::vector<uint8_t>& wasm, uint8_t id) {
  const auto ranges = sections(wasm);
  const auto found =
      std::find_if(ranges.begin(), ranges.end(), [&](const SectionRange& section) { return section.id == id; });
  return found == ranges.end() ? std::nullopt : std::optional<SectionRange>(*found);
}

std::optional<SectionRange> findManifestSection(const std::vector<uint8_t>& wasm) {
  for (const auto& section : sections(wasm)) {
    if (section.id != 0) {
      continue;
    }
    size_t position = section.payload_begin;
    auto name_length = readVarUint32(wasm, &position);
    if (!name_length || static_cast<size_t>(*name_length) > section.end - position) {
      return std::nullopt;
    }
    const std::string_view name(
        reinterpret_cast<const char*>(wasm.data() + position), static_cast<size_t>(*name_length));
    if (name == PJ_PARSER_MODULE_MANIFEST_SECTION_NAME) {
      return section;
    }
  }
  return std::nullopt;
}

struct ExportLocation {
  std::string name;
  size_t name_begin = 0;
  uint8_t kind = 0;
  uint32_t index = 0;
  size_t index_begin = 0;
  size_t index_end = 0;
};

std::vector<ExportLocation> exportLocations(const std::vector<uint8_t>& wasm) {
  const auto section = findSection(wasm, 7);
  if (!section) {
    return {};
  }
  size_t position = section->payload_begin;
  auto count = readVarUint32(wasm, &position);
  if (!count) {
    return {};
  }
  std::vector<ExportLocation> result;
  for (uint32_t index = 0; index < *count; ++index) {
    auto name_length = readVarUint32(wasm, &position);
    if (!name_length || static_cast<size_t>(*name_length) > section->end - position) {
      return {};
    }
    const size_t name_begin = position;
    std::string name(reinterpret_cast<const char*>(wasm.data() + position), static_cast<size_t>(*name_length));
    position += *name_length;
    if (position >= section->end) {
      return {};
    }
    const uint8_t kind = wasm[position++];
    const size_t item_begin = position;
    auto item_index = readVarUint32(wasm, &position);
    if (!item_index) {
      return {};
    }
    result.push_back(
        ExportLocation{
            .name = std::move(name),
            .name_begin = name_begin,
            .kind = kind,
            .index = *item_index,
            .index_begin = item_begin,
            .index_end = position,
        });
  }
  return result;
}

std::optional<ExportLocation> findExportLocation(const std::vector<uint8_t>& wasm, std::string_view name) {
  const auto exports = exportLocations(wasm);
  const auto found =
      std::find_if(exports.begin(), exports.end(), [&](const ExportLocation& item) { return item.name == name; });
  return found == exports.end() ? std::nullopt : std::optional<ExportLocation>(*found);
}

void insertSectionAfter(std::vector<uint8_t>* wasm, uint8_t preceding_id, const std::vector<uint8_t>& section) {
  const auto preceding = findSection(*wasm, preceding_id);
  ASSERT_TRUE(preceding.has_value());
  wasm->insert(wasm->begin() + static_cast<ptrdiff_t>(preceding->end), section.begin(), section.end());
}

std::vector<uint8_t> withoutManifest(std::vector<uint8_t> wasm) {
  const auto manifest = findManifestSection(wasm);
  EXPECT_TRUE(manifest.has_value());
  if (manifest) {
    wasm.erase(
        wasm.begin() + static_cast<ptrdiff_t>(manifest->begin), wasm.begin() + static_cast<ptrdiff_t>(manifest->end));
  }
  return wasm;
}

std::vector<uint8_t> withDuplicateManifest(std::vector<uint8_t> wasm) {
  const auto manifest = findManifestSection(wasm);
  EXPECT_TRUE(manifest.has_value());
  if (manifest) {
    const std::vector<uint8_t> duplicate(
        wasm.begin() + static_cast<ptrdiff_t>(manifest->begin), wasm.begin() + static_cast<ptrdiff_t>(manifest->end));
    wasm.insert(wasm.end(), duplicate.begin(), duplicate.end());
  }
  return wasm;
}

std::vector<uint8_t> withMalformedManifest(std::vector<uint8_t> wasm) {
  wasm = withoutManifest(std::move(wasm));
  constexpr std::string_view kMalformed = "{";
  auto embedded = parser_module::appendManifestSection(wasm, bytes(kMalformed));
  EXPECT_TRUE(embedded.has_value()) << embedded.error();
  return embedded ? std::move(*embedded) : std::move(wasm);
}

std::vector<uint8_t> withStartSection(std::vector<uint8_t> wasm) {
  const auto initialize = findExportLocation(wasm, "_initialize");
  EXPECT_TRUE(initialize.has_value());
  if (initialize) {
    insertSectionAfter(&wasm, 7, encodeSection(8, encodeVarUint32(initialize->index)));
  }
  return wasm;
}

std::vector<uint8_t> withStartExport(std::vector<uint8_t> wasm) {
  const auto memory = findExportLocation(wasm, "memory");
  EXPECT_TRUE(memory.has_value());
  if (memory) {
    constexpr std::string_view kStart = "_start";
    static_assert(kStart.size() == 6);
    std::copy(kStart.begin(), kStart.end(), wasm.begin() + static_cast<ptrdiff_t>(memory->name_begin));
  }
  return wasm;
}

std::vector<uint8_t> withoutOperationalExport(std::vector<uint8_t> wasm) {
  const auto exported = findExportLocation(wasm, PJ_MODULE_FREE_EXPORT_NAME);
  EXPECT_TRUE(exported.has_value());
  if (exported) {
    wasm[exported->name_begin] = 'x';
  }
  return wasm;
}

std::vector<uint8_t> withWrongExportSignature(std::vector<uint8_t> wasm) {
  const auto abi = findExportLocation(wasm, PJ_MODULE_ABI_EXPORT_NAME);
  const auto create = findExportLocation(wasm, PJ_MODULE_CREATE_EXPORT_NAME);
  EXPECT_TRUE(abi.has_value());
  EXPECT_TRUE(create.has_value());
  if (abi && create) {
    const auto encoded_index = encodeVarUint32(create->index);
    EXPECT_EQ(encoded_index.size(), abi->index_end - abi->index_begin);
    if (encoded_index.size() == abi->index_end - abi->index_begin) {
      std::copy(encoded_index.begin(), encoded_index.end(), wasm.begin() + static_cast<ptrdiff_t>(abi->index_begin));
    }
  }
  return wasm;
}

std::vector<uint8_t> withDisallowedImport(std::vector<uint8_t> wasm) {
  constexpr std::string_view kModule = "wasi_snapshot_preview1";
  constexpr std::string_view kName = "fd_write";
  std::vector<uint8_t> payload{1, static_cast<uint8_t>(kModule.size())};
  append(&payload, kModule);
  payload.push_back(static_cast<uint8_t>(kName.size()));
  append(&payload, kName);
  payload.push_back(0);  // function import
  payload.push_back(0);  // function type zero
  insertSectionAfter(&wasm, 1, encodeSection(2, payload));
  return wasm;
}

std::vector<uint8_t> withoutMemoryMaximum(std::vector<uint8_t> wasm) {
  const auto memory = findSection(wasm, 5);
  EXPECT_TRUE(memory.has_value());
  if (!memory) {
    return wasm;
  }
  size_t position = memory->payload_begin;
  const auto count = readVarUint32(wasm, &position);
  const auto flags = readVarUint32(wasm, &position);
  const auto minimum = readVarUint32(wasm, &position);
  const auto maximum = readVarUint32(wasm, &position);
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(flags, 1U);
  EXPECT_TRUE(minimum.has_value());
  EXPECT_TRUE(maximum.has_value());
  EXPECT_EQ(position, memory->end);
  if (!count || *count != 1 || !flags || *flags != 1 || !minimum || !maximum || position != memory->end) {
    return wasm;
  }

  std::vector<uint8_t> payload{1, 0};
  append(&payload, encodeVarUint32(*minimum));
  const auto replacement = encodeSection(5, payload);
  wasm.erase(wasm.begin() + static_cast<ptrdiff_t>(memory->begin), wasm.begin() + static_cast<ptrdiff_t>(memory->end));
  wasm.insert(wasm.begin() + static_cast<ptrdiff_t>(memory->begin), replacement.begin(), replacement.end());
  return wasm;
}

/// Rewrite the fixture's single funcref table limits. A null maximum drops
/// the maximum flag entirely.
std::vector<uint8_t> withTableLimits(std::vector<uint8_t> wasm, std::optional<uint32_t> maximum) {
  const auto table = findSection(wasm, 4);
  EXPECT_TRUE(table.has_value());
  if (!table) {
    return wasm;
  }
  size_t position = table->payload_begin;
  const auto count = readVarUint32(wasm, &position);
  const uint8_t reference_type = wasm[position++];
  const auto flags = readVarUint32(wasm, &position);
  const auto minimum = readVarUint32(wasm, &position);
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(reference_type, 0x70);
  EXPECT_EQ(flags, 1U);
  EXPECT_TRUE(minimum.has_value());
  if (!count || *count != 1 || !flags || *flags != 1 || !minimum) {
    return wasm;
  }

  std::vector<uint8_t> payload{1, 0x70, static_cast<uint8_t>(maximum ? 1 : 0)};
  append(&payload, encodeVarUint32(*minimum));
  if (maximum) {
    append(&payload, encodeVarUint32(std::max(*maximum, *minimum)));
  }
  const auto replacement = encodeSection(4, payload);
  wasm.erase(wasm.begin() + static_cast<ptrdiff_t>(table->begin), wasm.begin() + static_cast<ptrdiff_t>(table->end));
  wasm.insert(wasm.begin() + static_cast<ptrdiff_t>(table->begin), replacement.begin(), replacement.end());
  return wasm;
}

/// Replace the embedded manifest with one declaring three claims while the
/// compiled module still knows two, so the guest's own creation-error path
/// (token zero) is reachable through a claim index the host accepts.
std::vector<uint8_t> withThreeClaimManifest(std::vector<uint8_t> wasm) {
  wasm = withoutManifest(std::move(wasm));
  constexpr std::string_view kManifest = R"({
  "module_abi": 1,
  "id": "org.plotjuggler.test.kit-cdr-pointcloud",
  "name": "Authoring kit CDR PointCloud fixture",
  "version": "1.0.0",
  "claims": [
    {"claim_id": "full-wire", "encoding": "ros2msg", "type_name": "toy_msgs/msg/Cloud",
     "routes": ["object"], "object_type": "kPointCloud", "priority": 0},
    {"claim_id": "spliced", "encoding": "ros2msg", "type_name": "toy_msgs/msg/CloudSplice",
     "routes": ["object"], "object_type": "kPointCloud", "priority": 0},
    {"claim_id": "phantom", "encoding": "ros2msg", "type_name": "toy_msgs/msg/Phantom",
     "routes": ["object"], "object_type": "kPointCloud", "priority": 0}
  ]
})";
  auto embedded = parser_module::appendManifestSection(wasm, bytes(kManifest));
  EXPECT_TRUE(embedded.has_value()) << embedded.error();
  return embedded ? std::move(*embedded) : std::move(wasm);
}

std::vector<uint8_t> withFunctionFirstOpcode(std::vector<uint8_t> wasm, std::string_view export_name, uint8_t opcode) {
  const auto function = findExportLocation(wasm, export_name);
  const auto code = findSection(wasm, 10);
  EXPECT_TRUE(function.has_value());
  EXPECT_TRUE(code.has_value());
  if (!function || !code) {
    return wasm;
  }
  size_t position = code->payload_begin;
  auto function_count = readVarUint32(wasm, &position);
  EXPECT_TRUE(function_count.has_value());
  if (!function_count || function->index >= *function_count) {
    ADD_FAILURE() << export_name << " export does not identify a defined fixture function";
    return wasm;
  }
  for (uint32_t function_index = 0; function_index < *function_count; ++function_index) {
    auto body_size = readVarUint32(wasm, &position);
    if (!body_size || static_cast<size_t>(*body_size) > code->end - position) {
      ADD_FAILURE() << "malformed fixture function body";
      return wasm;
    }
    const size_t body_end = position + *body_size;
    auto local_group_count = readVarUint32(wasm, &position);
    if (!local_group_count) {
      return wasm;
    }
    for (uint32_t local = 0; local < *local_group_count; ++local) {
      auto count = readVarUint32(wasm, &position);
      if (!count || position >= body_end) {
        return wasm;
      }
      ++position;  // local value type
    }
    if (function_index == function->index) {
      EXPECT_LT(position, body_end);
      if (position < body_end) {
        wasm[position] = opcode;
      }
      return wasm;
    }
    position = body_end;
  }
  return wasm;
}

std::vector<uint8_t> withInsertedFunctionFirstOpcode(
    std::vector<uint8_t> wasm, std::string_view export_name, uint8_t opcode) {
  const auto function = findExportLocation(wasm, export_name);
  const auto code = findSection(wasm, 10);
  EXPECT_TRUE(function.has_value());
  EXPECT_TRUE(code.has_value());
  if (!function || !code) {
    return wasm;
  }

  std::vector<uint8_t> payload(
      wasm.begin() + static_cast<ptrdiff_t>(code->payload_begin), wasm.begin() + static_cast<ptrdiff_t>(code->end));
  size_t position = 0;
  const auto function_count = readVarUint32(payload, &position);
  EXPECT_TRUE(function_count.has_value());
  if (!function_count || function->index >= *function_count) {
    ADD_FAILURE() << export_name << " export does not identify a defined fixture function";
    return wasm;
  }

  for (uint32_t function_index = 0; function_index < *function_count; ++function_index) {
    const size_t body_size_begin = position;
    const auto body_size = readVarUint32(payload, &position);
    const size_t body_size_end = position;
    if (!body_size || static_cast<size_t>(*body_size) > payload.size() - position) {
      ADD_FAILURE() << "malformed fixture function body";
      return wasm;
    }
    const size_t body_end = position + *body_size;
    const auto local_group_count = readVarUint32(payload, &position);
    if (!local_group_count) {
      return wasm;
    }
    for (uint32_t local = 0; local < *local_group_count; ++local) {
      const auto count = readVarUint32(payload, &position);
      if (!count || position >= body_end) {
        return wasm;
      }
      ++position;  // local value type
    }
    if (function_index == function->index) {
      const auto encoded_body_size = encodeVarUint32(*body_size + 1U);
      payload.erase(
          payload.begin() + static_cast<ptrdiff_t>(body_size_begin),
          payload.begin() + static_cast<ptrdiff_t>(body_size_end));
      payload.insert(
          payload.begin() + static_cast<ptrdiff_t>(body_size_begin), encoded_body_size.begin(),
          encoded_body_size.end());
      const size_t adjusted_position = position - (body_size_end - body_size_begin) + encoded_body_size.size();
      payload.insert(payload.begin() + static_cast<ptrdiff_t>(adjusted_position), opcode);

      const auto replacement = encodeSection(10, payload);
      wasm.erase(wasm.begin() + static_cast<ptrdiff_t>(code->begin), wasm.begin() + static_cast<ptrdiff_t>(code->end));
      wasm.insert(wasm.begin() + static_cast<ptrdiff_t>(code->begin), replacement.begin(), replacement.end());
      return wasm;
    }
    position = body_end;
  }
  return wasm;
}

std::vector<uint8_t> withTrappingParse(std::vector<uint8_t> wasm) {
  return withFunctionFirstOpcode(std::move(wasm), PJ_MODULE_PARSE_EXPORT_NAME, 0x00);  // unreachable
}

std::vector<uint8_t> withTrappingCreate(std::vector<uint8_t> wasm) {
  return withFunctionFirstOpcode(std::move(wasm), PJ_MODULE_CREATE_EXPORT_NAME, 0x00);  // unreachable
}

std::vector<uint8_t> withTrappingDestroy(std::vector<uint8_t> wasm) {
  return withFunctionFirstOpcode(std::move(wasm), PJ_MODULE_DESTROY_EXPORT_NAME, 0x00);  // unreachable
}

std::vector<uint8_t> withTrappingFree(std::vector<uint8_t> wasm) {
  return withInsertedFunctionFirstOpcode(std::move(wasm), PJ_MODULE_FREE_EXPORT_NAME, 0x00);  // unreachable
}

std::vector<uint8_t> withInvalidParseOpcode(std::vector<uint8_t> wasm) {
  return withFunctionFirstOpcode(std::move(wasm), PJ_MODULE_PARSE_EXPORT_NAME, 0xFF);
}

class TemporaryWasm {
 public:
  explicit TemporaryWasm(const std::vector<uint8_t>& bytes) {
    static std::atomic<uint64_t> sequence{0};
    const uint64_t nonce = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = std::filesystem::temp_directory_path() /
            ("pj-wasm-parser-module-" + std::to_string(nonce) + "-" + std::to_string(sequence.fetch_add(1)) + ".wasm");
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    EXPECT_TRUE(output.good());
  }

  ~TemporaryWasm() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  [[nodiscard]] std::string string() const {
    return path_.string();
  }

 private:
  std::filesystem::path path_;
};

void appendU32(std::vector<uint8_t>& output, uint32_t value) {
  const size_t relative = output.size() - 4;
  output.insert(output.end(), (4 - (relative % 4)) % 4, 0);
  for (size_t index = 0; index < 4; ++index) {
    output.push_back(static_cast<uint8_t>(value >> (index * 8U)));
  }
}

std::vector<uint8_t> toyPayload() {
  std::vector<uint8_t> output{0, 1, 0, 0};
  appendU32(output, 2);
  appendU32(output, 4);
  output.insert(output.end(), {'m', 'a', 'p', 0});
  appendU32(output, 8);
  output.insert(output.end(), {1, 2, 3, 4, 5, 6, 7, 8});
  return output;
}

WasmParserModuleLoadOptions collectingInto(std::vector<Diagnostic>* diagnostics) {
  WasmParserModuleLoadOptions options;
  options.sink = [diagnostics](const Diagnostic& diagnostic) { diagnostics->push_back(diagnostic); };
  return options;
}

parser_module::BindingInfoV1 binding(uint32_t claim_index, std::string_view schema) {
  return parser_module::BindingInfoV1{
      .route = parser_module::Route::kObject,
      .claim_index = claim_index,
      .expected_object_type = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
      .encoding = bytes("ros2msg"),
      .type_name = bytes(claim_index == 0 ? "toy_msgs/msg/Cloud" : "toy_msgs/msg/CloudSplice"),
      .schema = bytes(schema),
      .claim_id = bytes(claim_index == 0 ? "full-wire" : "spliced"),
      .config_json = bytes("{}"),
      .schema_digest = {},
  };
}

TEST(WasmParserModule, LoadsValidatesAndAdmitsManifestWithoutInstantiation) {
  std::vector<Diagnostic> diagnostics;
  auto module = WasmParserModule::load(PJ_TOY_CDR_POINTCLOUD_WASM_PATH, collectingInto(&diagnostics));
  ASSERT_TRUE(module.has_value()) << module.error();
  EXPECT_TRUE(module->valid());
  EXPECT_TRUE(diagnostics.empty());
  EXPECT_EQ(module->declaredLinearMemoryMaximum(), UINT64_C(256) * 1024U * 1024U);
  EXPECT_GT(module->declaredTableElements(), 0U);
  EXPECT_LE(module->declaredTableElements(), parser_module::ParserModuleWasmLimits::kDefaultMaximumTableElements);

  ParserClaimCatalog catalog;
  auto manifest = catalog.ingestModuleManifest(module->manifestJson(), ParserClaimProvenance::kFolderDrop, 31);
  ASSERT_TRUE(manifest.has_value()) << manifest.error();
  EXPECT_EQ(manifest->id, "org.plotjuggler.test.kit-cdr-pointcloud");
  EXPECT_EQ(manifest->claims.size(), 2U);
}

TEST(WasmParserModule, RejectsLoaderViolationsWithOneDiagnostic) {
  const auto valid = readFile(PJ_TOY_CDR_POINTCLOUD_WASM_PATH);
  const std::vector<std::pair<std::vector<uint8_t>, std::string>> cases{
      {withoutManifest(valid), "no parser-module manifest"},
      {withDuplicateManifest(valid), "multiple parser-module manifest"},
      {withMalformedManifest(valid), "manifest is invalid JSON"},
      {withStartSection(valid), "forbidden start section"},
      {withStartExport(valid), "forbidden _start"},
      {withoutOperationalExport(valid), PJ_MODULE_FREE_EXPORT_NAME},
      {withWrongExportSignature(valid), "wrong wasm signature"},
      {withDisallowedImport(valid), "wasi_snapshot_preview1.fd_write"},
      {withoutMemoryMaximum(valid), "memory has no declared maximum"},
      {withTableLimits(valid, std::nullopt), "table has no declared maximum"},
      {withTableLimits(valid, UINT32_C(1) << 20U), "table maximum 1048576 exceeds configured cap"},
      {withInvalidParseOpcode(valid), "Wasmer rejected parser module"},
  };

  for (const auto& [artifact, expected] : cases) {
    TemporaryWasm file(artifact);
    std::vector<Diagnostic> diagnostics;
    auto module = WasmParserModule::load(file.string(), collectingInto(&diagnostics));
    EXPECT_FALSE(module.has_value()) << expected;
    ASSERT_EQ(diagnostics.size(), 1U) << expected;
    EXPECT_EQ(diagnostics.front().level, DiagnosticLevel::kError);
    EXPECT_EQ(diagnostics.front().message, module.error());
    EXPECT_NE(module.error().find(expected), std::string::npos) << module.error();
  }
}

TEST(WasmParserModule, RunsFullWireAndSplicedPointCloudLifecycles) {
  auto module = WasmParserModule::load(PJ_TOY_CDR_POINTCLOUD_WASM_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  const auto payload = toyPayload();
  const parser_module::ParseInputV1 input{
      .has_timestamp = false,
      .timestamp_ns = 0,
      .payload = payload,
  };

  auto full = WasmParserModuleInstance::create(*module, 0);
  ASSERT_TRUE(full.has_value()) << full.error().message;
  auto full_bind = full->bind(binding(0, kSchema));
  ASSERT_TRUE(full_bind.has_value()) << full_bind.error();
  ASSERT_EQ(full_bind->outcome, ParserModuleBindOutcome::kAccept);
  auto full_result = full->parse(input);
  ASSERT_TRUE(full_result.has_value()) << full_result.error();
  ASSERT_EQ(full_result->fault, ParserModuleFaultKind::kNone) << full_result->message;
  const auto* full_object = std::get_if<ParserModuleObjectOutput>(&*full_result->output);
  ASSERT_NE(full_object, nullptr);
  EXPECT_FALSE(full_object->splice.has_value());
  const auto* full_cloud = std::any_cast<sdk::PointCloud>(&full_object->object);
  ASSERT_NE(full_cloud, nullptr);
  EXPECT_EQ(full_cloud->width, 2U);
  EXPECT_EQ(full_cloud->frame_id, "map");
  const std::array<uint8_t, 8> expected_data{1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_TRUE(std::equal(full_cloud->data.begin(), full_cloud->data.end(), expected_data.begin(), expected_data.end()));

  auto spliced = WasmParserModuleInstance::create(*module, 1);
  ASSERT_TRUE(spliced.has_value()) << spliced.error().message;
  auto splice_bind = spliced->bind(binding(1, kSchema));
  ASSERT_TRUE(splice_bind.has_value()) << splice_bind.error();
  ASSERT_EQ(splice_bind->outcome, ParserModuleBindOutcome::kAccept);
  auto splice_result = spliced->parse(input);
  ASSERT_TRUE(splice_result.has_value()) << splice_result.error();
  ASSERT_EQ(splice_result->fault, ParserModuleFaultKind::kNone) << splice_result->message;
  const auto* splice_object = std::get_if<ParserModuleObjectOutput>(&*splice_result->output);
  ASSERT_NE(splice_object, nullptr);
  ASSERT_TRUE(splice_object->splice.has_value());
  EXPECT_EQ(splice_object->splice->field_number, 9U);
  EXPECT_EQ(splice_object->splice->input_offset, 20U);
  EXPECT_EQ(splice_object->splice->payload_bytes, (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
  const auto* splice_cloud = std::any_cast<sdk::PointCloud>(&splice_object->object);
  ASSERT_NE(splice_cloud, nullptr);
  EXPECT_TRUE(
      std::equal(splice_cloud->data.begin(), splice_cloud->data.end(), expected_data.begin(), expected_data.end()));
}

TEST(WasmParserModule, SurfacesBindDecline) {
  auto module = WasmParserModule::load(PJ_TOY_CDR_POINTCLOUD_WASM_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = WasmParserModuleInstance::create(*module, 0);
  ASSERT_TRUE(instance.has_value()) << instance.error().message;
  auto result = instance->bind(binding(0, "uint32 width\nstring frame_id\n"));
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->outcome, ParserModuleBindOutcome::kDecline);
  EXPECT_EQ(result->fault, ParserModuleFaultKind::kNone);
  EXPECT_NE(result->message.find("unsupported toy schema revision"), std::string::npos);
}

TEST(WasmParserModule, DeepSchemaReturnsDepthErrorWithConfiguredShadowStack) {
  std::string schema = "T1 next\n";
  for (size_t depth = 1; depth <= 64; ++depth) {
    schema += "MSG: T" + std::to_string(depth) + "\n";
    schema += depth < 64 ? "T" + std::to_string(depth + 1) + " next\n" : "uint32 value\n";
  }

  auto module = WasmParserModule::load(PJ_TOY_CDR_POINTCLOUD_WASM_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = WasmParserModuleInstance::create(*module, 0);
  ASSERT_TRUE(instance.has_value()) << instance.error().message;
  auto result = instance->bind(binding(0, schema));
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->outcome, ParserModuleBindOutcome::kDecline);
  EXPECT_EQ(result->fault, ParserModuleFaultKind::kNone);
  EXPECT_NE(result->message.find("nesting depth exceeds 64"), std::string::npos);
}

TEST(WasmParserModule, RejectsClaimIndexOutsideTheManifestBeforeCallingTheGuest) {
  auto module = WasmParserModule::load(PJ_TOY_CDR_POINTCLOUD_WASM_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = WasmParserModuleInstance::create(*module, 2);
  ASSERT_FALSE(instance.has_value());
  EXPECT_EQ(instance.error().outcome, WasmParserModuleCreateOutcome::kError);
  EXPECT_EQ(instance.error().fault, ParserModuleFaultKind::kNone);
  EXPECT_NE(instance.error().message.find("claim index is outside the wasm parser-module manifest"), std::string::npos);
}

TEST(WasmParserModule, CopiesTokenZeroCreationError) {
  TemporaryWasm artifact(withThreeClaimManifest(readFile(PJ_TOY_CDR_POINTCLOUD_WASM_PATH)));
  auto module = WasmParserModule::load(artifact.string());
  ASSERT_TRUE(module.has_value()) << module.error();
  // The host accepts index 2; the guest (built with two claims) returns the
  // creation-error token and the message is read back through
  // pj_module_last_error(0, ...).
  auto instance = WasmParserModuleInstance::create(*module, 2);
  ASSERT_FALSE(instance.has_value());
  EXPECT_EQ(instance.error().outcome, WasmParserModuleCreateOutcome::kError);
  EXPECT_EQ(instance.error().fault, ParserModuleFaultKind::kNone);
  EXPECT_EQ(instance.error().message, "claim index is outside the module manifest");
}

TEST(WasmParserModule, ClassifiesModuleParseErrorAsStrikeFreeDataError) {
  auto module = WasmParserModule::load(PJ_TOY_CDR_POINTCLOUD_WASM_PATH);
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = WasmParserModuleInstance::create(*module, 0);
  ASSERT_TRUE(instance.has_value()) << instance.error().message;
  auto bound = instance->bind(binding(0, kSchema));
  ASSERT_TRUE(bound.has_value()) << bound.error();
  ASSERT_EQ(bound->outcome, ParserModuleBindOutcome::kAccept);

  const std::array<uint8_t, 4> truncated_payload{0, 1, 0, 0};
  auto result = instance->parse(parser_module::ParseInputV1{.payload = truncated_payload});
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->fault, ParserModuleFaultKind::kDataError);
  EXPECT_FALSE(result->output.has_value());

  ParserModuleStrikeTracker tracker;
  const ParserModuleClaimKey key{"org.plotjuggler.test.kit-cdr-pointcloud", "full-wire"};
  EXPECT_EQ(tracker.recordFault(key, result->fault).strikes, 0U);
  EXPECT_EQ(tracker.state(key).health, ParserModuleClaimHealth::kActive);
}

TEST(WasmParserModule, ClassifiesGuestTrapAsContractViolation) {
  TemporaryWasm artifact(withTrappingParse(readFile(PJ_TOY_CDR_POINTCLOUD_WASM_PATH)));
  auto module = WasmParserModule::load(artifact.string());
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = WasmParserModuleInstance::create(*module, 0);
  ASSERT_TRUE(instance.has_value()) << instance.error().message;
  auto bound = instance->bind(binding(0, kSchema));
  ASSERT_TRUE(bound.has_value()) << bound.error();
  ASSERT_EQ(bound->outcome, ParserModuleBindOutcome::kAccept);
  const auto payload = toyPayload();
  auto result = instance->parse(parser_module::ParseInputV1{.payload = payload});
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->fault, ParserModuleFaultKind::kContractViolation);
  EXPECT_NE(result->message.find("wasm trap"), std::string::npos);

  ParserModuleStrikeTracker tracker;
  const ParserModuleClaimKey key{"org.plotjuggler.test.kit-cdr-pointcloud", "full-wire"};
  EXPECT_EQ(tracker.recordFault(key, result->fault).strikes, 1U);
}

TEST(WasmParserModule, TypesCreationTrapsAsContractViolations) {
  TemporaryWasm artifact(withTrappingCreate(readFile(PJ_TOY_CDR_POINTCLOUD_WASM_PATH)));
  auto module = WasmParserModule::load(artifact.string());
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = WasmParserModuleInstance::create(*module, 0);
  ASSERT_FALSE(instance.has_value());
  EXPECT_EQ(instance.error().outcome, WasmParserModuleCreateOutcome::kError);
  EXPECT_EQ(instance.error().fault, ParserModuleFaultKind::kContractViolation);
  EXPECT_NE(instance.error().message.find("pj_module_create failed: wasm trap"), std::string::npos);

  // The host feeds the classified fault to the same tracker native uses.
  ParserModuleStrikeTracker tracker;
  const ParserModuleClaimKey key{"org.plotjuggler.test.kit-cdr-pointcloud", "full-wire"};
  EXPECT_EQ(tracker.recordFault(key, instance.error().fault).strikes, 1U);
}

TEST(WasmParserModule, SurvivesDestroyTrapsAndReportsGuestFreeTraps) {
  {
    TemporaryWasm artifact(withTrappingDestroy(readFile(PJ_TOY_CDR_POINTCLOUD_WASM_PATH)));
    auto module = WasmParserModule::load(artifact.string());
    ASSERT_TRUE(module.has_value()) << module.error();
    auto instance = WasmParserModuleInstance::create(*module, 0);
    ASSERT_TRUE(instance.has_value()) << instance.error().message;
    // Teardown is best-effort: a trapping destroy must not escape the wrapper.
    *instance = WasmParserModuleInstance{};
    EXPECT_FALSE(instance->valid());
  }

  TemporaryWasm artifact(withTrappingFree(readFile(PJ_TOY_CDR_POINTCLOUD_WASM_PATH)));
  auto module = WasmParserModule::load(artifact.string());
  ASSERT_TRUE(module.has_value()) << module.error();
  auto instance = WasmParserModuleInstance::create(*module, 0);
  ASSERT_TRUE(instance.has_value()) << instance.error().message;
  auto result = instance->bind(binding(0, kSchema));
  ASSERT_TRUE(result.has_value()) << result.error();
  EXPECT_EQ(result->fault, ParserModuleFaultKind::kContractViolation);
  EXPECT_NE(result->message.find("pj_module_free failed after bind"), std::string::npos);
  EXPECT_NE(instance->lifecycleDiagnostic().find("pj_module_free failed after bind"), std::string_view::npos);
}

}  // namespace
}  // namespace PJ
