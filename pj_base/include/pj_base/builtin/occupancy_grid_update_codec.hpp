#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/occupancy_grid_update.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaOccupancyGridUpdate = "PJ.OccupancyGridUpdate";

/// Serializes sdk::OccupancyGridUpdate to canonical PJ.OccupancyGridUpdate wire
/// bytes (see pj_base/proto/pj/OccupancyGridUpdate.proto).
[[nodiscard]] std::vector<uint8_t> serializeOccupancyGridUpdate(const sdk::OccupancyGridUpdate& update);

/// Decodes canonical PJ.OccupancyGridUpdate wire bytes. The returned object
/// owns its cell bytes via `anchor`.
[[nodiscard]] Expected<sdk::OccupancyGridUpdate> deserializeOccupancyGridUpdate(const uint8_t* data, size_t size);

}  // namespace PJ
