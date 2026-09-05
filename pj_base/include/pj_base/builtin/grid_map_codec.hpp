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
/// bytes via `anchor`. Rejects a layout the cell math could not index safely
/// (see validateGridMap), except that a wire carrying no `data` decodes with an
/// empty span: that is the functional-v2 splice form, whose bytes the host
/// attaches afterwards, so the data length is left for validateGridMap.
[[nodiscard]] Expected<sdk::GridMap> deserializeGridMap(const uint8_t* data, size_t size);

/// Full layout check for a grid whose bytes are in place: every field has a
/// known datatype, a non-zero count and ends within `cell_stride`; with cells
/// declared, both strides are non-zero, a row holds its columns, and `data`
/// covers `row_count * row_stride`. Hosts call it right after attaching spliced
/// bytes; consumers that index cells call it again before trusting the layout.
[[nodiscard]] Expected<void> validateGridMap(const sdk::GridMap& grid);

}  // namespace PJ
