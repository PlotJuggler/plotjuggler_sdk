#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Internal layout checks for byte-backed builtins whose records are described
// by a PointField list (MeshMap vertices and faces, MeshMapUpdate records).
//
// Inline-only; not exposed through the public include path.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/expected.hpp"

namespace PJ::builtin_wire {

/// Every field has a known datatype and a non-zero count and ends within
/// `stride`. `what` prefixes the error ("MeshMap vertex").
[[nodiscard]] inline Expected<void> validateRecordFields(
    const std::vector<sdk::PointField>& fields, uint32_t stride, std::string_view what) {
  for (const auto& field : fields) {
    if (field.datatype == sdk::PointField::Datatype::kUnknown) {
      return unexpected(std::string(what) + " field '" + field.name + "' has an unknown datatype");
    }
    if (field.count == 0) {
      return unexpected(std::string(what) + " field '" + field.name + "' has a zero count");
    }
    const uint64_t end = static_cast<uint64_t>(field.offset) +
                         (static_cast<uint64_t>(sdk::bytesPerElement(field.datatype)) * field.count);
    if (end > stride) {
      return unexpected(std::string(what) + " field '" + field.name + "' reaches past its stride");
    }
  }
  return {};
}

/// The field named `name` exists with exactly `datatype` and `count`.
[[nodiscard]] inline Expected<void> requireField(
    const std::vector<sdk::PointField>& fields, std::string_view name, sdk::PointField::Datatype datatype,
    uint32_t count, std::string_view what) {
  for (const auto& field : fields) {
    if (field.name == name) {
      if (field.datatype != datatype || field.count != count) {
        return unexpected(std::string(what) + " field '" + std::string(name) + "' has the wrong type or count");
      }
      return {};
    }
  }
  return unexpected(std::string(what) + " lacks the required field '" + std::string(name) + "'");
}

}  // namespace PJ::builtin_wire
