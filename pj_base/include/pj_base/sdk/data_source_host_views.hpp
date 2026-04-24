/**
 * @file data_source_host_views.hpp
 * @brief C++ wrappers over the DataSource runtime host vtable (v4).
 *
 * The runtime host is delivered to DataSource plugins via the service
 * registry under the canonical name `"pj.runtime.v1"`. This header wraps
 * the raw `PJ_data_source_runtime_host_t` fat pointer in an ergonomic
 * view that null-checks every call and maps ABI error out-params into
 * `PJ::Expected<T>` / `PJ::Status`.
 *
 * Plugin authors access the view through
 * `DataSourcePluginBase::runtimeHost()`; it is not constructed directly
 * in plugin code.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "pj_base/data_source_protocol.h"
#include "pj_base/expected.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ {

/// C++ mirror of PJ_data_source_state_t.
enum class DataSourceState : uint32_t {
  kIdle = PJ_DATA_SOURCE_STATE_IDLE,
  kConfiguring = PJ_DATA_SOURCE_STATE_CONFIGURING,
  kStarting = PJ_DATA_SOURCE_STATE_STARTING,
  kRunning = PJ_DATA_SOURCE_STATE_RUNNING,
  kPaused = PJ_DATA_SOURCE_STATE_PAUSED,
  kStopping = PJ_DATA_SOURCE_STATE_STOPPING,
  kStopped = PJ_DATA_SOURCE_STATE_STOPPED,
  kFailed = PJ_DATA_SOURCE_STATE_FAILED,
};

/// Severity level for plugin-to-host diagnostic messages.
enum class DataSourceMessageLevel : uint32_t {
  kInfo = PJ_DATA_SOURCE_MESSAGE_INFO,
  kWarning = PJ_DATA_SOURCE_MESSAGE_WARNING,
  kError = PJ_DATA_SOURCE_MESSAGE_ERROR,
};

/// Type of message box to display (determines icon).
enum class MessageBoxType : uint32_t {
  kInfo = PJ_MESSAGE_BOX_INFO,
  kWarning = PJ_MESSAGE_BOX_WARNING,
  kError = PJ_MESSAGE_BOX_ERROR,
  kQuestion = PJ_MESSAGE_BOX_QUESTION,
};

/// Standard buttons for message boxes (combinable with |).
enum class MessageBoxButton : int {
  kOk = PJ_MSG_BTN_OK,
  kCancel = PJ_MSG_BTN_CANCEL,
  kYes = PJ_MSG_BTN_YES,
  kNo = PJ_MSG_BTN_NO,
  kContinue = PJ_MSG_BTN_CONTINUE,
  kAbort = PJ_MSG_BTN_ABORT,
  kRetry = PJ_MSG_BTN_RETRY,
  kIgnore = PJ_MSG_BTN_IGNORE,
};

inline int operator|(MessageBoxButton a, MessageBoxButton b) {
  return static_cast<int>(a) | static_cast<int>(b);
}
inline int operator|(int a, MessageBoxButton b) {
  return a | static_cast<int>(b);
}

/// Capability flag constants mirrored from the C ABI.
constexpr uint64_t kCapabilityFiniteImport = PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT;
constexpr uint64_t kCapabilityContinuousStream = PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM;
constexpr uint64_t kCapabilityDirectIngest = PJ_DATA_SOURCE_CAPABILITY_DIRECT_INGEST;
constexpr uint64_t kCapabilityDelegatedIngest = PJ_DATA_SOURCE_CAPABILITY_DELEGATED_INGEST;
constexpr uint64_t kCapabilitySupportsPause = PJ_DATA_SOURCE_CAPABILITY_SUPPORTS_PAUSE;
constexpr uint64_t kCapabilityHasDialog = PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG;

using ParserBindingHandle = PJ_parser_binding_handle_t;

/// C++ mirror of PJ_parser_binding_request_t for delegated-ingest parser lookup.
struct ParserBindingRequest {
  std::string_view topic_name;
  std::string_view parser_encoding;
  std::string_view type_name;
  Span<const uint8_t> schema;
  std::string_view parser_config_json;
};

/// Convert a PJ_error_t populated by the ABI into a descriptive std::string.
/// Safe to call on a zero-initialized error (returns "unspecified error").
[[nodiscard]] inline std::string errorToString(const PJ_error_t& err) {
  std::string out;
  if (err.domain[0] != '\0') {
    out.append(err.domain);
    out.append(": ");
  }
  if (err.message[0] != '\0') {
    out.append(err.message);
  }
  if (out.empty()) {
    out = "unspecified error";
  }
  return out;
}

/**
 * Type-safe view over the runtime host vtable.
 *
 * Each method null-checks the underlying function pointer and maps the
 * ABI `bool + PJ_error_t*` convention into `PJ::Expected<T>` /
 * `PJ::Status` for idiomatic C++ usage. Calls on an unbound host return
 * errors rather than crashing.
 */
