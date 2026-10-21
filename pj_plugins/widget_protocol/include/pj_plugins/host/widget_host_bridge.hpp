/**
 * @file widget_host_bridge.hpp
 * @brief Host-side builder of the "pj.widget_host.v1" service from C++ callbacks.
 *
 * The host fills three std::function members and registers bridge.host() in
 * the ServiceRegistryBuilder it passes to WidgetHandle::bind, so it never
 * hand-writes the C vtable. One bridge per open panel; it must outlive the
 * handle bound to it.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pj_plugins/widget_protocol.h>

#include <cstdint>
#include <functional>
#include <pj_base/expected.hpp>
#include <pj_base/sdk/plugin_data_api.hpp>
#include <pj_plugins/host/widget_handle.hpp>
#include <pj_plugins/widget_types.hpp>
#include <string>
#include <string_view>

namespace PJ {

/// Arguments of one history read, as the plugin passed them.
struct WidgetHistoryQuery {
  uint32_t input = 0;
  uint32_t slot = 0;
  double from = 0.0;
  double to = 0.0;
  uint32_t max_samples = 0;  ///< 0 = host default cap
};

/// Receives one sample. The value (and what it points to) is valid only inside the call. Returns
/// false when the plugin asked to stop: deliver nothing more and return the total.
using WidgetValueSink = std::function<bool(const PJ_widget_value_t&)>;

/// Host implementation of the widget service. Non-copyable and non-movable: the registered
/// fat pointer refers to this object.
class WidgetHostBridge {
 public:
  /// Resolve the wire and call @p sink per sample in ascending time, at most `max_samples` of them
  /// (0 = the host's cap), stopping as soon as the sink returns false. Return the number of samples
  /// the range held before the cap or decimation (saturated at UINT32_MAX), or an error when the wire
  /// does not exist or the call is not allowed (off the main thread, inside a paint handler); the
  /// sink must not have been called then.
  /// Unset = history unsupported.
  std::function<Expected<uint32_t>(const WidgetHistoryQuery& query, const WidgetValueSink& sink)> history;
  /// Unset = ignored.
  std::function<void()> inputs_changed;
  /// Unset = dropped. A message view no valid plugin produces (null data with a size, or longer than
  /// WidgetHandle::kMaxInputTextBytes) arrives as a warning naming it, never read.
  std::function<void(WidgetMessageLevel level, std::string_view message)> report_message;

  WidgetHostBridge() = default;
  WidgetHostBridge(const WidgetHostBridge&) = delete;
  WidgetHostBridge& operator=(const WidgetHostBridge&) = delete;

  /// The fat pointer to register as sdk::WidgetHostService.
  [[nodiscard]] PJ_widget_host_t host() {
    return PJ_widget_host_t{this, &vtable()};
  }

 private:
  static const PJ_widget_host_vtable_t& vtable() {
    static const PJ_widget_host_vtable_t vt = {
        PJ_WIDGET_HOST_PROTOCOL_VERSION,
        sizeof(PJ_widget_host_vtable_t),
        &queryHistory,
        &notifyInputsChanged,
        &reportWidgetMessage,
    };
    return vt;
  }

  static bool queryHistory(
      void* ctx, uint32_t input, uint32_t slot, double from, double to, uint32_t max_samples,
      PJ_widget_value_sink_fn sink, void* user, uint32_t* out_total, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<WidgetHostBridge*>(ctx);
    if (out_total != nullptr) {
      *out_total = 0;
    }
    if (!self->history) {
      sdk::fillError(out_error, 1, "widget_host", "history is not available");
      return false;
    }
    try {
      const WidgetHistoryQuery query{input, slot, from, to, max_samples};
      // Once the plugin's sink said stop, it is never called again, even by a host callback that
      // ignores the false.
      bool stopped = false;
      auto total = self->history(query, [&](const PJ_widget_value_t& value) -> bool {
        if (stopped) {
          return false;
        }
        if (sink != nullptr && !sink(user, &value)) {
          stopped = true;
        }
        return !stopped;
      });
      if (!total) {
        sdk::fillError(out_error, 1, "widget_host", total.error());
        return false;
      }
      if (out_total != nullptr) {
        *out_total = *total;
      }
      return true;
    } catch (const std::exception& e) {
      sdk::fillError(out_error, 1, "widget_host", std::string("history threw: ") + e.what());
      return false;
    } catch (...) {
      sdk::fillError(out_error, 1, "widget_host", "history threw");
      return false;
    }
  }

  static void notifyInputsChanged(void* ctx) noexcept {
    auto* self = static_cast<WidgetHostBridge*>(ctx);
    try {
      if (self->inputs_changed) {
        self->inputs_changed();
      }
    } catch (...) {}
  }

  static void reportWidgetMessage(void* ctx, PJ_widget_message_level_t level, PJ_string_view_t message) noexcept {
    auto* self = static_cast<WidgetHostBridge*>(ctx);
    try {
      if (self->report_message) {
        auto text = detail::checkedWidgetView(message, WidgetHandle::kMaxInputTextBytes, "message");
        if (text) {
          self->report_message(static_cast<WidgetMessageLevel>(level), *text);
        } else {
          self->report_message(WidgetMessageLevel::kWarning, text.error());
        }
      }
    } catch (...) {}
  }
};

}  // namespace PJ
