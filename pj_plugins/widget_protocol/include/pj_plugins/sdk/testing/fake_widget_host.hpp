/**
 * @file fake_widget_host.hpp
 * @brief In-memory "pj.widget_host.v1" host for Widget plugin unit tests.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pj_plugins/widget_protocol.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <pj_plugins/host/widget_host_bridge.hpp>
#include <pj_plugins/widget_types.hpp>
#include <string>
#include <utility>
#include <vector>

namespace PJ::sdk::testing {

/// Widget host for single-threaded tests, served through the real PJ::WidgetHostBridge so the plugin
/// sees the C ABI. It holds samples per wire and answers `query_history` as the protocol states it:
/// display-time seconds, inclusive at both ends, ascending time, `max_samples == 0` = `default_cap`,
/// an explicit cap honoured, `out_total` = samples in the range before any cap.
///
/// Times are stored as a real host stores them, as integer nanoseconds, and shown on the display axis
/// as the seconds from `display_offset`. Far from the display origin a double does not hold every
/// nanosecond (near 1.7e9 s it steps ~238 ns), so samples a few hundred nanoseconds apart share one
/// display time, as they do in the application. Put the samples at epoch nanoseconds with a zero
/// offset to test that case; at small display times every nanosecond is distinct.
///
/// The fake must outlive the handle from makeHost().
///
/// @since 0.41.0
class FakeWidgetHost {
 public:
  struct Sample {
    std::chrono::nanoseconds store_time{0};
    PJ_widget_value_kind_t kind = PJ_WIDGET_VALUE_OBJECT;
    // Every member has an initializer, so designated initializers may skip any of them under
    // -Wmissing-field-initializers.
    double number = 0.0;           ///< kind NUMBER
    std::string text{};            ///< kind TEXT
    std::vector<uint8_t> bytes{};  ///< kind OBJECT
    /// Counted in a range but never delivered, as an entry whose fetch or encoding fails on a real host.
    bool undeliverable = false;
  };
  struct Wire {
    std::string label;
    std::string type_name;        ///< of the OBJECT samples
    std::vector<Sample> samples;  ///< ascending store_time; equal times keep arrival order
  };
  using WireKey = std::pair<uint32_t, uint32_t>;  ///< (input, slot)

  std::map<WireKey, Wire> wires;
  /// Store time shown as display time 0.
  std::chrono::nanoseconds display_offset{0};
  uint32_t default_cap = 10'000;                             ///< cap when max_samples is 0
  uint32_t hard_cap = std::numeric_limits<uint32_t>::max();  ///< clamp on an explicit cap (older hosts)
  /// Non-empty: every history read fails with this message, as for a wire the host has not resolved.
  std::string fail_with;

  /// Every history read, in call order.
  std::vector<WidgetHistoryQuery> queries;
  /// Every report_widget_message, in call order.
  std::vector<std::pair<WidgetMessageLevel, std::string>> messages;
  int inputs_changed_calls = 0;

  FakeWidgetHost() {
    bridge_.history = [this](const WidgetHistoryQuery& query, const WidgetValueSink& sink) {
      return history(query, sink);
    };
    bridge_.inputs_changed = [this] { ++inputs_changed_calls; };
    bridge_.report_message = [this](WidgetMessageLevel level, std::string_view message) {
      messages.emplace_back(level, std::string(message));
    };
  }
  FakeWidgetHost(const FakeWidgetHost&) = delete;
  FakeWidgetHost& operator=(const FakeWidgetHost&) = delete;

  /// The fat pointer to register as sdk::WidgetHostService.
  [[nodiscard]] PJ_widget_host_t makeHost() {
    return bridge_.host();
  }

  /// The wire (input, slot), created when absent, with its label set.
  Wire& wire(uint32_t input, uint32_t slot, std::string label) {
    Wire& entry = wires[{input, slot}];
    entry.label = std::move(label);
    return entry;
  }

  /// Insert into an existing wire after any sample of equal time, as a store keeps arrival order.
  Sample& insert(uint32_t input, uint32_t slot, Sample sample) {
    auto& samples = wires.at({input, slot}).samples;
    const auto at = std::upper_bound(
        samples.begin(), samples.end(), sample.store_time,
        [](std::chrono::nanoseconds time, const Sample& held) { return time < held.store_time; });
    return *samples.insert(at, std::move(sample));
  }

  /// Display seconds -> store time at the current offset, rounded to the nearest nanosecond.
  [[nodiscard]] std::chrono::nanoseconds storeTime(double display_seconds) const {
    return std::chrono::round<std::chrono::nanoseconds>(std::chrono::duration<double>(display_seconds)) +
           display_offset;
  }
  /// Store time -> the display time, in seconds, the host hands to the plugin.
  [[nodiscard]] double displayTime(std::chrono::nanoseconds store_time) const {
    return std::chrono::duration<double>(store_time - display_offset).count();
  }

  /// The messages reported at @p level.
  [[nodiscard]] std::vector<std::string> messagesAt(WidgetMessageLevel level) const {
    std::vector<std::string> out;
    for (const auto& [at, text] : messages) {
      if (at == level) {
        out.push_back(text);
      }
    }
    return out;
  }

  /// What on_update carries at display time @p tracker: one value per wire in (input, slot) order,
  /// the latest sample at or before it, or NONE (label still set) when there is none or it is
  /// undeliverable.
  [[nodiscard]] std::vector<PJ_widget_value_t> values(double tracker) const {
    std::vector<PJ_widget_value_t> out;
    out.reserve(wires.size());
    for (const auto& [key, entry] : wires) {
      PJ_widget_value_t value{};
      value.input = key.first;
      value.slot = key.second;
      value.kind = PJ_WIDGET_VALUE_NONE;
      value.label = view(entry.label);
      const Sample* latest = nullptr;
      for (const Sample& sample : entry.samples) {
        if (displayTime(sample.store_time) > tracker) {
          break;
        }
        latest = &sample;
      }
      if (latest != nullptr && !latest->undeliverable) {
        fill(value, *latest, entry);
      }
      out.push_back(value);
    }
    return out;
  }

 private:
  static PJ_string_view_t view(const std::string& text) {
    return PJ_string_view_t{text.data(), text.size()};
  }

  void fill(PJ_widget_value_t& value, const Sample& sample, const Wire& entry) const {
    value.kind = sample.kind;
    value.time = displayTime(sample.store_time);
    value.number = sample.number;
    value.text = view(sample.text);
    if (sample.kind == PJ_WIDGET_VALUE_OBJECT) {
      value.bytes = PJ_bytes_view_t{sample.bytes.data(), sample.bytes.size()};
      value.type_name = view(entry.type_name);
    }
  }

  Expected<uint32_t> history(const WidgetHistoryQuery& query, const WidgetValueSink& sink) {
    queries.push_back(query);
    if (!fail_with.empty()) {
      return unexpected(fail_with);
    }
    const auto found = wires.find({query.input, query.slot});
    if (found == wires.end()) {
      return unexpected(
          std::string("input ") + std::to_string(query.input) + " slot " + std::to_string(query.slot) +
          " is not wired");
    }
    const Wire& entry = found->second;
    std::vector<size_t> in_range;
    for (size_t i = 0; i < entry.samples.size(); ++i) {
      const double time = displayTime(entry.samples[i].store_time);
      if (time >= query.from && time <= query.to) {
        in_range.push_back(i);
      }
    }
    const size_t total = in_range.size();
    // At least one, as on the application's host.
    const size_t cap =
        std::max<size_t>(1, query.max_samples == 0 ? default_cap : std::min(query.max_samples, hard_cap));
    // Over the cap: evenly spaced picks that keep the first and the last sample.
    std::vector<size_t> picked;
    if (total <= cap) {
      picked = std::move(in_range);
    } else {
      picked.reserve(cap);
      const size_t denominator = std::max<size_t>(cap - 1, 1);
      for (size_t j = 0; j < cap; ++j) {
        picked.push_back(in_range[j * (total - 1) / denominator]);
      }
    }
    for (size_t index : picked) {
      const Sample& sample = entry.samples[index];
      if (sample.undeliverable) {
        continue;
      }
      PJ_widget_value_t value{};
      value.input = query.input;
      value.slot = query.slot;
      value.label = view(entry.label);
      fill(value, sample, entry);
      if (!sink(value)) {
        break;
      }
    }
    return static_cast<uint32_t>(std::min<size_t>(total, std::numeric_limits<uint32_t>::max()));
  }

  WidgetHostBridge bridge_;
};

}  // namespace PJ::sdk::testing
