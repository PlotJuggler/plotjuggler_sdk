#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <any>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/builtin/builtin_object_codec.hpp"
#include "pj_base/builtin/grid_map_codec.hpp"
#include "pj_base/builtin_object_abi.h"
#include "pj_base/expected.hpp"
#include "pj_base/parser_module_abi.h"
#include "pj_base/span.hpp"
#include "pj_plugins/host/parser_module_runtime.hpp"

namespace PJ::detail {

inline ParserModuleParseResult contractViolation(int32_t code, std::string message) {
  return ParserModuleParseResult{
      .fault = ParserModuleFaultKind::kContractViolation,
      .result_code = code,
      .message = std::move(message),
      .output = std::nullopt,
  };
}

inline Expected<ParserModuleScalarOutput> ownScalarOutput(const parser_module::ScalarOutputV1& scalar) {
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

inline Expected<ParserModuleObjectOutput> ownObjectOutput(
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

}  // namespace PJ::detail
