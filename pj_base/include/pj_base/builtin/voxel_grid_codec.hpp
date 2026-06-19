#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/voxel_grid.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaVoxelGrid = "PJ.VoxelGrid";

/// Serializes sdk::VoxelGrid to canonical PJ.VoxelGrid wire bytes
/// (see pj_base/proto/pj/VoxelGrid.proto).
[[nodiscard]] std::vector<uint8_t> serializeVoxelGrid(const sdk::VoxelGrid& grid);

/// Decodes canonical PJ.VoxelGrid wire bytes. The returned object owns its
/// voxel bytes via `anchor`.
[[nodiscard]] Expected<sdk::VoxelGrid> deserializeVoxelGrid(const uint8_t* data, size_t size);

}  // namespace PJ
