/**
 * @file data_source_plugin_base.hpp
 * @brief C++ SDK for implementing DataSource plugins.
 *
 * Plugin authors subclass DataSourcePluginBase, override the required virtuals,
 * and export with the PJ_DATA_SOURCE_PLUGIN(ClassName, manifest) macro. The SDK handles
 * C ABI trampoline generation and exception safety.
 *
 * See pj_plugins/examples/mock_data_source.cpp for a complete example.
 */
#pragma once

#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

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

/// Allow combining MessageBoxButton values with |.
inline int operator|(MessageBoxButton a, MessageBoxButton b) {
  return static_cast<int>(a) | static_cast<int>(b);
}
inline int operator|(int a, MessageBoxButton b) {
  return a | static_cast<int>(b);
}

/// @name Capability flag constants
/// @{
constexpr uint64_t kCapabilityFiniteImport = PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT;
constexpr uint64_t kCapabilityContinuousStream = PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM;
constexpr uint64_t kCapabilityDirectIngest = PJ_DATA_SOURCE_CAPABILITY_DIRECT_INGEST;
constexpr uint64_t kCapabilityDelegatedIngest = PJ_DATA_SOURCE_CAPABILITY_DELEGATED_INGEST;
constexpr uint64_t kCapabilitySupportsPause = PJ_DATA_SOURCE_CAPABILITY_SUPPORTS_PAUSE;
constexpr uint64_t kCapabilityHasDialog = PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG;
/// @}

using ParserBindingHandle = PJ_parser_binding_handle_t;

/// C++ mirror of PJ_parser_binding_request_t for delegated-ingest parser lookup.
struct ParserBindingRequest {
  std::string_view topic_name;
  std::string_view parser_encoding;
  std::string_view type_name;
  Span<const uint8_t> schema;
  std::string_view parser_config_json;
};

/**
 * Type-safe C++ view over the runtime host vtable.
 *
 * Plugins access this via DataSourcePluginBase::runtimeHost(). Each method
 * is a null-safe wrapper: calls on an unbound host are no-ops or return
 * safe defaults. This is the control-plane counterpart to
 * sdk::SourceWriteHostView (data plane).
 */
class DataSourceRuntimeHostView {
 public:
  explicit DataSourceRuntimeHostView(PJ_data_source_runtime_host_t host = {}) : host_(host) {}

  /// Returns true if both context and vtable pointers are set.
  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  /// Returns the last host-side error, or empty if none.
  [[nodiscard]] std::string_view lastError() const {
    if (!valid() || host_.vtable->get_last_error == nullptr) {
      return {};
    }
    const char* err = host_.vtable->get_last_error(host_.ctx);
    return err == nullptr ? std::string_view{} : std::string_view(err);
  }

  /// Send a diagnostic message to the host UI log.
  void reportMessage(DataSourceMessageLevel level, std::string_view message) const {
    if (valid() && host_.vtable->report_message != nullptr) {
      host_.vtable->report_message(
          host_.ctx, static_cast<PJ_data_source_message_level_t>(level), sdk::toAbiString(message));
    }
  }

  /// Begin a progress bar with @p total_steps. Set @p cancellable to allow user abort.
  [[nodiscard]] bool progressStart(std::string_view label, uint64_t total_steps, bool cancellable) const {
    if (!valid() || host_.vtable->progress_start == nullptr) {
      return false;
    }
    return host_.vtable->progress_start(host_.ctx, sdk::toAbiString(label), total_steps, cancellable);
  }

  /// Advance progress. Returns false if the user cancelled.
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

  /// Check whether the host has requested the plugin to stop.
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

