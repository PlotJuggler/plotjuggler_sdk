/**
 * @file streaming_dialog.hpp
 * @brief Dialog-side helpers shared by streaming sources with a connection
 *        panel: the parser-encoding selector and the topic-selection merge.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/sdk/text_utils.hpp"
#include "pj_plugins/sdk/widget_data.hpp"

namespace PJ {
namespace sdk {

/// Index of `selected` in `available`, or 0 when absent (the combo's first
/// entry is the fallback selection).
[[nodiscard]] inline int encodingIndex(const std::string& selected, const std::vector<std::string>& available) {
  const auto it = std::find(available.begin(), available.end(), selected);
  return it == available.end() ? 0 : static_cast<int>(std::distance(available.begin(), it));
}

/// The encoding at combo index `index`; out-of-range falls back to the first
/// available entry, or to `fallback` when nothing is available.
[[nodiscard]] inline std::string encodingAt(
    int index, const std::vector<std::string>& available, std::string_view fallback = "json") {
  if (index >= 0 && index < static_cast<int>(available.size())) {
    return available[static_cast<size_t>(index)];
  }
  return available.empty() ? std::string(fallback) : available.front();
}

/// Populate the parser-encoding combo consistently. Returns whether at least
/// one parser is available so callers can include that fact in their OK gate.
inline bool writeEncodingSelector(
    WidgetData& data, std::string_view widget_name, const std::vector<std::string>& available,
    const std::string& selected) {
  const bool enabled = !available.empty();
  const std::string name(widget_name);
  if (enabled) {
    data.setItems(name, available);
    data.setCurrentIndex(name, encodingIndex(selected, available));
  } else {
    data.setItems(name, {"(no parsers available)"});
    data.setCurrentIndex(name, 0);
    data.setEnabled(name, false);
  }
  return enabled;
}

/// The host reports only selections from currently rendered rows. Preserve old
/// selections it could not report, then accept the reported visible selection.
template <typename IsVisible, typename AcceptReported>
[[nodiscard]] std::vector<std::string> mergeVisibleSelection(
    const std::vector<std::string>& previous, const std::vector<std::string>& reported, IsVisible&& is_visible,
    AcceptReported&& accept_reported) {
  std::vector<std::string> result;
  result.reserve(previous.size() + reported.size());
  for (const auto& value : previous) {
    if (!is_visible(value)) {
      result.push_back(value);
    }
  }
  for (const auto& value : reported) {
    if (!accept_reported(value) || std::find(result.begin(), result.end(), value) != result.end()) {
      continue;
    }
    result.push_back(value);
  }
  return result;
}

/// True when `selection` is empty (no filter) or some entry projects to `topic`.
template <typename Selection, typename Projection>
[[nodiscard]] bool passesSelectionFilter(
    const std::string& topic, const Selection& selection, Projection&& projection) {
  return selection.empty() ||
         std::any_of(selection.begin(), selection.end(), [&](const auto& value) { return projection(value) == topic; });
}

}  // namespace sdk
}  // namespace PJ
