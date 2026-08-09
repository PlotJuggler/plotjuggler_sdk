// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/wasm_parser_module_runtime.hpp"

#include <wasmer.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "detail/parser_module_result_helpers.hpp"
#include "detail/wasm_parser_module_state.hpp"
#include "pj_base/parser_module_wasm.hpp"
#include "pj_base/span.hpp"

namespace PJ {
namespace {

using detail::contractViolation;

/// pj_module_parse writes the output descriptor address into the first host
/// slot and its length into the second. Both slots are i64.
constexpr size_t kOutputSlotBytes = 8;
constexpr size_t kOutputBlockBytes = 2 * kOutputSlotBytes;

std::string trapMessage(wasm_trap_t* trap) {
  wasm_message_t message = WASM_EMPTY_VEC;
  wasm_trap_message(trap, &message);
  size_t size = message.size;
  if (size != 0 && message.data[size - 1] == '\0') {
    --size;
  }
  std::string result;
  if (size != 0 && message.data != nullptr) {
    result.assign(message.data, size);
  }
  wasm_byte_vec_delete(&message);
  wasm_trap_delete(trap);
  return result.empty() ? "wasm trap without a message" : "wasm trap: " + result;
}

std::string meteringExhaustedMessage(std::string_view function_name, uint64_t points_per_call) {
  return "wasm metering exhausted during " + std::string(function_name) + " (instruction-point limit " +
         std::to_string(points_per_call) + ")";
}

/// Every guest ABI argument is an i64 token, address, or byte count.
wasm_val_t wasmI64(uint64_t value) {
  const wasm_val_t argument = WASM_I64_VAL(static_cast<int64_t>(value));
  return argument;
}

/// Call one guest export with a fresh instruction-point allowance. A trap and
/// an exhausted allowance both fail the call; exhaustion is reported in
/// preference to the trap message because it names the enforced deadline.
Expected<void> callGuest(
    wasm_instance_t* instance, uint64_t points_per_call, std::string_view function_name, wasm_func_t* function,
    Span<wasm_val_t> arguments, wasm_val_vec_t* results) {
  wasmer_metering_set_remaining_points(instance, points_per_call);
  const wasm_val_vec_t args{.size = arguments.size(), .data = arguments.data()};
  if (wasm_trap_t* trap = wasm_func_call(function, &args, results)) {
    const bool exhausted = wasmer_metering_points_are_exhausted(instance);
    std::string message = trapMessage(trap);
    if (exhausted) {
      return unexpected(meteringExhaustedMessage(function_name, points_per_call));
    }
    return unexpected(std::move(message));
  }
  if (wasmer_metering_points_are_exhausted(instance)) {
    return unexpected(meteringExhaustedMessage(function_name, points_per_call));
  }
  return {};
}

std::string exportName(const wasm_exporttype_t* exported) {
  const wasm_name_t* name = wasm_exporttype_name(exported);
  return std::string(name->data, name->size);
}

uint64_t decodeU64(Span<const uint8_t> bytes) {
  uint64_t value = 0;
  for (size_t index = 0; index < kOutputSlotBytes; ++index) {
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

ParserModuleBindResult bindContractViolation(int32_t code, std::string message) {
  return ParserModuleBindResult{
      .outcome = ParserModuleBindOutcome::kError,
      .fault = ParserModuleFaultKind::kContractViolation,
      .result_code = code,
      .message = std::move(message),
  };
}

}  // namespace

namespace detail {

struct WasmParserModuleInstanceState {
  ~WasmParserModuleInstanceState() {
    if (token != PJ_MODULE_CREATION_ERROR_TOKEN && destroy != nullptr) {
      wasm_val_t arguments[1] = {wasmI64(token)};
      auto destroyed = callVoid(PJ_MODULE_DESTROY_EXPORT_NAME, destroy, arguments);
      if (!destroyed) {
        (void)recordContractViolation("pj_module_destroy failed: " + destroyed.error());
      }
    }
    if (exports_initialized) {
      wasm_extern_vec_delete(&exports);
    }
    if (instance != nullptr) {
      wasm_instance_delete(instance);
    }
    if (store != nullptr) {
      wasm_store_delete(store);
    }
    if (instance_budget_reserved && module->session_budget != nullptr) {
      (void)module->session_budget->releaseInstance(module->module_id);
    }
  }

  [[nodiscard]] ParserModuleClaimKey claimKey() const {
    return ParserModuleClaimKey{module->module_id, module->claim_ids[claim_index]};
  }

  [[nodiscard]] ParserModuleStrikeState recordContractViolation(std::string message) {
    lifecycle_diagnostic = std::move(message);
    return module->strike_tracker->recordFault(claimKey(), ParserModuleFaultKind::kContractViolation);
  }

  [[nodiscard]] Expected<void> callVoid(
      std::string_view function_name, wasm_func_t* function, Span<wasm_val_t> arguments) const {
    wasm_val_vec_t results = WASM_EMPTY_VEC;
    return callGuest(instance, module->metering_points_per_call, function_name, function, arguments, &results);
  }

  [[nodiscard]] Expected<int32_t> callI32(
      std::string_view function_name, wasm_func_t* function, Span<wasm_val_t> arguments) const {
    wasm_val_t values[1] = {WASM_INIT_VAL};
    wasm_val_vec_t results = WASM_ARRAY_VEC(values);
    auto called = callGuest(instance, module->metering_points_per_call, function_name, function, arguments, &results);
    if (!called) {
      return unexpected(called.error());
    }
    if (values[0].kind != WASM_I32) {
      return unexpected(std::string("wasm function returned a non-i32 result"));
    }
    return values[0].of.i32;
  }

  [[nodiscard]] Expected<int64_t> callI64(
      std::string_view function_name, wasm_func_t* function, Span<wasm_val_t> arguments) const {
    wasm_val_t values[1] = {WASM_INIT_VAL};
    wasm_val_vec_t results = WASM_ARRAY_VEC(values);
    auto called = callGuest(instance, module->metering_points_per_call, function_name, function, arguments, &results);
    if (!called) {
      return unexpected(called.error());
    }
    if (values[0].kind != WASM_I64) {
      return unexpected(std::string("wasm function returned a non-i64 result"));
    }
    return values[0].of.i64;
  }

  [[nodiscard]] Expected<Span<uint8_t>> memoryRange(uint64_t address, uint64_t length) const {
    if (memory == nullptr || address > std::numeric_limits<size_t>::max() ||
        length > std::numeric_limits<size_t>::max()) {
      return unexpected(std::string("guest memory range exceeds the host address range"));
    }
    const size_t offset = static_cast<size_t>(address);
    const size_t size = static_cast<size_t>(length);
    const size_t memory_size = wasm_memory_data_size(memory);
    if (offset > memory_size || size > memory_size - offset) {
      return unexpected(std::string("guest memory range is outside current linear memory"));
    }
    auto* base = reinterpret_cast<uint8_t*>(wasm_memory_data(memory));
    if (base == nullptr && memory_size != 0) {
      return unexpected(std::string("Wasmer returned a null linear-memory base"));
    }
    return Span<uint8_t>(base == nullptr ? nullptr : base + offset, size);
  }

  [[nodiscard]] Expected<uint64_t> allocate(uint64_t size) const {
    wasm_val_t arguments[1] = {wasmI64(size)};
    auto result = callI64(PJ_MODULE_ALLOC_EXPORT_NAME, alloc, arguments);
    if (!result) {
      return unexpected(result.error());
    }
    const uint64_t address = static_cast<uint64_t>(*result);
    if (address == 0) {
      return unexpected(std::string("pj_module_alloc returned token zero"));
    }
    return address;
  }

  [[nodiscard]] Expected<void> freeAllocation(uint64_t address, uint64_t size) const {
    wasm_val_t arguments[2] = {wasmI64(address), wasmI64(size)};
    return callVoid(PJ_MODULE_FREE_EXPORT_NAME, free, arguments);
  }

  /// Read the guest error string into an already-allocated guest buffer. The
  /// caller owns that buffer and releases it whatever the outcome here.
  [[nodiscard]] Expected<std::string> readLastError(uint64_t error_token, uint64_t buffer_address) const {
    wasm_val_t arguments[3] = {
        wasmI64(error_token),
        wasmI64(buffer_address),
        wasmI64(PJ_MODULE_ERROR_BUFFER_SIZE),
    };
    auto written_result = callI64(PJ_MODULE_LAST_ERROR_EXPORT_NAME, last_error, arguments);
    if (!written_result) {
      return unexpected("pj_module_last_error trapped: " + written_result.error());
    }
    const uint64_t written = static_cast<uint64_t>(*written_result);
    if (written > PJ_MODULE_ERROR_BUFFER_SIZE) {
      return unexpected(std::string("pj_module_last_error returned a length larger than its buffer"));
    }
    auto bytes = memoryRange(buffer_address, written);
    if (!bytes) {
      return unexpected(bytes.error());
    }
    const auto terminator = std::find(bytes->begin(), bytes->end(), uint8_t{0});
    return std::string(bytes->begin(), terminator);
  }

  [[nodiscard]] Expected<std::string> copyLastError(uint64_t error_token) const {
    auto address = allocate(PJ_MODULE_ERROR_BUFFER_SIZE);
    if (!address) {
      return unexpected("cannot allocate the guest error buffer: " + address.error());
    }
    auto message = readLastError(error_token, *address);
    auto released = freeAllocation(*address, PJ_MODULE_ERROR_BUFFER_SIZE);
    if (!message) {
      return unexpected(message.error());
    }
    if (!released) {
      return unexpected("pj_module_free trapped after last_error: " + released.error());
    }
    return message;
  }

  std::shared_ptr<const WasmParserModuleState> module;
  wasm_store_t* store = nullptr;
  wasm_instance_t* instance = nullptr;
  wasm_extern_vec_t exports = WASM_EMPTY_VEC;
  bool exports_initialized = false;
  wasm_memory_t* memory = nullptr;
  wasm_func_t* initialize = nullptr;
  wasm_func_t* abi = nullptr;
  wasm_func_t* create = nullptr;
  wasm_func_t* destroy = nullptr;
  wasm_func_t* bind = nullptr;
  wasm_func_t* parse = nullptr;
  wasm_func_t* last_error = nullptr;
  wasm_func_t* alloc = nullptr;
  wasm_func_t* free = nullptr;
  uint64_t token = PJ_MODULE_CREATION_ERROR_TOKEN;
  uint32_t claim_index = 0;
  parser_module::Route bound_route = parser_module::Route::kScalar;
  uint16_t expected_object_type = 0;
  bool bound = false;
  bool instance_budget_reserved = false;
  bool recreation_pending = false;
  std::vector<uint8_t> binding_bytes;
  std::string lifecycle_diagnostic;
};

}  // namespace detail

namespace {

/// Owns one `pj_module_alloc` region for the duration of a host operation.
/// The destructor releases it and records a contract fault if guest free
/// traps; `release()` frees it early and returns that fault directly.
class GuestAllocation {
 public:
  GuestAllocation(detail::WasmParserModuleInstanceState& state, uint64_t address, uint64_t size)
      : state_(&state), address_(address), size_(size) {}

  GuestAllocation(const GuestAllocation&) = delete;
  GuestAllocation& operator=(const GuestAllocation&) = delete;

  ~GuestAllocation() {
    auto released = release();
    if (!released) {
      (void)state_->recordContractViolation("pj_module_free failed during cleanup: " + released.error());
    }
  }

  [[nodiscard]] uint64_t address() const noexcept {
    return address_;
  }

  /// Free now instead of at scope exit. A released allocation stays released.
  [[nodiscard]] Expected<void> release() {
    if (address_ == 0) {
      return {};
    }
    return state_->freeAllocation(std::exchange(address_, UINT64_C(0)), size_);
  }

 private:
  detail::WasmParserModuleInstanceState* state_;
  uint64_t address_;
  uint64_t size_;
};

/// Release both parse allocations, output slots first, and report the first
/// guest free that failed.
Expected<void> releaseParseBuffers(GuestAllocation& slots, GuestAllocation& input) {
  auto released_slots = slots.release();
  auto released_input = input.release();
  if (!released_slots) {
    return unexpected("pj_module_free failed: " + released_slots.error());
  }
  if (!released_input) {
    return unexpected("pj_module_free failed: " + released_input.error());
  }
  return {};
}

Expected<void> bindRuntimeExports(detail::WasmParserModuleInstanceState* state) {
  wasm_exporttype_vec_t declarations = WASM_EMPTY_VEC;
  wasm_module_exports(state->module->module, &declarations);
  wasm_instance_exports(state->instance, &state->exports);
  state->exports_initialized = true;
  if (declarations.size != state->exports.size) {
    wasm_exporttype_vec_delete(&declarations);
    return unexpected(std::string("Wasmer returned an export count inconsistent with the compiled module"));
  }

  for (size_t index = 0; index < declarations.size; ++index) {
    const std::string name = exportName(declarations.data[index]);
    wasm_extern_t* external = state->exports.data[index];
    if (name == "memory") {
      state->memory = wasm_extern_as_memory(external);
    } else if (name == "_initialize") {
      state->initialize = wasm_extern_as_func(external);
    } else if (name == PJ_MODULE_ABI_EXPORT_NAME) {
      state->abi = wasm_extern_as_func(external);
    } else if (name == PJ_MODULE_CREATE_EXPORT_NAME) {
      state->create = wasm_extern_as_func(external);
    } else if (name == PJ_MODULE_DESTROY_EXPORT_NAME) {
      state->destroy = wasm_extern_as_func(external);
    } else if (name == PJ_MODULE_BIND_EXPORT_NAME) {
      state->bind = wasm_extern_as_func(external);
    } else if (name == PJ_MODULE_PARSE_EXPORT_NAME) {
      state->parse = wasm_extern_as_func(external);
    } else if (name == PJ_MODULE_LAST_ERROR_EXPORT_NAME) {
      state->last_error = wasm_extern_as_func(external);
    } else if (name == PJ_MODULE_ALLOC_EXPORT_NAME) {
      state->alloc = wasm_extern_as_func(external);
    } else if (name == PJ_MODULE_FREE_EXPORT_NAME) {
      state->free = wasm_extern_as_func(external);
    }
  }
  wasm_exporttype_vec_delete(&declarations);

  if (state->memory == nullptr || state->initialize == nullptr || state->abi == nullptr || state->create == nullptr ||
      state->destroy == nullptr || state->bind == nullptr || state->parse == nullptr || state->last_error == nullptr ||
      state->alloc == nullptr || state->free == nullptr) {
    return unexpected(std::string("Wasmer instance is missing a statically validated runtime export"));
  }
  return {};
}

}  // namespace

WasmParserModuleInstance::WasmParserModuleInstance(std::unique_ptr<detail::WasmParserModuleInstanceState> state)
    : state_(std::move(state)) {}

WasmParserModuleInstance::~WasmParserModuleInstance() = default;
WasmParserModuleInstance::WasmParserModuleInstance(WasmParserModuleInstance&& other) noexcept = default;
WasmParserModuleInstance& WasmParserModuleInstance::operator=(WasmParserModuleInstance&& other) noexcept = default;

Expected<WasmParserModuleInstance, WasmParserModuleCreateError> WasmParserModuleInstance::create(
    const WasmParserModule& module, uint32_t claim_index) {
  const auto reject =
      [](std::string message, ParserModuleFaultKind fault = ParserModuleFaultKind::kNone,
         WasmParserModuleCreateOutcome outcome =
             WasmParserModuleCreateOutcome::kError) -> Expected<WasmParserModuleInstance, WasmParserModuleCreateError> {
    return unexpected(
        WasmParserModuleCreateError{
            .outcome = outcome,
            .fault = fault,
            .message = std::move(message),
        });
  };
  if (!module.valid()) {
    return reject("cannot create an instance from an invalid wasm parser module");
  }
  if (claim_index >= module.state_->claim_ids.size()) {
    return reject("claim index is outside the wasm parser-module manifest");
  }
  const ParserModuleClaimKey key{module.state_->module_id, module.state_->claim_ids[claim_index]};
  const ParserModuleStrikeState initial_health = module.state_->strike_tracker->state(key);
  if (initial_health.health == ParserModuleClaimHealth::kDisabled) {
    return reject(
        "parser-module claim is disabled for the session", ParserModuleFaultKind::kNone,
        WasmParserModuleCreateOutcome::kAdmissionDecline);
  }
  auto admission = module.state_->session_budget->admitInstance(module.state_->module_id);
  if (!admission.accepted()) {
    return reject(
        std::move(admission.diagnostic), ParserModuleFaultKind::kNone,
        WasmParserModuleCreateOutcome::kAdmissionDecline);
  }
  auto state = std::make_unique<detail::WasmParserModuleInstanceState>();
  state->module = module.state_;
  state->claim_index = claim_index;
  state->instance_budget_reserved = true;
  state->recreation_pending = initial_health.health == ParserModuleClaimHealth::kQuarantined;
  state->store = wasm_store_new(state->module->engine);
  if (state->store == nullptr) {
    return reject("failed to create a Wasmer store");
  }

  const wasm_extern_vec_t imports = WASM_EMPTY_VEC;
  wasm_trap_t* instantiation_trap = nullptr;
  state->instance = wasm_instance_new(state->store, state->module->module, &imports, &instantiation_trap);
  if (state->instance == nullptr) {
    if (instantiation_trap != nullptr) {
      const std::string message = "wasm instantiation failed: " + trapMessage(instantiation_trap);
      (void)state->recordContractViolation(message);
      return reject(message, ParserModuleFaultKind::kContractViolation);
    }
    return reject("Wasmer failed to instantiate the parser module");
  }
  auto exports = bindRuntimeExports(state.get());
  if (!exports) {
    (void)state->recordContractViolation(exports.error());
    return reject(exports.error(), ParserModuleFaultKind::kContractViolation);
  }
  auto initialized = state->callVoid("_initialize", state->initialize, {});
  if (!initialized) {
    const std::string message = "parser-module _initialize failed: " + initialized.error();
    (void)state->recordContractViolation(message);
    return reject(message, ParserModuleFaultKind::kContractViolation);
  }
  auto abi = state->callI32(PJ_MODULE_ABI_EXPORT_NAME, state->abi, {});
  if (!abi) {
    const std::string message = "pj_module_abi failed: " + abi.error();
    (void)state->recordContractViolation(message);
    return reject(message, ParserModuleFaultKind::kContractViolation);
  }
  if (static_cast<uint32_t>(*abi) != PJ_PARSER_MODULE_ABI_VERSION) {
    const std::string message = "wasm parser module ABI mismatch (expected " +
                                std::to_string(PJ_PARSER_MODULE_ABI_VERSION) + ", got " +
                                std::to_string(static_cast<uint32_t>(*abi)) + ")";
    (void)state->recordContractViolation(message);
    return reject(message, ParserModuleFaultKind::kContractViolation);
  }

  wasm_val_t arguments[1] = {WASM_I32_VAL(static_cast<int32_t>(claim_index))};
  auto token = state->callI64(PJ_MODULE_CREATE_EXPORT_NAME, state->create, arguments);
  if (!token) {
    const std::string message = "pj_module_create failed: " + token.error();
    (void)state->recordContractViolation(message);
    return reject(message, ParserModuleFaultKind::kContractViolation);
  }
  state->token = static_cast<uint64_t>(*token);
  if (state->token == PJ_MODULE_CREATION_ERROR_TOKEN) {
    auto message = state->copyLastError(PJ_MODULE_CREATION_ERROR_TOKEN);
    if (!message) {
      (void)state->recordContractViolation(message.error());
      return reject(message.error(), ParserModuleFaultKind::kContractViolation);
    }
    return reject(*message);
  }
  return WasmParserModuleInstance(std::move(state));
}

Expected<void> WasmParserModuleInstance::recreateBoundInstance() {
  if (state_ == nullptr || state_->binding_bytes.empty()) {
    return unexpected(std::string("quarantined wasm parser-module instance has no accepted binding to replay"));
  }
  const uint32_t claim_index = state_->claim_index;
  const std::vector<uint8_t> binding_bytes = state_->binding_bytes;
  auto binding = parser_module::readBindingInfoV1(binding_bytes);
  if (!binding) {
    return unexpected("cannot decode the quarantined binding for replay: " + binding.error());
  }
  WasmParserModule module(state_->module);
  state_.reset();

  auto recreated = create(module, claim_index);
  if (!recreated) {
    return unexpected("quarantine recreation failed during create: " + recreated.error().message);
  }
  auto rebound = recreated->bind(*binding);
  if (!rebound) {
    return unexpected("quarantine recreation failed during bind: " + rebound.error());
  }
  if (rebound->outcome != ParserModuleBindOutcome::kAccept) {
    return unexpected("quarantine binding replay was not accepted: " + rebound->message);
  }
  state_ = std::move(recreated->state_);
  return {};
}

Expected<ParserModuleBindResult> WasmParserModuleInstance::bind(const parser_module::BindingInfoV1& info) {
  if (!valid()) {
    return unexpected(std::string("cannot bind an invalid wasm parser-module instance"));
  }
  if (info.claim_index != state_->claim_index) {
    return unexpected(std::string("BindingInfo claim_index does not match the created wasm instance"));
  }
  auto encoded = parser_module::writeBindingInfoV1(info);
  if (!encoded) {
    return unexpected(encoded.error());
  }
  auto address = state_->allocate(encoded->size());
  if (!address) {
    (void)state_->recordContractViolation(address.error());
    return bindContractViolation(PJ_MODULE_ERR_ALLOCATION_FAILURE, address.error());
  }
  GuestAllocation input_buffer(*state_, *address, encoded->size());
  auto guest_input = state_->memoryRange(input_buffer.address(), encoded->size());
  if (!guest_input) {
    (void)state_->recordContractViolation(guest_input.error());
    return bindContractViolation(PJ_MODULE_ERR_GENERIC, guest_input.error());
  }
  std::copy(encoded->begin(), encoded->end(), guest_input->begin());

  wasm_val_t arguments[3] = {
      wasmI64(state_->token),
      wasmI64(input_buffer.address()),
      wasmI64(encoded->size()),
  };
  auto code_result = state_->callI32(PJ_MODULE_BIND_EXPORT_NAME, state_->bind, arguments);
  auto released = input_buffer.release();
  if (!code_result) {
    state_->bound = false;
    (void)state_->recordContractViolation(code_result.error());
    return bindContractViolation(PJ_MODULE_ERR_GENERIC, code_result.error());
  }
  if (!released) {
    state_->bound = false;
    const std::string message = "pj_module_free failed after bind: " + released.error();
    (void)state_->recordContractViolation(message);
    return bindContractViolation(PJ_MODULE_ERR_GENERIC, message);
  }

  const int32_t code = *code_result;
  ParserModuleBindResult result{
      .outcome = ParserModuleBindOutcome::kError,
      .fault = ParserModuleFaultKind::kNone,
      .result_code = code,
      .message = {},
  };
  if (code == PJ_MODULE_OK) {
    result.outcome = ParserModuleBindOutcome::kAccept;
    state_->bound_route = info.route;
    state_->expected_object_type = info.expected_object_type;
    state_->bound = true;
    state_->binding_bytes = *encoded;
    if (state_->recreation_pending) {
      (void)state_->module->strike_tracker->markRecreated(state_->claimKey());
      state_->recreation_pending = false;
    }
    return result;
  }

  state_->bound = false;
  if (code == PJ_MODULE_DECLINE) {
    result.outcome = ParserModuleBindOutcome::kDecline;
  } else if (code < 0) {
    result.outcome = ParserModuleBindOutcome::kError;
    if (code == PJ_MODULE_ERR_BAD_TOKEN) {
      result.fault = ParserModuleFaultKind::kContractViolation;
    }
  } else {
    const std::string message = "pj_module_bind returned an out-of-contract positive result";
    (void)state_->recordContractViolation(message);
    return bindContractViolation(code, message);
  }

  auto message = state_->copyLastError(state_->token);
  if (!message) {
    result.fault = ParserModuleFaultKind::kContractViolation;
    result.message = message.error();
  } else {
    result.message = std::move(*message);
  }
  if (result.fault == ParserModuleFaultKind::kContractViolation) {
    (void)state_->recordContractViolation(result.message);
  }
  return result;
}

Expected<ParserModuleParseResult> WasmParserModuleInstance::parse(const parser_module::ParseInputV1& input) {
  if (!valid()) {
    return unexpected(std::string("cannot parse with an invalid wasm parser-module instance"));
  }
  if (!state_->bound) {
    return unexpected(std::string("cannot parse before an accepted wasm module bind"));
  }
  const ParserModuleStrikeState health = state_->module->strike_tracker->state(state_->claimKey());
  if (health.health == ParserModuleClaimHealth::kDisabled) {
    return contractViolation(PJ_MODULE_ERR_GENERIC, "parser-module claim is disabled for the session");
  }
  if (health.health == ParserModuleClaimHealth::kQuarantined) {
    auto recreated = recreateBoundInstance();
    if (!recreated) {
      return contractViolation(PJ_MODULE_ERR_GENERIC, recreated.error());
    }
  }

  const auto parse_once = [&]() -> Expected<ParserModuleParseResult> {
    auto encoded = parser_module::writeParseInputV1(input);
    if (!encoded) {
      return unexpected(encoded.error());
    }

    auto input_address = state_->allocate(encoded->size());
    if (!input_address) {
      return contractViolation(PJ_MODULE_ERR_ALLOCATION_FAILURE, input_address.error());
    }
    GuestAllocation input_buffer(*state_, *input_address, encoded->size());
    auto slots_address = state_->allocate(kOutputBlockBytes);
    if (!slots_address) {
      return contractViolation(PJ_MODULE_ERR_ALLOCATION_FAILURE, slots_address.error());
    }
    GuestAllocation slots_buffer(*state_, *slots_address, kOutputBlockBytes);

    auto guest_input = state_->memoryRange(input_buffer.address(), encoded->size());
    auto guest_slots = state_->memoryRange(slots_buffer.address(), kOutputBlockBytes);
    if (!guest_input || !guest_slots) {
      return contractViolation(PJ_MODULE_ERR_GENERIC, guest_input ? guest_slots.error() : guest_input.error());
    }
    std::copy(encoded->begin(), encoded->end(), guest_input->begin());
    std::fill(guest_slots->begin(), guest_slots->end(), uint8_t{0});

    wasm_val_t arguments[5] = {
        wasmI64(state_->token),
        wasmI64(input_buffer.address()),
        wasmI64(encoded->size()),
        wasmI64(slots_buffer.address()),
        wasmI64(slots_buffer.address() + kOutputSlotBytes),
    };
    auto code_result = state_->callI32(PJ_MODULE_PARSE_EXPORT_NAME, state_->parse, arguments);
    if (!code_result) {
      return contractViolation(PJ_MODULE_ERR_GENERIC, code_result.error());
    }

    const int32_t code = *code_result;
    if (code < 0) {
      auto message = state_->copyLastError(state_->token);
      auto released = releaseParseBuffers(slots_buffer, input_buffer);
      if (!message || !released) {
        return contractViolation(code, !message ? message.error() : released.error());
      }
      return ParserModuleParseResult{
          .fault = code == PJ_MODULE_ERR_BAD_TOKEN ? ParserModuleFaultKind::kContractViolation
                                                   : ParserModuleFaultKind::kDataError,
          .result_code = code,
          .message = std::move(*message),
          .output = std::nullopt,
      };
    }
    if (code != PJ_MODULE_OK) {
      return contractViolation(code, "pj_module_parse returned a nonzero non-error result");
    }

    // parse may grow or relocate memory. Re-acquire before reading both return
    // slots, then re-acquire again for the module-owned descriptor itself.
    auto returned_slots = state_->memoryRange(slots_buffer.address(), kOutputBlockBytes);
    if (!returned_slots) {
      return contractViolation(code, returned_slots.error());
    }
    const uint64_t output_address = decodeU64(returned_slots->first(kOutputSlotBytes));
    const uint64_t output_length = decodeU64(returned_slots->subspan(kOutputSlotBytes, kOutputSlotBytes));
    if (output_address == 0 || output_length == 0) {
      return contractViolation(code, "pj_module_parse returned an unreadable output descriptor");
    }
    auto guest_output = state_->memoryRange(output_address, output_length);
    if (!guest_output) {
      return contractViolation(code, guest_output.error());
    }
    const std::vector<uint8_t> descriptor_bytes(guest_output->begin(), guest_output->end());

    auto released = releaseParseBuffers(slots_buffer, input_buffer);
    if (!released) {
      return contractViolation(code, released.error());
    }

    auto descriptor = parser_module::readOutputDescriptorV1(descriptor_bytes);
    if (!descriptor) {
      return contractViolation(code, "malformed output descriptor: " + descriptor.error());
    }

    std::optional<ParserModuleOutput> output;
    if (state_->bound_route == parser_module::Route::kScalar) {
      const auto* scalar = std::get_if<parser_module::ScalarOutputV1>(&*descriptor);
      if (scalar == nullptr) {
        return contractViolation(code, "output descriptor route does not match the scalar binding");
      }
      auto owned = detail::ownScalarOutput(*scalar);
      if (!owned) {
        return contractViolation(code, owned.error());
      }
      output = ParserModuleOutput(std::move(*owned));
    } else {
      const auto* object = std::get_if<parser_module::ObjectOutputV1>(&*descriptor);
      if (object == nullptr) {
        return contractViolation(code, "output descriptor route does not match the object binding");
      }
      auto owned = detail::ownObjectOutput(*object, input.payload, state_->expected_object_type);
      if (!owned) {
        return contractViolation(code, owned.error());
      }
      output = ParserModuleOutput(std::move(*owned));
    }
    return ParserModuleParseResult{
        .fault = ParserModuleFaultKind::kNone,
        .result_code = code,
        .message = {},
        .output = std::move(output),
    };
  };

  auto result = parse_once();
  if (!result || result->fault != ParserModuleFaultKind::kContractViolation) {
    return result;
  }

  const ParserModuleStrikeState strike = state_->recordContractViolation(result->message);
  if (strike.health == ParserModuleClaimHealth::kQuarantined) {
    auto recreated = recreateBoundInstance();
    if (!recreated) {
      result->message += "; automatic quarantine recreation failed: " + recreated.error();
    } else {
      result->message += "; claim quarantined and recreated through create/bind replay";
    }
  } else if (strike.health == ParserModuleClaimHealth::kDisabled) {
    result->message += "; claim disabled for the session after repeat quarantine";
    state_.reset();
  }
  return result;
}

bool WasmParserModuleInstance::valid() const noexcept {
  return state_ != nullptr && state_->token != PJ_MODULE_CREATION_ERROR_TOKEN;
}

uint32_t WasmParserModuleInstance::claimIndex() const noexcept {
  return state_ == nullptr ? 0 : state_->claim_index;
}

ParserModuleStrikeState WasmParserModuleInstance::strikeState() const {
  return state_ == nullptr ? ParserModuleStrikeState{} : state_->module->strike_tracker->state(state_->claimKey());
}

std::string_view WasmParserModuleInstance::lifecycleDiagnostic() const noexcept {
  return state_ == nullptr ? std::string_view{} : std::string_view(state_->lifecycle_diagnostic);
}

}  // namespace PJ
