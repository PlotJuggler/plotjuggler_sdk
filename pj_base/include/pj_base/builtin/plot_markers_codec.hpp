#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/plot_markers.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

/// Wire-format identifier for canonical plot markers.
inline constexpr std::string_view kSchemaPlotMarkers = "PJ.PlotMarkers";

/// Serializes a sdk::PlotMarkers to canonical PJ.PlotMarkers wire bytes.
[[nodiscard]] std::vector<uint8_t> serializePlotMarkers(const sdk::PlotMarkers& markers);

/// Decodes canonical PJ.PlotMarkers wire bytes into sdk::PlotMarkers.
///
/// An EMPTY buffer decodes to an empty set — the canonical "no markers"
/// tombstone a producer publishes to clear its output (the object store is
/// replace-only; markers are overwritten, never deleted). Returns an error for
/// null-with-size, truncated, or malformed payloads.
[[nodiscard]] Expected<sdk::PlotMarkers> deserializePlotMarkers(const uint8_t* data, size_t size);

}  // namespace PJ