  /// Bind (or look up) a parser for delegated ingest. Returns the handle on success.
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
    if (!host_.vtable->ensure_parser_binding(host_.ctx, &raw, &handle)) {
      return unexpected(std::string(lastError()));
    }
    return handle;
  }

  /// Push a raw message for host-side parsing via a previously obtained binding handle.
  [[nodiscard]] Status pushRawMessage(
      ParserBindingHandle handle, Timestamp host_timestamp_ns, Span<const uint8_t> payload) const {
    if (!valid() || host_.vtable->push_raw_message == nullptr) {
      return unexpected("runtime host is not bound");
    }
    if (!host_.vtable->push_raw_message(host_.ctx, handle, host_timestamp_ns, sdk::toAbiBytes(payload))) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  // ─────────────────────────────────────────────────────────────────────────────
  // Modal message box API
  // ─────────────────────────────────────────────────────────────────────────────

  /**
   * Display a modal message box and wait for user response.
   * @param type    Dialog type (determines icon).
   * @param title   Window title.
   * @param message Message text (may contain newlines).
   * @param buttons Bitmask of MessageBoxButton values.
   * @return The button clicked, or MessageBoxButton::kOk if host doesn't support dialogs.
   */
  [[nodiscard]] MessageBoxButton showMessageBox(
      MessageBoxType type, std::string_view title, std::string_view message, int buttons = 0) const {
    if (!valid() || host_.vtable->show_message_box == nullptr) {
      // Host doesn't support message boxes — return positive default
      if (buttons & static_cast<int>(MessageBoxButton::kContinue)) return MessageBoxButton::kContinue;
      if (buttons & static_cast<int>(MessageBoxButton::kYes)) return MessageBoxButton::kYes;
      return MessageBoxButton::kOk;
    }
    int result = host_.vtable->show_message_box(
        host_.ctx, static_cast<PJ_message_box_type_t>(type), sdk::toAbiString(title), sdk::toAbiString(message),
        buttons == 0 ? PJ_MSG_BTN_OK : buttons);
    return static_cast<MessageBoxButton>(result);
  }

  /// Show an information message box with OK button.
  void showInfo(std::string_view title, std::string_view message) const {
    (void)showMessageBox(MessageBoxType::kInfo, title, message, static_cast<int>(MessageBoxButton::kOk));
  }

  /// Show a warning message box with OK button.
  void showWarning(std::string_view title, std::string_view message) const {
    (void)showMessageBox(MessageBoxType::kWarning, title, message, static_cast<int>(MessageBoxButton::kOk));
  }

  /// Show an error message box with OK button.
  void showError(std::string_view title, std::string_view message) const {
    (void)showMessageBox(MessageBoxType::kError, title, message, static_cast<int>(MessageBoxButton::kOk));
  }

  /// Show a question dialog with Continue/Abort buttons. Returns true if user chose Continue.
  [[nodiscard]] bool askContinue(std::string_view title, std::string_view message) const {
    auto result =
        showMessageBox(MessageBoxType::kQuestion, title, message, MessageBoxButton::kContinue | MessageBoxButton::kAbort);
    return result == MessageBoxButton::kContinue;
  }

  /// Show a question dialog with Yes/No buttons. Returns true if user chose Yes.
  [[nodiscard]] bool askYesNo(std::string_view title, std::string_view message) const {
    auto result =
        showMessageBox(MessageBoxType::kQuestion, title, message, MessageBoxButton::kYes | MessageBoxButton::kNo);
    return result == MessageBoxButton::kYes;
  }

  // ─────────────────────────────────────────────────────────────────────────────
  // Dynamic parser discovery API
  // ─────────────────────────────────────────────────────────────────────────────

  /**
   * List all available parser encodings as a JSON string.
   *
   * Returns a JSON array string of encoding names, e.g. ["json","cbor","protobuf"].
   * Prefer using listAvailableEncodings() which returns a parsed vector.
   *
   * @return JSON array string, or empty string if host doesn't support this or no parsers loaded.
   * @note Check that the host vtable has this method (newer hosts only).
   */
  [[nodiscard]] std::string_view listAvailableEncodingsJson() const {
    if (!valid()) {
      return {};
    }
    // Check struct_size to see if this field exists (forward compatibility)
    constexpr size_t field_offset =
        offsetof(PJ_data_source_runtime_host_vtable_t, list_available_encodings);
    if (host_.vtable->struct_size < field_offset + sizeof(void*)) {
      return {};  // Older host without this method
    }
    if (host_.vtable->list_available_encodings == nullptr) {
      return {};
    }
    const char* result = host_.vtable->list_available_encodings(host_.ctx);
    return result == nullptr ? std::string_view{} : std::string_view(result);
  }

  /**
   * List all available parser encodings.
   *
   * Returns a vector of encoding names, e.g. {"json", "cbor", "protobuf"}.
   * Plugins can use this to dynamically populate encoding selection UI instead
   * of hardcoding a static list.
   *
   * @return Vector of encoding names, or empty vector if host doesn't support this.
   */
  [[nodiscard]] std::vector<std::string> listAvailableEncodings() const {
    auto json = listAvailableEncodingsJson();
    return parseJsonStringArray(json);
  }

 private:
  /// Parse a simple JSON array of strings: ["a","b","c"] -> {"a","b","c"}
  /// Handles escaped quotes within strings. Returns empty vector on malformed input.
  static std::vector<std::string> parseJsonStringArray(std::string_view json) {
    std::vector<std::string> result;
    if (json.empty()) return result;

    size_t i = 0;
    // Skip whitespace and find opening bracket
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n')) ++i;
    if (i >= json.size() || json[i] != '[') return result;
    ++i;

    while (i < json.size()) {
      // Skip whitespace
      while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == ',')) ++i;
      if (i >= json.size() || json[i] == ']') break;

      // Expect opening quote
      if (json[i] != '"') return {};  // Malformed
      ++i;

      // Parse string content (handle escaped quotes)
      std::string str;
      while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
          ++i;  // Skip backslash
          if (json[i] == '"' || json[i] == '\\') {
            str += json[i];
          } else {
            str += '\\';
            str += json[i];
          }
        } else {
          str += json[i];
        }
        ++i;
      }
      if (i >= json.size()) return {};  // Unclosed string
      ++i;  // Skip closing quote

      result.push_back(std::move(str));
    }

    return result;
  }

 public:

  /// Access the underlying C ABI struct.
  [[nodiscard]] const PJ_data_source_runtime_host_t& raw() const {
    return host_;
  }

 private:
  PJ_data_source_runtime_host_t host_{};
};

