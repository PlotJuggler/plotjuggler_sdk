#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/log.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaLog = "PJ.Log";

/// Serializes sdk::Log to canonical PJ.Log wire bytes
/// (see pj_base/proto/pj/Log.proto).
[[nodiscard]] std::vector<uint8_t> serializeLog(const sdk::Log& log);

/// Decodes canonical PJ.Log wire bytes into an owned sdk::Log.
[[nodiscard]] Expected<sdk::Log> deserializeLog(const uint8_t* data, size_t size);

}  // namespace PJ