class DataSourceRuntimeHostView {
 public:
  DataSourceRuntimeHostView() = default;
  explicit DataSourceRuntimeHostView(PJ_data_source_runtime_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const noexcept {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  /// Send a diagnostic message to the host UI log. Never fails.
  void reportMessage(DataSourceMessageLevel level, std::string_view message) const {
    if (valid() && host_.vtable->report_message != nullptr) {
      host_.vtable->report_message(
          host_.ctx, static_cast<PJ_data_source_message_level_t>(level), sdk::toAbiString(message));
    }
  }

  /// Begin a progress bar. Returns an error if the host refused to start it.
  [[nodiscard]] Status progressStart(std::string_view label, uint64_t total_steps, bool cancellable) const {
    if (!valid() || host_.vtable->progress_start == nullptr) {
      return unexpected("runtime host is not bound");
    }
    PJ_error_t err{};
    if (!host_.vtable->progress_start(host_.ctx, sdk::toAbiString(label), total_steps, cancellable, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// Advance progress. Returns true to continue, false if the user cancelled.
  [[nodiscard]] bool progressUpdate(uint64_t current_step) const {
    if (!valid() || host_.vtable->progress_update == nullptr) {
      return false;
    }
    return host_.vtable->progress_update(host_.ctx, current_step);
  }

  /// End the current progress sequence.
  void progressFinish() const {
    if (valid() && host_.vtable->progress_finish != nullptr) {
      host_.vtable->progress_finish(host_.ctx);
    }
  }

  /// Returns true if the host has requested the plugin to stop.
  [[nodiscard]] bool isStopRequested() const {
    if (!valid() || host_.vtable->is_stop_requested == nullptr) {
      return false;
    }
    return host_.vtable->is_stop_requested(host_.ctx);
  }

  /// Inform the host that the plugin has transitioned to @p state.
  void notifyState(DataSourceState state) const {
    if (valid() && host_.vtable->notify_state != nullptr) {
      host_.vtable->notify_state(host_.ctx, static_cast<PJ_data_source_state_t>(state));
    }
  }

  /// Plugin-initiated stop. @p terminal_state should be kStopped or kFailed.
  void requestStop(DataSourceState terminal_state, std::string_view reason) const {
    if (valid() && host_.vtable->request_stop != nullptr) {
      host_.vtable->request_stop(
          host_.ctx, static_cast<PJ_data_source_state_t>(terminal_state), sdk::toAbiString(reason));
    }
  }

  /// Bind (or look up) a parser for delegated ingest.
  [[nodiscard]] Expected<ParserBindingHandle> ensureParserBinding(const ParserBindingRequest& request) const {
    if (!valid() || host_.vtable->ensure_parser_binding == nullptr) {
      return unexpected("runtime host is not bound");
    }

    PJ_parser_binding_request_t raw{
        .topic_name = sdk::toAbiString(request.topic_name),
        .parser_encoding = sdk::toAbiString(request.parser_encoding),
        .type_name = sdk::toAbiString(request.type_name),
        .schema = sdk::toAbiBytes(request.schema),
        .parser_config_json = sdk::toAbiString(request.parser_config_json),
    };

    ParserBindingHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_parser_binding(host_.ctx, &raw, &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  /// Push a raw message for host-side parsing via a previously obtained binding handle.
  [[nodiscard]] Status pushRawMessage(
      ParserBindingHandle handle, Timestamp host_timestamp_ns, Span<const uint8_t> payload) const {
    if (!valid() || host_.vtable->push_raw_message == nullptr) {
      return unexpected("runtime host is not bound");
    }
    PJ_error_t err{};
    if (!host_.vtable->push_raw_message(host_.ctx, handle, host_timestamp_ns, sdk::toAbiBytes(payload), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /**
   * Display a modal message box and wait for user response.
   * @return The button clicked, or kOk if the host does not support dialogs.
   */
  [[nodiscard]] MessageBoxButton showMessageBox(
      MessageBoxType type, std::string_view title, std::string_view message, int buttons = 0) const {
    if (!valid() || host_.vtable->show_message_box == nullptr) {
      if (buttons & static_cast<int>(MessageBoxButton::kContinue)) {
        return MessageBoxButton::kContinue;
      }
      if (buttons & static_cast<int>(MessageBoxButton::kYes)) {
        return MessageBoxButton::kYes;
      }
      return MessageBoxButton::kOk;
    }
    int result = host_.vtable->show_message_box(
        host_.ctx, static_cast<PJ_message_box_type_t>(type), sdk::toAbiString(title), sdk::toAbiString(message),
        buttons == 0 ? PJ_MSG_BTN_OK : buttons);
    return static_cast<MessageBoxButton>(result);
  }

  void showInfo(std::string_view title, std::string_view message) const {
    (void)showMessageBox(MessageBoxType::kInfo, title, message, static_cast<int>(MessageBoxButton::kOk));
  }
  void showWarning(std::string_view title, std::string_view message) const {
    (void)showMessageBox(MessageBoxType::kWarning, title, message, static_cast<int>(MessageBoxButton::kOk));
  }
  void showError(std::string_view title, std::string_view message) const {
    (void)showMessageBox(MessageBoxType::kError, title, message, static_cast<int>(MessageBoxButton::kOk));
  }
  [[nodiscard]] bool askContinue(std::string_view title, std::string_view message) const {
    auto result = showMessageBox(
        MessageBoxType::kQuestion, title, message, MessageBoxButton::kContinue | MessageBoxButton::kAbort);
    return result == MessageBoxButton::kContinue;
  }
  [[nodiscard]] bool askYesNo(std::string_view title, std::string_view message) const {
    auto result =
        showMessageBox(MessageBoxType::kQuestion, title, message, MessageBoxButton::kYes | MessageBoxButton::kNo);
    return result == MessageBoxButton::kYes;
  }

  /**
   * List all available parser encodings.
   * @return JSON array string of encoding names, or empty if no parsers loaded.
   */
  [[nodiscard]] std::string_view listAvailableEncodings() const {
    if (!valid() || host_.vtable->list_available_encodings == nullptr) {
      return {};
    }
    const char* result = host_.vtable->list_available_encodings(host_.ctx);
    return result == nullptr ? std::string_view{} : std::string_view(result);
  }

  /// Access the underlying C ABI struct (SDK internals).
  [[nodiscard]] const PJ_data_source_runtime_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_data_source_runtime_host_t host_{};
};

}  // namespace PJ