/**
 * Base class for DataSource plugins.
 *
 * Subclass and override the pure-virtual methods: capabilities(), start(),
 * stop(), and currentState(). Optionally override pause/resume, poll,
 * saveConfig/loadConfig for richer behaviour.
 *
 * Use writeHost() and runtimeHost() (protected) to interact with the host
 * during start() and poll(). Export with PJ_DATA_SOURCE_PLUGIN(YourClass, manifest).
 *
 * The base class generates C ABI trampolines with full exception safety —
 * any exception thrown from a virtual is caught, stored via setLastError(),
 * and converted to a false/null return across the ABI boundary.
 */
class DataSourcePluginBase {
 public:
  virtual ~DataSourcePluginBase() = default;

  /// Return a bitmask of kCapability* flags describing this source's features.
  virtual uint64_t capabilities() const = 0;

  /// Bind the data-plane write host. Override only if you need custom validation.
  virtual Status bindWriteHost(PJ_source_write_host_t write_host) {
    if (write_host.ctx == nullptr || write_host.vtable == nullptr) {
      return unexpected("write host is not bound");
    }
    write_host_ = write_host;
    return okStatus();
  }

  /// Bind the control-plane runtime host. Override only if you need custom validation.
  virtual Status bindRuntimeHost(PJ_data_source_runtime_host_t runtime_host) {
    if (runtime_host.ctx == nullptr || runtime_host.vtable == nullptr) {
      return unexpected("runtime host is not bound");
    }
    runtime_host_ = runtime_host;
    return okStatus();
  }

  /// Serialize plugin configuration to JSON.
  /// If this source has a dialog, delegate to the dialog's saveConfig().
  /// The host persists this and may pass it back via loadConfig() to restore state.
  virtual std::string saveConfig() const {
    return "{}";
  }

  /// Restore plugin configuration from JSON.
  /// Called before start(), possibly before showing the dialog.
  /// If this source has a dialog, delegate to the dialog's loadConfig().
  /// File importers receive {"filepath": "/path/to/file"} from the host.
  virtual Status loadConfig(std::string_view config_json) {
    (void)config_json;
    return okStatus();
  }

  /// Begin data acquisition. Hosts are already bound when this is called.
  virtual Status start() = 0;

  /// Stop data acquisition. Must be idempotent.
  virtual void stop() = 0;

  /// Pause a running source. Default returns error (unsupported).
  virtual Status pause() {
    return unexpected("pause is not supported");
  }

  /// Resume a paused source. Default returns error (unsupported).
  virtual Status resume() {
    return unexpected("resume is not supported");
  }

  /// Called periodically while running. Override for streaming sources. Default is no-op.
  virtual Status poll() {
    return okStatus();
  }

  /// Return the current lifecycle state.
  virtual DataSourceState currentState() const = 0;

  /// Return the last error message. Override for custom error reporting.
  virtual std::string lastError() const {
    return last_error_;
  }

