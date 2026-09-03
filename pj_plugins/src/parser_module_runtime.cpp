// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_plugins/host/parser_module_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "detail/native_parser_module_state.hpp"
#include "pj_base/builtin/builtin_object_codec.hpp"
#include "pj_base/builtin/grid_map_codec.hpp"
#include "pj_base/builtin_object_abi.h"
#include "pj_base/span.hpp"

namespace PJ {
namespace {

uint64_t addressOf(const void* pointer) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

Expected<std::string> copyLastError(const detail::NativeParserModuleState& module, uint64_t token) {
  std::array<char, PJ_MODULE_ERROR_BUFFER_SIZE> buffer{};
  const uint64_t written = module.last_error(token, addressOf(buffer.data()), buffer.size());
  if (written > buffer.size()) {
    return unexpected("pj_module_last_error returned a length larger than its buffer");
  }
  const auto length = static_cast<size_t>(written);
  const auto terminator = std::find(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(length), '\0');
  return std::string(buffer.begin(), terminator);
}

ParserModuleParseResult contractViolation(int32_t code, std::string message) {
  return ParserModuleParseResult{
      .fault = ParserModuleFaultKind::kContractViolation,
      .result_code = code,
      .message = std::move(message),
      .output = std::nullopt,
  };
}

Expected<ParserModuleScalarOutput> ownScalarOutput(const parser_module::ScalarOutputV1& scalar) {
  ParserModuleScalarOutput owned;
  owned.has_timestamp = scalar.has_timestamp;
  owned.timestamp_ns = scalar.timestamp_ns;
  owned.fields.reserve(scalar.fields.size());
  for (const auto& field : scalar.fields) {
    ParserModuleScalarValue value = std::visit(
        []<typename Value>(const Value& item) -> ParserModuleScalarValue {
          if constexpr (std::is_same_v<Value, std::string_view>) {
            return std::string(item);
          } else {
            return item;
          }
        },
        field.value);
    owned.fields.push_back(ParserModuleScalarField{.name = std::string(field.name), .value = std::move(value)});
  }
  return owned;
}

Expected<ParserModuleObjectOutput> ownObjectOutput(
    const parser_module::ObjectOutputV1& object, Span<const uint8_t> input_payload, uint16_t expected_type) {
  if (object.object_type != expected_type) {
    return unexpected(
        "output object type " + std::to_string(object.object_type) + " does not match bound type " +
        std::to_string(expected_type));
  }

  const auto type = static_cast<sdk::BuiltinObjectType>(object.object_type);
  auto decoded = deserializeBuiltinObject(type, object.wire.data(), object.wire.size());
  if (!decoded) {
    return unexpected("output canonical wire is malformed: " + decoded.error());
  }

  ParserModuleObjectOutput owned;
  owned.object = std::move(*decoded);
  owned.wire.assign(object.wire.begin(), object.wire.end());
  if (object.splice.has_value()) {
    uint32_t eligible_field = 0;
    if (!pj_builtin_object_splice_field_number_v1(object.object_type, &eligible_field) ||
        eligible_field != object.splice->field_number) {
      return unexpected("output splice field is not eligible for the object type");
    }
    const uint64_t payload_size = static_cast<uint64_t>(input_payload.size());
    if (object.splice->input_offset > payload_size ||
        object.splice->input_length > payload_size - object.splice->input_offset) {
      return unexpected("output splice range is outside the parse payload");
    }
    const auto offset = static_cast<size_t>(object.splice->input_offset);
    const auto length = static_cast<size_t>(object.splice->input_length);
    auto materialized = std::make_shared<std::vector<uint8_t>>(
        input_payload.begin() + static_cast<ptrdiff_t>(offset),
        input_payload.begin() + static_cast<ptrdiff_t>(offset + length));
    const auto attach = [&]<typename Object>() -> bool {
      auto* typed = std::any_cast<Object>(&owned.object);
      if (typed == nullptr) {
        return false;
      }
      typed->data = Span<const uint8_t>(materialized->data(), materialized->size());
      typed->anchor = materialized;
      return true;
    };
    bool attached = false;
    switch (type) {
      case sdk::BuiltinObjectType::kImage:
        attached = attach.template operator()<sdk::Image>();
        break;
      case sdk::BuiltinObjectType::kPointCloud:
        attached = attach.template operator()<sdk::PointCloud>();
        break;
      case sdk::BuiltinObjectType::kDepthImage:
        attached = attach.template operator()<sdk::DepthImage>();
        break;
      case sdk::BuiltinObjectType::kOccupancyGrid:
        attached = attach.template operator()<sdk::OccupancyGrid>();
        break;
      case sdk::BuiltinObjectType::kCompressedPointCloud:
        attached = attach.template operator()<sdk::CompressedPointCloud>();
        break;
      case sdk::BuiltinObjectType::kMesh3D:
        attached = attach.template operator()<sdk::Mesh3D>();
        break;
      case sdk::BuiltinObjectType::kVideoFrame:
        attached = attach.template operator()<sdk::VideoFrame>();
        break;
      case sdk::BuiltinObjectType::kOccupancyGridUpdate:
        attached = attach.template operator()<sdk::OccupancyGridUpdate>();
        break;
      case sdk::BuiltinObjectType::kVoxelGrid:
        attached = attach.template operator()<sdk::VoxelGrid>();
        break;
      case sdk::BuiltinObjectType::kGridMap:
        attached = attach.template operator()<sdk::GridMap>();
        break;
      default:
        break;
    }
    if (!attached) {
      return unexpected("output splice could not be attached to its canonical object");
    }
    // GridMap decodes header-only for splices; the data-length check waits
    // until the bytes exist, which is now.
    if (type == sdk::BuiltinObjectType::kGridMap) {
      if (auto valid = validateGridMap(*std::any_cast<sdk::GridMap>(&owned.object)); !valid) {
        return unexpected("output spliced GridMap layout is invalid: " + valid.error());
      }
    }
    owned.splice = ParserModuleObjectSplice{
        .field_number = object.splice->field_number,
        .input_offset = object.splice->input_offset,
        .payload_bytes = *materialized,
    };
  }
  return owned;
}

}  // namespace

NativeParserModuleInstance::NativeParserModuleInstance(
    std::shared_ptr<const detail::NativeParserModuleState> module, uint64_t token, uint32_t claim_index)
    : module_(std::move(module)), token_(token), claim_index_(claim_index) {}

NativeParserModuleInstance::~NativeParserModuleInstance() {
  reset();
}

NativeParserModuleInstance::NativeParserModuleInstance(NativeParserModuleInstance&& other) noexcept
    : module_(std::move(other.module_)),
      token_(std::exchange(other.token_, PJ_MODULE_CREATION_ERROR_TOKEN)),
      claim_index_(other.claim_index_),
      bound_route_(other.bound_route_),
      expected_object_type_(other.expected_object_type_),
      bound_(other.bound_) {
  other.bound_ = false;
}

NativeParserModuleInstance& NativeParserModuleInstance::operator=(NativeParserModuleInstance&& other) noexcept {
  if (this != &other) {
    reset();
    module_ = std::move(other.module_);
    token_ = std::exchange(other.token_, PJ_MODULE_CREATION_ERROR_TOKEN);
    claim_index_ = other.claim_index_;
    bound_route_ = other.bound_route_;
    expected_object_type_ = other.expected_object_type_;
    bound_ = other.bound_;
    other.bound_ = false;
  }
  return *this;
}

Expected<NativeParserModuleInstance> NativeParserModuleInstance::create(
    const NativeParserModule& module, uint32_t claim_index) {
  if (!module.valid()) {
    return unexpected("cannot create an instance from an invalid native parser module");
  }
  const uint64_t token = module.state_->create(claim_index);
  if (token == PJ_MODULE_CREATION_ERROR_TOKEN) {
    auto message = copyLastError(*module.state_, PJ_MODULE_CREATION_ERROR_TOKEN);
    return unexpected(message ? *message : message.error());
  }
  return NativeParserModuleInstance(module.state_, token, claim_index);
}

Expected<ParserModuleBindResult> NativeParserModuleInstance::bind(const parser_module::BindingInfoV1& info) {
  if (!valid()) {
    return unexpected("cannot bind an invalid native parser-module instance");
  }
  if (info.claim_index != claim_index_) {
    return unexpected("BindingInfo claim_index does not match the created instance");
  }
  auto encoded = parser_module::writeBindingInfoV1(info);
  if (!encoded) {
    return unexpected(encoded.error());
  }
  const int32_t code = module_->bind(token_, addressOf(encoded->data()), encoded->size());

  ParserModuleBindResult result{
      .outcome = ParserModuleBindOutcome::kError,
      .fault = ParserModuleFaultKind::kNone,
      .result_code = code,
      .message = {},
  };
  if (code == PJ_MODULE_OK) {
    result.outcome = ParserModuleBindOutcome::kAccept;
    bound_route_ = info.route;
    expected_object_type_ = info.expected_object_type;
    bound_ = true;
    return result;
  }

  bound_ = false;
  if (code == PJ_MODULE_DECLINE) {
    result.outcome = ParserModuleBindOutcome::kDecline;
  } else if (code < 0) {
    result.outcome = ParserModuleBindOutcome::kError;
    if (code == PJ_MODULE_ERR_BAD_TOKEN) {
      result.fault = ParserModuleFaultKind::kContractViolation;
    }
  } else {
    result.outcome = ParserModuleBindOutcome::kError;
    result.fault = ParserModuleFaultKind::kContractViolation;
    result.message = "pj_module_bind returned an out-of-contract positive result";
    return result;
  }

  auto message = copyLastError(*module_, token_);
  if (!message) {
    result.fault = ParserModuleFaultKind::kContractViolation;
    result.message = message.error();
  } else {
    result.message = std::move(*message);
  }
  return result;
}

Expected<ParserModuleParseResult> NativeParserModuleInstance::parse(const parser_module::ParseInputV1& input) {
  if (!valid()) {
    return unexpected("cannot parse with an invalid native parser-module instance");
  }
  if (!bound_) {
    return unexpected("cannot parse before an accepted module bind");
  }
  auto encoded = parser_module::writeParseInputV1(input);
  if (!encoded) {
    return unexpected(encoded.error());
  }

  uint64_t output_address = 0;
  uint64_t output_length = 0;
  const int32_t code = module_->parse(
      token_, addressOf(encoded->data()), encoded->size(), addressOf(&output_address), addressOf(&output_length));
  if (code < 0) {
    auto message = copyLastError(*module_, token_);
    if (!message) {
      return contractViolation(code, message.error());
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
  if (output_address == 0 || output_length == 0 || output_length > std::numeric_limits<size_t>::max()) {
    return contractViolation(code, "pj_module_parse returned an unreadable output descriptor");
  }

  const auto* output_bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(output_address));
  auto descriptor =
      parser_module::readOutputDescriptorV1(Span<const uint8_t>(output_bytes, static_cast<size_t>(output_length)));
  if (!descriptor) {
    return contractViolation(code, "malformed output descriptor: " + descriptor.error());
  }

  if (bound_route_ == parser_module::Route::kScalar) {
    const auto* scalar = std::get_if<parser_module::ScalarOutputV1>(&*descriptor);
    if (scalar == nullptr) {
      return contractViolation(code, "output descriptor route does not match the scalar binding");
    }
    auto owned = ownScalarOutput(*scalar);
    if (!owned) {
      return contractViolation(code, owned.error());
    }
    return ParserModuleParseResult{
        .fault = ParserModuleFaultKind::kNone,
        .result_code = code,
        .message = {},
        .output = ParserModuleOutput(std::move(*owned)),
    };
  }

  const auto* object = std::get_if<parser_module::ObjectOutputV1>(&*descriptor);
  if (object == nullptr) {
    return contractViolation(code, "output descriptor route does not match the object binding");
  }
  auto owned = ownObjectOutput(*object, input.payload, expected_object_type_);
  if (!owned) {
    return contractViolation(code, owned.error());
  }
  return ParserModuleParseResult{
      .fault = ParserModuleFaultKind::kNone,
      .result_code = code,
      .message = {},
      .output = ParserModuleOutput(std::move(*owned)),
  };
}

void NativeParserModuleInstance::reset() noexcept {
  if (module_ != nullptr && token_ != PJ_MODULE_CREATION_ERROR_TOKEN) {
    module_->destroy(token_);
  }
  token_ = PJ_MODULE_CREATION_ERROR_TOKEN;
  bound_ = false;
  module_.reset();
}

ParserModuleStrikeState ParserModuleStrikeTracker::recordFault(
    const ParserModuleClaimKey& key, ParserModuleFaultKind fault) {
  auto [it, inserted] = states_.try_emplace(key);
  (void)inserted;
  auto& state = it->second;
  if (fault != ParserModuleFaultKind::kContractViolation || state.health != ParserModuleClaimHealth::kActive) {
    return state;
  }

  ++state.strikes;
  if (state.strikes == 3) {
    state.strikes = 0;
    ++state.quarantine_count;
    if (state.quarantine_count == 1) {
      state.health = ParserModuleClaimHealth::kQuarantined;
    } else {
      state.health = ParserModuleClaimHealth::kDisabled;
    }
  }
  return state;
}

bool ParserModuleStrikeTracker::markRecreated(const ParserModuleClaimKey& key) {
  auto it = states_.find(key);
  if (it == states_.end() || it->second.health != ParserModuleClaimHealth::kQuarantined) {
    return false;
  }
  it->second.health = ParserModuleClaimHealth::kActive;
  return true;
}

ParserModuleStrikeState ParserModuleStrikeTracker::state(const ParserModuleClaimKey& key) const {
  const auto it = states_.find(key);
  return it == states_.end() ? ParserModuleStrikeState{} : it->second;
}

}  // namespace PJ
