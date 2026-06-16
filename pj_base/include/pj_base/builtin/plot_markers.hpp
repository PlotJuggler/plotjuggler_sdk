/**
 * @file plot_markers.hpp
 * @brief Markers (findings) anchored to a time-series plot: shaded time
 *        regions, point events, value bands, and text labels.
 *
 * PlotMarkers is to a time-series plot what ImageAnnotations is to a video
 * frame — a canonical SDK builtin object with a wire codec — but its shape is
 * its own. Where ImageAnnotations groups heterogeneous drawing primitives
 * (points/circles/texts), a marker is a *homogeneous record* distinguished by a
 * `kind`, and a topic holds a flat list of them.
 *
 * A marker deliberately carries NO id (identity is owned by the host marker
 * store and surfaced by the marker API, like every other builtin which carries
 * none), NO source (no builtin records its creator — provenance is the
 * dataset/topic the marker lives under, with optional extras in `metadata`),
 * and NO scope (a marker's reach is decided by which topic it is addressed to:
 * a series topic vs. a dataset-global topic).
 *
 * Like ImageAnnotations, marker data is small and owned outright via
 * std::vector; eager ingestion is the natural default.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pj_base/builtin/image_annotations.hpp"  // for ColorRGBA
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// What a marker marks; selects which anchor fields are meaningful.
enum class MarkerKind : uint8_t {
  kRegion,     ///< time span [t_start, t_end] — shaded vertical band.
  kEvent,      ///< single time t_start (+ optional value) — tick / point.
  kValueBand,  ///< value span [value_low, value_high] — horizontal band (series-only).
  kLabel,      ///< text callout anchored at t_start.
};

/// The finding verdict carried by a marker.
enum class MarkerStatus : uint8_t {
  kNone,  ///< Not a pass/fail finding (a plain annotation).
  kPass,
  kFail,
};

/// Severity; drives the default color when `color` is unset (alpha 0).
enum class MarkerSeverity : uint8_t {
  kInfo,
  kWarning,
  kError,
  kCritical,
};

/// Free-form producer key/value metadata. The extension hatch that keeps the
/// schema stable as producers attach extra fields (threshold, peak, from/to,
/// ...) without a schema change. Serializes as a canonical PJ.KeyValuePair.
struct MarkerProperty {
  std::string key;
  std::string value;
  bool operator==(const MarkerProperty&) const = default;
};

/// One marker: a homogeneous, identity-less record. Identity (the delete
/// handle) is owned by the host marker store, not this value.
struct PlotMarker {
  MarkerKind kind = MarkerKind::kRegion;

  // --- anchor (interpret by kind; irrelevant fields ignored) ---
  Timestamp t_start = 0;       ///< Region start · Event/Label time · (ValueBand: ignored).
  Timestamp t_end = 0;         ///< Region end · (others: ignored).
  double value_low = 0.0;      ///< ValueBand low · Event point value · (others: ignored).
  double value_high = 0.0;     ///< ValueBand high · (others: ignored).
  bool has_value = false;      ///< Event: `value_low` is a meaningful point value.

  // --- semantics / presentation (shared by every kind) ---
  MarkerStatus status = MarkerStatus::kNone;
  MarkerSeverity severity = MarkerSeverity::kInfo;
  std::string category;        ///< e.g. "overspeed", "state_transition".
  std::string label;           ///< Short title (tooltip / panel / Label content).
  std::string description;     ///< Optional longer text.
  ColorRGBA color = {0, 0, 0, 0};  ///< a=0 → derive color from `severity`.
  std::vector<MarkerProperty> metadata;

  bool operator==(const PlotMarker&) const = default;
};

/// The canonical object a marker topic holds and the codec (de)serializes:
/// the set of markers for one topic (one series, or the dataset-global topic).
struct PlotMarkers {
  std::vector<PlotMarker> markers;
  bool operator==(const PlotMarkers&) const = default;

  /// True if there are no markers.
  [[nodiscard]] bool empty() const noexcept {
    return markers.empty();
  }
};

}  // namespace sdk
}  // namespace PJ