  /// Override to return your dialog member's context.
  /// Default returns nullptr (no dialog).
  virtual void* dialogContext() {
    return nullptr;
  }

  template <typename CreateFn>
  static const PJ_data_source_vtable_t* vtableWithCreate(CreateFn create_fn, const char* manifest) {
    PJ_ASSERT(manifest != nullptr && manifest[0] == '{', "manifest must be a JSON object");
    PJ_ASSERT(std::strstr(manifest, "\"name\"") != nullptr, "manifest must contain a \"name\" key");
    PJ_ASSERT(std::strstr(manifest, "\"version\"") != nullptr, "manifest must contain a \"version\" key");
    static const PJ_data_source_vtable_t vt = {
        PJ_DATA_SOURCE_PROTOCOL_VERSION,
        sizeof(PJ_data_source_vtable_t),
        create_fn,
        trampoline_destroy,
        manifest,
        trampoline_capabilities,
        trampoline_bind_write_host,
        trampoline_bind_runtime_host,
        trampoline_save_config,
        trampoline_load_config,
        trampoline_start,
        trampoline_stop,
        trampoline_pause,
        trampoline_resume,
        trampoline_poll,
        trampoline_current_state,
        trampoline_get_last_error,
        trampoline_get_dialog_context,
    };
    return &vt;
  }

 protected:
  [[nodiscard]] bool writeHostBound() const {
    return write_host_.ctx != nullptr && write_host_.vtable != nullptr;
  }

  [[nodiscard]] bool runtimeHostBound() const {
    return runtime_host_.ctx != nullptr && runtime_host_.vtable != nullptr;
  }

  [[nodiscard]] sdk::SourceWriteHostView writeHost() const {
    return sdk::SourceWriteHostView(write_host_);
  }

  [[nodiscard]] DataSourceRuntimeHostView runtimeHost() const {
    return DataSourceRuntimeHostView(runtime_host_);
  }

  void setLastError(std::string error) {
    last_error_ = std::move(error);
  }

 private:
  PJ_source_write_host_t write_host_{};
  PJ_data_source_runtime_host_t runtime_host_{};
  std::string config_buf_;
  mutable std::string last_error_;

  // C ABI trampolines — exception-safe bridges between host vtable calls and
  // C++ virtuals. Implementations live in detail/data_source_trampolines.hpp.
  static void trampoline_destroy(void* ctx);
  static uint64_t trampoline_capabilities(void* ctx);
  static bool trampoline_bind_write_host(void* ctx, PJ_source_write_host_t write_host);
  static bool trampoline_bind_runtime_host(void* ctx, PJ_data_source_runtime_host_t runtime_host);
  static const char* trampoline_save_config(void* ctx);
  static bool trampoline_load_config(void* ctx, const char* config_json);
  static bool trampoline_start(void* ctx);
  static void trampoline_stop(void* ctx);
  static bool trampoline_pause(void* ctx);
  static bool trampoline_resume(void* ctx);
  static bool trampoline_poll(void* ctx);
  static PJ_data_source_state_t trampoline_current_state(void* ctx);
  static void* trampoline_get_dialog_context(void* ctx);
  static const char* trampoline_get_last_error(void* ctx);
};

}  // namespace PJ

// Out-of-line trampoline definitions — separated to keep the public API header concise.
#include "pj_base/sdk/detail/data_source_trampolines.hpp"

/**
 * Export a DataSourcePluginBase subclass as a shared-library plugin.
 *
 * Place at file scope (after the class definition). Generates the extern "C"
 * entry point `PJ_get_data_source_vtable` that the host resolves via dlsym.
 *
 * @param ClassName   The DataSourcePluginBase subclass to instantiate.
 * @param manifest    A string literal containing the JSON manifest
 *                    (must have at least "name" and "version" keys).
 *
 * Usage:
 * @code
 *   PJ_DATA_SOURCE_PLUGIN(MyDataSource, R"({"name":"My Source","version":"1.0.0"})")
 * @endcode
 */
#define PJ_DATA_SOURCE_PLUGIN(ClassName, manifest)                                                       \
  extern "C" PJ_DATA_SOURCE_EXPORT const PJ_data_source_vtable_t* PJ_get_data_source_vtable() {          \
    static const PJ_data_source_vtable_t* vt =                                                           \
        PJ::DataSourcePluginBase::vtableWithCreate([]() -> void* { return new ClassName(); }, manifest); \
    return vt;                                                                                           \
  }
