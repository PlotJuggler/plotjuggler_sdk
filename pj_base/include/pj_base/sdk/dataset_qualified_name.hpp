#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Dataset-qualified series names — the naming contract for series addressed by
// string across the plugin boundary (normative statement in plugin_data_api.h,
// DATASET-QUALIFIED NAMES). A series' full identity is (dataset, topic, field);
// a bare "topic/field" name is an abbreviation that stops being unique the
// moment two loaded datasets share topic names. The canonical serialized
// identity is "dataset_source:topic/field" — the same form hosts print — and
// this header is the shared parser/composer for it, so hosts and plugins never
// drift.

#include <cstddef>
#include <string>
#include <string_view>

#include "pj_base/span.hpp"

namespace PJ::sdk {

/// Result of splitting a series name that may carry the dataset qualifier,
/// "dataset_source:topic/field".
/// @since 0.27.0
struct DatasetQualifierSplit {
  bool qualified = false;
  std::string dataset_source;  ///< the matched source name; empty when unqualified
  std::string bare;            ///< the name with the qualifier removed
};

/// Split `name` against the KNOWN dataset source names (longest match wins).
/// Matching against known names instead of parsing at ':' means source names
/// need no escaping — "[stream] UDP Server:/udp/data" works as-is — and a ':'
/// inside an ordinary name can never be misread as a qualifier.
/// @since 0.27.0
[[nodiscard]] inline DatasetQualifierSplit splitDatasetQualifier(
    std::string_view name, Span<const std::string> source_names) {
  DatasetQualifierSplit out;
  std::size_t best = 0;
  for (const std::string& src : source_names) {
    if (src.empty() || src.size() <= best || name.size() <= src.size() || name[src.size()] != ':' ||
        name.compare(0, src.size(), src) != 0) {
      continue;
    }
    best = src.size();
    out.dataset_source = src;
  }
  out.qualified = best != 0;
  out.bare = std::string(out.qualified ? name.substr(best + 1) : name);
  return out;
}

/// The canonical dataset-qualified form, "dataset_source:bare" — the inverse of
/// splitDatasetQualifier for a loaded source name.
/// @since 0.27.0
[[nodiscard]] inline std::string qualifiedSeriesName(std::string_view dataset_source, std::string_view bare) {
  std::string out;
  out.reserve(dataset_source.size() + 1 + bare.size());
  out.append(dataset_source);
  out.push_back(':');
  out.append(bare);
  return out;
}

}  // namespace PJ::sdk
