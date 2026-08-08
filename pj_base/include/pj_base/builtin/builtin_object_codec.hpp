/**
 * @file builtin_object_codec.hpp
 * @brief Type-erased dispatch over every canonical builtin-object codec.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/builtin/robot_description_codec.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

/// Serialize a known BuiltinObject using its canonical wire codec.
[[nodiscard]] Expected<std::vector<uint8_t>> serializeBuiltinObject(const sdk::BuiltinObject& object);

/// Decode canonical wire bytes into the concrete type selected by @p type.
[[nodiscard]] Expected<sdk::BuiltinObject> deserializeBuiltinObject(
    sdk::BuiltinObjectType type, const uint8_t* data, size_t size);

}  // namespace PJ
