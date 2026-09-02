#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/grid_map.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaGridMap = "PJ.GridMap";

/// Serializes sdk::GridMap to canonical PJ.GridMap wire bytes
/// (see pj_base/proto/pj/GridMap.proto). The struct is written as-is; layout
/// consistency is checked on decode.
[[nodiscard]] std::vector<uint8_t> serializeGridMap(const sdk::GridMap& grid);

/// Decodes canonical PJ.GridMap wire bytes. The returned object owns its cell
/// bytes via `anchor`. Rejects a layout the cell math could not index safely:
/// a zero stride with cells declared, a row shorter than its columns, `data`
/// shorter than `row_count * row_stride`, or a field reaching past `cell_stride`.
[[nodiscard]] Expected<sdk::GridMap> deserializeGridMap(const uint8_t* data, size_t size);

}  // namespace PJ
