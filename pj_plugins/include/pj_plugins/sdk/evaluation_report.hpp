/**
 * @file evaluation_report.hpp
 * @brief Parse the report poll_evaluation returns ("pj.data_processors.v1"), so
 *        every client reads it one way.
 *
 * COMPLETED and FAILED share one report shape; the schema is normative in the
 * poll_evaluation doc-comment of pj_base/plugin_data_api.h. The parser keeps the
 * contract's client rules: unknown keys are ignored, an unrecognised value of a
 * known key (coverage.stopped, outputs[].status) fails the parse, and every *_ns
 * must be a JSON integer in int64 range (never read through a double).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/time_math.hpp"

namespace PJ::sdk {

/// Why an evaluation ended (coverage.stopped).
/// @since 0.37.0
enum class EvaluationStop {
  kComplete,
  kBudgetTime,
  kBudgetEvaluations,
  kBudgetBytes,
  kBudgetReport,
  kCancelled,
  kError
};

/// The status of one output in a bundle.
/// @since 0.37.0
enum class EvaluationOutputStatus { kOk, kUnavailable, kEmpty, kError };

/// One output of a bundle.
/// @since 0.37.0
struct EvaluationOutput {
  EvaluationOutputStatus status = EvaluationOutputStatus::kOk;
  nlohmann::json value;    ///< a number or a string; null when absent
  nlohmann::json summary;  ///< an object output's summary (objects never travel as bytes); null when absent
  std::string reason;      ///< why "unavailable" or "error"; empty when absent
};

/// The result at one evaluated instant.
/// @since 0.37.0
struct EvaluationBundle {
  int64_t requested_ns = 0;
  /// Derived: the newest resolved input timestamp, or requested_ns when there are no inputs.
  int64_t stamp_ns = 0;
  std::map<std::string, int64_t> inputs;  ///< input alias -> timestamp of the sample it resolved to
  std::map<std::string, EvaluationOutput> outputs;
};

/// A retention gap the evaluated range fell into (raw ns).
/// @since 0.37.0
struct EvaluationGap {
  int64_t from_ns = 0;
  int64_t to_ns = 0;
};

/// A parsed poll_evaluation report. The first five members are the `coverage` object.
/// @since 0.37.0
struct EvaluationReport {
  uint64_t candidates = 0;
  uint64_t evaluated = 0;
  EvaluationStop stopped = EvaluationStop::kComplete;
  std::string error;  ///< the reason, set when stopped == kError
  std::vector<EvaluationGap> gaps;
  std::vector<EvaluationBundle> bundles;
  /// The root "outputs": what the host inferred for a request with no declared outputs.
  std::vector<DataProcessorOutput> inferred_outputs;
};

namespace detail {

[[nodiscard]] inline std::optional<int64_t> reportInt64(const nlohmann::json& value) {
  if (value.is_number_unsigned()) {
    return toSignedTicks(value.get<uint64_t>());
  }
  return value.is_number_integer() ? std::optional<int64_t>(value.get<int64_t>()) : std::nullopt;
}

[[nodiscard]] inline std::optional<int64_t> reportInt64(const nlohmann::json& object, const char* key) {
  const auto it = object.find(key);
  return it == object.end() ? std::nullopt : reportInt64(*it);
}

[[nodiscard]] inline std::optional<std::string> reportString(const nlohmann::json& object, const char* key) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    return std::nullopt;
  }
  return it->get<std::string>();
}

template <typename Enum, std::size_t N>
[[nodiscard]] std::optional<Enum> reportEnum(
    const std::pair<std::string_view, Enum> (&names)[N], const std::optional<std::string>& text) {
  for (const auto& [name, value] : names) {
    if (text.has_value() && name == *text) {
      return value;
    }
  }
  return std::nullopt;
}

}  // namespace detail

/// Parse a poll_evaluation report (COMPLETED or FAILED). Fails on a raw NUL byte,
/// malformed JSON, a missing required key, any present key of the wrong type, an
/// unrecognised coverage.stopped or outputs[].status, a *_ns outside int64,
/// `evaluated > candidates`, and coverage.error present without stopped "error" or
/// missing with it.
/// @since 0.37.0
[[nodiscard]] inline Expected<EvaluationReport> parseEvaluationReport(std::string_view json) {
  static constexpr std::pair<std::string_view, EvaluationStop> kStops[] = {
      {"complete", EvaluationStop::kComplete},
      {"budget_time", EvaluationStop::kBudgetTime},
      {"budget_evaluations", EvaluationStop::kBudgetEvaluations},
      {"budget_bytes", EvaluationStop::kBudgetBytes},
      {"budget_report", EvaluationStop::kBudgetReport},
      {"cancelled", EvaluationStop::kCancelled},
      {"error", EvaluationStop::kError},
  };
  static constexpr std::pair<std::string_view, EvaluationOutputStatus> kStatuses[] = {
      {"ok", EvaluationOutputStatus::kOk},
      {"unavailable", EvaluationOutputStatus::kUnavailable},
      {"empty", EvaluationOutputStatus::kEmpty},
      {"error", EvaluationOutputStatus::kError},
  };

  // A NUL byte is never valid JSON text, and a parser may stop at it and accept a prefix.
  if (json.find('\0') != std::string_view::npos) {
    return unexpected("evaluation report contains a NUL byte");
  }
  nlohmann::json doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  if (!doc.is_object()) {  // also a parse error: is_discarded() is not an object
    return unexpected("evaluation report is not a JSON object");
  }
  const auto coverage = doc.find("coverage");
  const auto bundles = doc.find("bundles");
  if (coverage == doc.end() || !coverage->is_object() || bundles == doc.end() || !bundles->is_array()) {
    return unexpected("evaluation report needs a coverage object and a bundles array");
  }

  EvaluationReport report;
  const auto candidates = coverage->find("candidates");
  const auto evaluated = coverage->find("evaluated");
  if (candidates == coverage->end() || !candidates->is_number_unsigned() || evaluated == coverage->end() ||
      !evaluated->is_number_unsigned()) {
    return unexpected("coverage.candidates and coverage.evaluated must be unsigned integers");
  }
  report.candidates = candidates->get<uint64_t>();
  report.evaluated = evaluated->get<uint64_t>();
  if (report.evaluated > report.candidates) {
    return unexpected("coverage.evaluated exceeds coverage.candidates");
  }
  const std::optional<std::string> stopped = detail::reportString(*coverage, "stopped");
  const std::optional<EvaluationStop> stop = detail::reportEnum(kStops, stopped);
  if (!stop.has_value()) {
    return unexpected("unknown coverage.stopped '" + stopped.value_or("") + "'");
  }
  report.stopped = *stop;
  const auto error = coverage->find("error");
  if ((error != coverage->end()) != (report.stopped == EvaluationStop::kError)) {
    return unexpected("coverage.error must be present exactly when coverage.stopped is 'error'");
  }
  if (error != coverage->end()) {
    if (!error->is_string()) {
      return unexpected("coverage.error must be a string");
    }
    report.error = std::move(error->get_ref<std::string&>());
  }
  const auto gaps = coverage->find("gaps");
  if (gaps == coverage->end() || !gaps->is_array()) {
    return unexpected("coverage.gaps must be an array");
  }
  for (const auto& gap : *gaps) {
    const auto from = detail::reportInt64(gap, "from_ns");
    const auto to = detail::reportInt64(gap, "to_ns");
    if (!from.has_value() || !to.has_value()) {
      return unexpected("coverage.gaps entries need int64 from_ns and to_ns");
    }
    report.gaps.push_back(EvaluationGap{*from, *to});
  }

  for (auto& entry : *bundles) {
    EvaluationBundle bundle;
    const auto requested = detail::reportInt64(entry, "requested_ns");
    const auto inputs = entry.find("inputs");
    const auto outputs = entry.find("outputs");
    if (!requested.has_value() || inputs == entry.end() || !inputs->is_object() || outputs == entry.end() ||
        !outputs->is_object()) {
      return unexpected("a bundle needs an int64 requested_ns, an inputs object and an outputs object");
    }
    bundle.requested_ns = *requested;
    std::optional<int64_t> newest;
    for (const auto& input : inputs->items()) {
      const auto resolved = detail::reportInt64(input.value());
      if (!resolved.has_value()) {
        return unexpected("bundle input '" + input.key() + "' must be an int64 timestamp");
      }
      bundle.inputs.emplace(input.key(), *resolved);
      newest = newest.has_value() ? std::max(*newest, *resolved) : *resolved;
    }
    bundle.stamp_ns = newest.value_or(bundle.requested_ns);
    for (auto& item : outputs->items()) {
      nlohmann::json& fields = item.value();
      const std::optional<std::string> status = detail::reportString(fields, "status");
      const std::optional<EvaluationOutputStatus> parsed = detail::reportEnum(kStatuses, status);
      if (!parsed.has_value()) {
        return unexpected("output '" + item.key() + "' has unknown status '" + status.value_or("") + "'");
      }
      EvaluationOutput output;
      output.status = *parsed;
      if (const auto value = fields.find("value"); value != fields.end()) {
        if (!value->is_number() && !value->is_string()) {
          return unexpected("output '" + item.key() + "' value must be a number or a string");
        }
        output.value = std::move(*value);
      }
      if (const auto summary = fields.find("summary"); summary != fields.end()) {
        if (!summary->is_object()) {
          return unexpected("output '" + item.key() + "' summary must be an object");
        }
        output.summary = std::move(*summary);
      }
      if (const auto reason = fields.find("reason"); reason != fields.end()) {
        if (!reason->is_string()) {
          return unexpected("output '" + item.key() + "' reason must be a string");
        }
        output.reason = std::move(reason->get_ref<std::string&>());
      }
      bundle.outputs.emplace(item.key(), std::move(output));
    }
    report.bundles.push_back(std::move(bundle));
  }

  if (const auto inferred = doc.find("outputs"); inferred != doc.end()) {
    if (!inferred->is_array()) {
      return unexpected("report outputs must be an array");
    }
    for (const auto& entry : *inferred) {
      auto name = detail::reportString(entry, "name");
      auto type = detail::reportString(entry, "type");
      if (!name.has_value() || !type.has_value()) {
        return unexpected("report outputs entries need a string name and type");
      }
      report.inferred_outputs.push_back(DataProcessorOutput{std::move(*name), std::move(*type)});
    }
  }
  return report;
}

}  // namespace PJ::sdk
