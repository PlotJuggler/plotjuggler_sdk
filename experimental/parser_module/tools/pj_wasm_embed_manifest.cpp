// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "pj_base/parser_module_manifest.hpp"
#include "pj_base/parser_module_wasm.hpp"

namespace {

using PJ::Expected;
using PJ::Span;
using PJ::unexpected;

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

[[nodiscard]] Expected<void> verify(
    const std::string& wasm_path, const std::string& manifest_path, uint64_t maximum_memory_bytes) {
  auto wasm = readFile(wasm_path);
  auto manifest = readFile(manifest_path);
  if (!wasm) {
    return unexpected(wasm.error());
  }
  if (!manifest) {
    return unexpected(manifest.error());
  }
  PJ::parser_module::ParserModuleWasmLimits limits;
  limits.maximum_linear_memory_bytes = maximum_memory_bytes;
  auto artifact = PJ::parser_module::validateParserModuleWasmArtifact(*wasm, limits);
  if (!artifact) {
    return unexpected(artifact.error());
  }
  const auto& embedded = artifact->manifest_json;
  if (embedded.size() != manifest->size() || !std::equal(embedded.begin(), embedded.end(), manifest->begin())) {
    return unexpected(std::string("embedded parser-module manifest bytes do not match the source file"));
  }

  const auto& module = artifact->module;
  std::cout << "WASM parser-module ABI conformance: PASS\n"
            << "  sections enumerated: " << module.section_count << '\n'
            << "  function types: " << module.function_type_count << ", functions: " << module.function_count
            << ", exports: " << module.exports.size() << '\n'
            << "  operational exports: 8 exact signatures verified\n"
            << "  reactor: _initialize exported; _start/start section absent\n"
            << "  native-only metadata exports: absent\n"
            << "  imports: empty frozen allow-list verified\n"
            << "  declared linear-memory maximum: " << artifact->declared_linear_memory_maximum << " bytes\n"
            << "  declared table elements: " << artifact->declared_table_elements << " (cap "
            << limits.maximum_table_elements << ")\n"
            << "  manifest section: exactly one, " << embedded.size() << " exact bytes\n";
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

void usage() {
  std::cerr << "usage: pj-wasm-embed-manifest embed INPUT.wasm MANIFEST.json OUTPUT.wasm\n"
               "   or: pj-wasm-embed-manifest verify MODULE.wasm MANIFEST.json [MAX_MEMORY_BYTES]\n";
}

}  // namespace

int main(int argc, char** argv) {
  Expected<void> result = unexpected(std::string("invalid arguments"));
  if (argc == 5 && std::string_view(argv[1]) == "embed") {
    result = embed(argv[2], argv[3], argv[4]);
  } else if ((argc == 4 || argc == 5) && std::string_view(argv[1]) == "verify") {
    uint64_t maximum_memory_bytes = PJ::parser_module::ParserModuleWasmLimits::kDefaultMaximumLinearMemoryBytes;
    if (argc == 5) {
      const std::string_view text(argv[4]);
      const auto parsed = std::from_chars(text.data(), text.data() + text.size(), maximum_memory_bytes);
      if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || maximum_memory_bytes == 0) {
        std::cerr << "parser-module wasm operation failed: MAX_MEMORY_BYTES must be a positive integer\n";
        return 1;
      }
    }
    result = verify(argv[2], argv[3], maximum_memory_bytes);
  } else {
    usage();
    return 2;
  }
  if (!result) {
    std::cerr << "parser-module wasm operation failed: " << result.error() << '\n';
    return 1;
  }
  return 0;
}
