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
  if (wasmer_is_backend_available(SINGLEPASS)) {
    wasm_config_set_backend(config, SINGLEPASS);
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
  if (session_budget != nullptr) {
    session_budget->releaseModule(module_reservation);
  }
}

}  // namespace detail

WasmParserModule::WasmParserModule(std::shared_ptr<const detail::WasmParserModuleState> state)
    : state_(std::move(state)) {}

Expected<WasmParserModule> WasmParserModule::load(std::string_view path, const WasmParserModuleLoadOptions& options) {
  // Every rejection emits exactly one error diagnostic and returns the same
  // text to the caller.
  const auto reject = [&](std::string message) -> Expected<WasmParserModule> {
    if (options.sink) {
      options.sink(
          Diagnostic{
              .level = DiagnosticLevel::kError,
              .source = options.diagnostic_source,
              .id = std::string(path),
              .message = message,
          });
    }
    return unexpected(std::move(message));
  };

  const WasmParserModuleLimits& limits = options.limits;
  if (limits.maximum_artifact_bytes == 0 || limits.maximum_linear_memory_bytes == 0 ||
      limits.metering_points_per_call == 0) {
    return reject("wasm parser-module limits must all be nonzero");
  }
  auto bytes = readFile(path, limits.maximum_artifact_bytes);
  if (!bytes) {
    return reject(bytes.error());
  }

  auto audited = parser_module::validateParserModuleWasmArtifact(
      *bytes, parser_module::ParserModuleWasmLimits{
                  .maximum_linear_memory_bytes = limits.maximum_linear_memory_bytes,
                  .maximum_table_elements = limits.maximum_table_elements,
              });
  if (!audited) {
    return reject(audited.error());
  }
  const char* manifest_data =
      audited->manifest_json.empty() ? "" : reinterpret_cast<const char*>(audited->manifest_json.data());
  const std::string_view manifest_json(manifest_data, audited->manifest_json.size());
  auto decoded_manifest = decodeParserModuleManifest(manifest_json, ParserClaimProvenance::kFolderDrop);
  if (!decoded_manifest) {
    return reject("invalid wasm parser-module manifest: " + decoded_manifest.error());
  }

  auto state = std::make_shared<detail::WasmParserModuleState>();
  state->path = path;
  state->manifest_json.assign(manifest_json);
  state->claim_count = decoded_manifest->claims.size();
  state->artifact_size = bytes->size();
  state->declared_linear_memory_maximum = audited->declared_linear_memory_maximum;
  state->declared_table_elements = audited->declared_table_elements;
  state->metering_points_per_call = limits.metering_points_per_call;
  if (options.budget != nullptr) {
    auto admission = options.budget->admitModule(
        bytes->size(), decoded_manifest->claims.size(), audited->declared_linear_memory_maximum);
    if (!admission.accepted()) {
      return reject(std::move(admission.diagnostic));
    }
    state->session_budget = options.budget;
    state->module_reservation = admission.reservation;
  }
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

uint64_t WasmParserModule::declaredTableElements() const noexcept {
  return state_ == nullptr ? 0 : state_->declared_table_elements;
}

}  // namespace PJ
