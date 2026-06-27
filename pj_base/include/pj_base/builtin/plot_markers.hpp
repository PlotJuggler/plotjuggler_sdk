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
#include <string_view>
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
  Timestamp t_start = 0;    ///< Region start · Event/Label time · (ValueBand: ignored).
  Timestamp t_end = 0;      ///< Region end · (others: ignored).
  double value_low = 0.0;   ///< ValueBand low · Event point value · (others: ignored).
  double value_high = 0.0;  ///< ValueBand high · (others: ignored).
  bool has_value = false;   ///< Event: `value_low` is a meaningful point value.

  // --- semantics / presentation (shared by every kind) ---
  MarkerStatus status = MarkerStatus::kNone;
  MarkerSeverity severity = MarkerSeverity::kInfo;
  std::string category;            ///< e.g. "overspeed", "state_transition".
  std::string label;               ///< Short title (tooltip / panel / Label content).
  std::string description;         ///< Optional longer text.
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

/// Reserved marker-topic name for dataset-global markers (drawn on every plot of
/// the dataset, regardless of which series it shows).
inline constexpr std::string_view kGlobalMarkerTopic = "__global__";

/// Marker sets live in the host ObjectStore as serialized PlotMarkers objects.
/// They sit under object topics in a reserved namespace so they never collide
/// with media object topics (images, point clouds). Producers (plugins, scripts,
/// host) and the plot overlay MUST agree on this mapping: the object topic for a
/// marker topic `T` is `kMarkerObjectTopicPrefix + T`. A producer owns its set
/// and republishes the whole PlotMarkers blob on every change (last-writer wins);
/// the store is never mutated marker-by-marker.
inline constexpr std::string_view kMarkerObjectTopicPrefix = "__markers__/";

/// Object-topic name carrying the marker set addressed to `marker_topic` (a
/// series field path, or kGlobalMarkerTopic for dataset-global markers).
[[nodiscard]] inline std::string markerObjectTopicName(std::string_view marker_topic) {
  std::string name(kMarkerObjectTopicPrefix);
  name.append(marker_topic);
  return name;
}

/// Reserved marker-topic infix for an EPHEMERAL preview set (a kind="markers" data
/// processor created with PJ_DATA_PROCESSOR_FLAG_EPHEMERAL). The host addresses a preview to the marker
/// topic `kPreviewMarkerTopic + <owner-id>` so its object topic sorts under the
/// marker namespace and renders like any set, yet is recognizable as throwaway and
/// excluded from session save. The host and the plot overlay MUST agree on this.
inline constexpr std::string_view kPreviewMarkerTopic = "__preview__/";

/// True if `marker_topic` (or its object-topic form) names an ephemeral preview set.
/// Accepts either the bare marker topic or the markerObjectTopicName() form.
[[nodiscard]] inline bool isPreviewMarkerTopic(std::string_view topic) {
  if (topic.starts_with(kMarkerObjectTopicPrefix)) {
    topic.remove_prefix(kMarkerObjectTopicPrefix.size());
  }
  return topic.starts_with(kPreviewMarkerTopic);
}

/// Per-series marker topic key from a curve's (topic_name, field_name). The
/// producer (e.g. the markers toolbox, via the host catalog_key_resolver) and
/// the plot overlay MUST build this key identically, so they share this helper.
/// Joins with a single '/', tolerating a field path that already carries a
/// leading '/': "/sensor/pressure" + "/data" → "/sensor/pressure/data" (not
/// the doubled "/sensor/pressure//data").
[[nodiscard]] inline std::string markerSeriesKey(std::string_view topic_name, std::string_view field_name) {
  std::string key(topic_name);
  if (field_name.empty()) {
    return key;  // no field → no separator (avoid a dangling trailing slash)
  }
  if (field_name.front() != '/') {
    key.push_back('/');
  }
  key.append(field_name);
  return key;
}

}  // namespace sdk
}  // namespace PJ
