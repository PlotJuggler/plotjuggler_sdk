#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/occupancy_grid.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaOccupancyGrid = "PJ.OccupancyGrid";

/// Serializes sdk::OccupancyGrid to canonical PJ.OccupancyGrid wire bytes
/// (see pj_base/proto/pj/OccupancyGrid.proto).
[[nodiscard]] std::vector<uint8_t> serializeOccupancyGrid(const sdk::OccupancyGrid& grid);

/// Decodes canonical PJ.OccupancyGrid wire bytes. The returned object owns
/// its cell bytes via `anchor`.
[[nodiscard]] Expected<sdk::OccupancyGrid> deserializeOccupancyGrid(const uint8_t* data, size_t size);

}  // namespace PJ
