// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/native_parser_module.hpp"

#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "detail/native_parser_module_state.hpp"
#include "pj_base/parser_module_abi.h"

namespace PJ {
namespace {

std::vector<detail::NativeModuleHandle>& sessionHandles() {
  static auto* handles = new std::vector<detail::NativeModuleHandle>();
  return *handles;
}

std::mutex& sessionHandlesMutex() {
  static auto* mutex = new std::mutex();
  return *mutex;
}

void retainForSession(detail::NativeModuleHandle handle) {
  std::lock_guard lock(sessionHandlesMutex());
  sessionHandles().push_back(handle);
}

Expected<NativeParserModule> rejectLoad(
    std::string_view path, const DiagnosticSink& sink, std::string_view source, std::string message) {
  if (sink) {
    sink(
        Diagnostic{
            .level = DiagnosticLevel::kError,
            .source = std::string(source),
            .id = std::string(path),
            .message = message,
        });
  }
  return unexpected(std::move(message));
}

template <typename Function>
Expected<Function> resolve(detail::NativeModuleHandle handle, const char* name) {
  auto symbol = detail::resolveNativeParserModuleSymbol(handle, name);
  if (!symbol) {
    return unexpected(symbol.error());
  }
  return reinterpret_cast<Function>(*symbol);
}

}  // namespace

NativeParserModule::NativeParserModule(std::shared_ptr<const detail::NativeParserModuleState> state)
    : state_(std::move(state)) {}

Expected<NativeParserModule> NativeParserModule::load(
    std::string_view path, DiagnosticSink sink, std::string diagnostic_source) {
  auto handle_result = detail::openNativeParserModule(std::filesystem::path(path));
  if (!handle_result) {
    return rejectLoad(path, sink, diagnostic_source, "failed to open native parser module: " + handle_result.error());
  }
  const auto handle = *handle_result;
  retainForSession(handle);

  auto state = std::make_shared<detail::NativeParserModuleState>();
  state->handle = handle;
  state->path = path;

#define PJ_RESOLVE_MODULE_EXPORT(member, type, name)                      \
  do {                                                                    \
    auto resolved = resolve<type>(handle, name);                          \
    if (!resolved) {                                                      \
      return rejectLoad(path, sink, diagnostic_source, resolved.error()); \
    }                                                                     \
    state->member = *resolved;                                            \
  } while (false)

  PJ_RESOLVE_MODULE_EXPORT(abi, PJ_module_abi_fn_t, PJ_MODULE_ABI_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(create, PJ_module_create_fn_t, PJ_MODULE_CREATE_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(destroy, PJ_module_destroy_fn_t, PJ_MODULE_DESTROY_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(bind, PJ_module_bind_fn_t, PJ_MODULE_BIND_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(parse, PJ_module_parse_fn_t, PJ_MODULE_PARSE_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(last_error, PJ_module_last_error_fn_t, PJ_MODULE_LAST_ERROR_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(alloc, PJ_module_alloc_fn_t, PJ_MODULE_ALLOC_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(free, PJ_module_free_fn_t, PJ_MODULE_FREE_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(manifest_addr, PJ_module_manifest_addr_fn_t, PJ_MODULE_MANIFEST_ADDR_EXPORT_NAME);
  PJ_RESOLVE_MODULE_EXPORT(manifest_len, PJ_module_manifest_len_fn_t, PJ_MODULE_MANIFEST_LEN_EXPORT_NAME);

#undef PJ_RESOLVE_MODULE_EXPORT

  const uint32_t actual_abi = state->abi();
  if (actual_abi != PJ_PARSER_MODULE_ABI_VERSION) {
    return rejectLoad(
        path, sink, diagnostic_source,
        "native parser module ABI mismatch (expected " + std::to_string(PJ_PARSER_MODULE_ABI_VERSION) + ", got " +
            std::to_string(actual_abi) + ")");
  }

  const uint64_t manifest_addr = state->manifest_addr();
  const uint64_t manifest_len = state->manifest_len();
  if (manifest_addr == 0 || manifest_len == 0 || manifest_len > std::numeric_limits<size_t>::max()) {
    return rejectLoad(path, sink, diagnostic_source, "native parser module manifest is unreadable");
  }
  const auto* manifest = reinterpret_cast<const char*>(static_cast<uintptr_t>(manifest_addr));
  state->manifest_json.assign(manifest, static_cast<size_t>(manifest_len));

  return NativeParserModule(std::move(state));
}

std::string_view NativeParserModule::path() const noexcept {
  return state_ == nullptr ? std::string_view{} : std::string_view(state_->path);
}

std::string_view NativeParserModule::manifestJson() const noexcept {
  return state_ == nullptr ? std::string_view{} : std::string_view(state_->manifest_json);
}

}  // namespace PJ
