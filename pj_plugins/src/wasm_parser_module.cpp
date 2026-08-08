// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/wasm_parser_module.hpp"

#include <wasmer.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "detail/wasm_parser_module_state.hpp"
#include "pj_base/parser_module_manifest.hpp"
#include "pj_base/parser_module_wasm.hpp"
#include "pj_plugins/host/parser_claim_catalog.hpp"

namespace PJ {
namespace {

Expected<std::vector<uint8_t>> readFile(std::string_view path, uint64_t maximum_bytes) {
  std::ifstream input(std::string(path), std::ios::binary | std::ios::ate);
  if (!input) {
    return unexpected(std::string("cannot open wasm parser module: ") + std::string(path));
  }
  const std::streamoff end = input.tellg();
  if (end < 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max() ||
      end > std::numeric_limits<std::streamsize>::max()) {
    return unexpected(std::string("wasm parser-module file size is invalid"));
  }
  if (static_cast<uint64_t>(end) > maximum_bytes) {
    return unexpected(
        "parser-module admission DECLINE: artifact_file_size budget exhausted (size " +
        std::to_string(static_cast<uint64_t>(end)) + ", limit " + std::to_string(maximum_bytes) + ")");
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  input.seekg(0);
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!input) {
    return unexpected(std::string("cannot read complete wasm parser-module file"));
  }
  return bytes;
}

std::string wasmerLastError() {
  const int length = wasmer_last_error_length();
  if (length <= 0) {
    return "Wasmer supplied no diagnostic";
  }
  std::string message(static_cast<size_t>(length), '\0');
  const int written = wasmer_last_error_message(message.data(), length);
  if (written <= 0) {
    return "Wasmer diagnostic retrieval failed";
  }
  if (!message.empty() && message.back() == '\0') {
    message.pop_back();
  }
  return message;
}

std::string wasmerFailure(std::string_view action) {
  return std::string(action) + ": " + wasmerLastError();
}

uint64_t unitMeteringCost(wasmer_parser_operator_t) {
  return 1;
}

Expected<wasm_engine_t*> createMeteredEngine(uint64_t points_per_call) {
  wasm_config_t* config = wasm_config_new();
  if (config == nullptr) {
    return unexpected(wasmerFailure("failed to create Wasmer configuration"));
  }
  wasmer_metering_t* metering = wasmer_metering_new(points_per_call, &unitMeteringCost);
  if (metering == nullptr) {
    wasm_config_delete(config);
    return unexpected(wasmerFailure("failed to create Wasmer metering middleware"));
  }
  wasmer_middleware_t* middleware = wasmer_metering_as_middleware(metering);
  if (middleware == nullptr) {
    wasmer_metering_delete(metering);
    wasm_config_delete(config);
    return unexpected(wasmerFailure("failed to adapt Wasmer metering middleware"));
  }
  wasm_config_push_middleware(config, middleware);
  wasm_engine_t* engine = wasm_engine_new_with_config(config);
  if (engine == nullptr) {
    wasm_config_delete(config);
    return unexpected(wasmerFailure("failed to create metered Wasmer engine"));
  }
  return engine;
}

}  // namespace

namespace detail {

WasmParserModuleState::~WasmParserModuleState() {
  if (module != nullptr) {
    wasm_module_delete(module);
  }
  if (engine != nullptr) {
    wasm_engine_delete(engine);
  }
  if (module_budget_reserved && session_budget != nullptr) {
    (void)session_budget->releaseModule(module_id);
  }
}

}  // namespace detail

WasmParserModule::WasmParserModule(std::shared_ptr<const detail::WasmParserModuleState> state)
    : state_(std::move(state)) {}

Expected<WasmParserModule> WasmParserModule::load(
    std::string_view path, DiagnosticSink sink, std::string diagnostic_source) {
  return load(
      path, WasmParserModuleLimits{}, defaultParserModuleSessionBudget(), std::move(sink),
      std::move(diagnostic_source));
}

Expected<WasmParserModule> WasmParserModule::load(
    std::string_view path, const WasmParserModuleLimits& limits, DiagnosticSink sink, std::string diagnostic_source) {
  return load(path, limits, defaultParserModuleSessionBudget(), std::move(sink), std::move(diagnostic_source));
}

Expected<WasmParserModule> WasmParserModule::load(
    std::string_view path, std::shared_ptr<ParserModuleSessionBudgetTracker> budget, DiagnosticSink sink,
    std::string diagnostic_source) {
  return load(path, WasmParserModuleLimits{}, std::move(budget), std::move(sink), std::move(diagnostic_source));
}

Expected<WasmParserModule> WasmParserModule::load(
    std::string_view path, const WasmParserModuleLimits& limits,
    std::shared_ptr<ParserModuleSessionBudgetTracker> budget, DiagnosticSink sink, std::string diagnostic_source) {
  // Every rejection emits exactly one error diagnostic and returns the same
  // text to the caller.
  const auto reject = [&](std::string message) -> Expected<WasmParserModule> {
    if (sink) {
      sink(
          Diagnostic{
              .level = DiagnosticLevel::kError,
              .source = diagnostic_source,
              .id = std::string(path),
              .message = message,
          });
    }
    return unexpected(std::move(message));
  };

  if (limits.maximum_artifact_bytes == 0 || limits.maximum_linear_memory_bytes == 0 ||
      limits.metering_points_per_call == 0) {
    return reject("wasm parser-module limits must all be nonzero");
  }
  if (budget == nullptr) {
    return reject("wasm parser-module session budget is null");
  }
  const uint64_t maximum_artifact_bytes =
      std::min(limits.maximum_artifact_bytes, budget->limits().maximum_artifact_bytes);
  auto bytes = readFile(path, maximum_artifact_bytes);
  if (!bytes) {
    return reject(bytes.error());
  }

  auto manifest = parser_module::readManifestSection(*bytes);
  if (!manifest) {
    return reject("invalid wasm parser-module manifest: " + manifest.error());
  }
  const char* manifest_data = manifest->empty() ? "" : reinterpret_cast<const char*>(manifest->data());
  const std::string_view manifest_json(manifest_data, manifest->size());
  auto decoded_manifest = decodeParserModuleManifest(manifest_json, ParserClaimProvenance::kFolderDrop);
  if (!decoded_manifest) {
    return reject("invalid wasm parser-module manifest: " + decoded_manifest.error());
  }
  auto inspected = parser_module::inspectWasmModule(*bytes);
  if (!inspected) {
    return reject("invalid wasm parser module: " + inspected.error());
  }
  if (!inspected->imports.empty()) {
    const auto& imported = inspected->imports.front();
    return reject("wasm parser module uses disallowed import '" + imported.module + "." + imported.name + "'");
  }
  auto abi = parser_module::validateParserModuleWasmAbi(*inspected);
  if (!abi) {
    return reject(abi.error());
  }
  auto memory_maximum = parser_module::validateParserModuleWasmMemory(*inspected, limits.maximum_linear_memory_bytes);
  if (!memory_maximum) {
    return reject(memory_maximum.error());
  }

  auto state = std::make_shared<detail::WasmParserModuleState>();
  state->path = path;
  state->artifact_size = bytes->size();
  state->declared_linear_memory_maximum = *memory_maximum;
  state->metering_points_per_call = limits.metering_points_per_call;
  state->module_id = decoded_manifest->id;
  state->claim_ids.reserve(decoded_manifest->claims.size());
  for (const auto& claim : decoded_manifest->claims) {
    state->claim_ids.push_back(claim.claim_id);
  }
  state->manifest_json.assign(manifest_json);
  state->session_budget = budget;
  state->strike_tracker = std::make_shared<ParserModuleStrikeTracker>();
  auto admission =
      budget->admitModule(decoded_manifest->id, bytes->size(), decoded_manifest->claims.size(), *memory_maximum);
  if (!admission.accepted()) {
    return reject(std::move(admission.diagnostic));
  }
  state->module_budget_reserved = true;
  auto engine = createMeteredEngine(limits.metering_points_per_call);
  if (!engine) {
    return reject(engine.error());
  }
  state->engine = *engine;
  const wasm_byte_vec_t binary{
      .size = bytes->size(),
      .data = reinterpret_cast<wasm_byte_t*>(bytes->data()),
  };
  state->module = wasmer_module_new(state->engine, &binary);
  if (state->module == nullptr) {
    return reject(wasmerFailure("Wasmer rejected parser module"));
  }
  return WasmParserModule(std::move(state));
}

std::string_view WasmParserModule::path() const noexcept {
  return state_ == nullptr ? std::string_view{} : std::string_view(state_->path);
}

std::string_view WasmParserModule::manifestJson() const noexcept {
  return state_ == nullptr ? std::string_view{} : std::string_view(state_->manifest_json);
}

uint64_t WasmParserModule::artifactSize() const noexcept {
  return state_ == nullptr ? 0 : state_->artifact_size;
}

uint64_t WasmParserModule::declaredLinearMemoryMaximum() const noexcept {
  return state_ == nullptr ? 0 : state_->declared_linear_memory_maximum;
}

ParserModuleStrikeState WasmParserModule::strikeState(uint32_t claim_index) const {
  if (state_ == nullptr || claim_index >= state_->claim_ids.size()) {
    return {};
  }
  return state_->strike_tracker->state(ParserModuleClaimKey{state_->module_id, state_->claim_ids[claim_index]});
}

}  // namespace PJ
