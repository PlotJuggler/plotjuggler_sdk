#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file parser_module_runtime.hpp
 * @brief Native parser-module instance lifecycle and fault classification.
 *
 * The wrapper performs one serialized create/bind/parse/destroy lifecycle.
 * Module-owned descriptor views are decoded and copied before parse returns.
 * The independent strike tracker is deliberately pure, non-thread-safe state
 * so a host executor can apply its own scheduling and generation policy.
 */

#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/parser_module_abi.h"
#include "pj_plugins/host/native_parser_module.hpp"

namespace PJ {

enum class ParserModuleFaultKind : uint8_t {
  kNone,
  kDataError,
  kContractViolation,
};

enum class ParserModuleBindOutcome : uint8_t {
  kAccept,
  kDecline,
  kError,
};

struct ParserModuleBindResult {
  ParserModuleBindOutcome outcome = ParserModuleBindOutcome::kError;
  ParserModuleFaultKind fault = ParserModuleFaultKind::kNone;
  int32_t result_code = PJ_MODULE_ERR_GENERIC;
  std::string message;
};

using ParserModuleScalarValue = std::variant<double, int64_t, uint64_t, bool, std::string>;

struct ParserModuleScalarField {
  std::string name;
  ParserModuleScalarValue value;
};

struct ParserModuleScalarOutput {
  bool has_timestamp = false;
  int64_t timestamp_ns = 0;
  std::vector<ParserModuleScalarField> fields;
};

struct ParserModuleObjectSplice {
  uint32_t field_number = 0;
  uint64_t input_offset = 0;
  std::vector<uint8_t> payload_bytes;
};

struct ParserModuleObjectOutput {
  sdk::BuiltinObject object;
  std::vector<uint8_t> wire;
  std::optional<ParserModuleObjectSplice> splice;
};

using ParserModuleOutput = std::variant<ParserModuleScalarOutput, ParserModuleObjectOutput>;

struct ParserModuleParseResult {
  ParserModuleFaultKind fault = ParserModuleFaultKind::kNone;
  int32_t result_code = PJ_MODULE_OK;
  std::string message;
  std::optional<ParserModuleOutput> output;
};

/// Move-only owner of one native module instance token.
class NativeParserModuleInstance {
 public:
  NativeParserModuleInstance() = default;
  ~NativeParserModuleInstance();

  NativeParserModuleInstance(NativeParserModuleInstance&& other) noexcept;
  NativeParserModuleInstance& operator=(NativeParserModuleInstance&& other) noexcept;

  NativeParserModuleInstance(const NativeParserModuleInstance&) = delete;
  NativeParserModuleInstance& operator=(const NativeParserModuleInstance&) = delete;

  /// Create the manifest claim at claim_index. Token-zero creation diagnostics
  /// are copied into the returned error.
  [[nodiscard]] static Expected<NativeParserModuleInstance> create(
      const NativeParserModule& module, uint32_t claim_index);

  /// Bind this instance using the frozen BindingInfo v1 codec.
  [[nodiscard]] Expected<ParserModuleBindResult> bind(const parser_module::BindingInfoV1& info);

  /// Parse one message and consume the returned descriptor transactionally.
  [[nodiscard]] Expected<ParserModuleParseResult> parse(const parser_module::ParseInputV1& input);

  [[nodiscard]] bool valid() const noexcept {
    return token_ != PJ_MODULE_CREATION_ERROR_TOKEN;
  }

  [[nodiscard]] uint32_t claimIndex() const noexcept {
    return claim_index_;
  }

 private:
  NativeParserModuleInstance(
      std::shared_ptr<const detail::NativeParserModuleState> module, uint64_t token, uint32_t claim_index);

  void reset() noexcept;

  // module_ and token_ are always set and cleared together, so a valid() token
  // implies a non-null module_.
  std::shared_ptr<const detail::NativeParserModuleState> module_;
  uint64_t token_ = PJ_MODULE_CREATION_ERROR_TOKEN;
  uint32_t claim_index_ = 0;
  parser_module::Route bound_route_ = parser_module::Route::kScalar;
  uint16_t expected_object_type_ = 0;
  bool bound_ = false;
};

struct ParserModuleClaimKey {
  std::string module_id;
  std::string claim_id;

  auto operator<=>(const ParserModuleClaimKey&) const = default;
};

enum class ParserModuleClaimHealth : uint8_t {
  kActive,
  kQuarantined,
  kDisabled,
};

struct ParserModuleStrikeState {
  ParserModuleClaimHealth health = ParserModuleClaimHealth::kActive;
  uint8_t strikes = 0;
  uint8_t quarantine_count = 0;
};

/// Pure per-(module, claim) contract-fault state. Data errors never mutate it.
class ParserModuleStrikeTracker {
 public:
  [[nodiscard]] ParserModuleStrikeState recordFault(const ParserModuleClaimKey& key, ParserModuleFaultKind fault);

  /// Reactivate a first-time quarantine after the caller successfully replays
  /// create and bind for the same immutable binding descriptor.
  [[nodiscard]] bool markRecreated(const ParserModuleClaimKey& key);

  [[nodiscard]] ParserModuleStrikeState state(const ParserModuleClaimKey& key) const;

 private:
  std::map<ParserModuleClaimKey, ParserModuleStrikeState> states_;
};

}  // namespace PJ
