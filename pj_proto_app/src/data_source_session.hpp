#pragma once

#include <QObject>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pj_base/data_source_protocol.h"
#include "pj_base/plugin_data_api.h"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/plugin_data_host.hpp"
#include "pj_plugins/host/data_source_handle.hpp"
#include "pj_plugins/host/data_source_library.hpp"
#include "pj_plugins/host/message_parser_handle.hpp"

namespace proto {

class PluginRegistry;

/// State for one parser binding: parser instance + its write host.
struct ParserBinding {
  std::unique_ptr<PJ::MessageParserHandle> parser;
  std::unique_ptr<PJ::DatastoreParserWriteHost> write_host;
};

struct DedupMessage {
  PJ_data_source_message_level_t level = PJ_DATA_SOURCE_MESSAGE_INFO;
  int count = 0;
};

/// Signature for the message box callback bound by the Qt layer.
/// Returns the button that was clicked (a PJ_message_box_buttons_t value).
using ShowMessageBoxCallback =
    std::function<int(PJ_message_box_type_t type, std::string_view title, std::string_view message, int buttons)>;

struct RuntimeHostState {
  std::mutex callback_mutex;  // protects messages and state_transitions from plugin threads
  std::vector<PJ_data_source_state_t> state_transitions;
  int progress_starts = 0;
  int progress_updates = 0;
  int progress_finishes = 0;
  std::atomic<bool> stop_requested{false};
  std::unordered_map<std::string, DedupMessage> messages;
  std::string last_error;

  // Delegated ingest bridge state
  PJ::DataEngine* engine = nullptr;
  PJ::DatasetId dataset_id = 0;
  PluginRegistry* registry = nullptr;
  uint32_t next_binding_id = 1;
  std::unordered_map<uint32_t, ParserBinding> parser_bindings;

  // Qt-layer callback for showing message boxes (bound at runtime by host app)
  ShowMessageBoxCallback show_message_box_callback;

  // Cached JSON array for list_available_encodings (lifetime: until next call)
  std::string available_encodings_cache;
};

class DataSourceSession : public QObject {
  Q_OBJECT

 public:
  DataSourceSession(
      PJ::DataEngine& engine, PJ::DataSourceLibrary& library, PJ::TimeDomainId td_id, std::string source_name,
      PluginRegistry* registry, QObject* parent = nullptr);

  /// Bind a minimal runtime host so the dialog can call listAvailableEncodings().
  /// Must be called before showing the dialog. setupAndStart() will complete the binding.
  void bindRuntimeHostForDialog();

  bool startFileImport(const std::string& config_json);
  bool startStream(const std::string& config_json);
  void stopStream();
  void poll();
  void requestStop();

  bool pauseStream();
  bool resumeStream();

  [[nodiscard]] PJ::DataSourceHandle& handle() {
    return handle_;
  }
  [[nodiscard]] PJ::DataSourceLibrary& library() {
    return library_;
  }
  [[nodiscard]] PJ_data_source_state_t currentState() const {
    return handle_.currentState();
  }
  [[nodiscard]] bool supportsPause() const {
    return (handle_.capabilities() & PJ_DATA_SOURCE_CAPABILITY_SUPPORTS_PAUSE) != 0;
  }
  [[nodiscard]] bool isStream() const {
    return is_stream_;
  }
  [[nodiscard]] PJ::DatasetId datasetId() const {
    return runtime_state_.dataset_id;
  }
  [[nodiscard]] const std::string& sourceName() const {
    return source_name_;
  }
  [[nodiscard]] const std::string& lastConfigJson() const {
    return last_config_json_;
  }
  [[nodiscard]] const std::string& lastError() const {
    return last_error_;
  }

  /// Bind the message box callback from the Qt layer. Must be called before start.
  void setMessageBoxCallback(ShowMessageBoxCallback callback) {
    runtime_state_.show_message_box_callback = std::move(callback);
  }

 signals:
  void importComplete();
  void streamDataReady();

 private:
  static PJ_data_source_runtime_host_t makeRuntimeHost(RuntimeHostState* state);

  bool setupAndStart(const std::string& config_json);

  PJ::DataEngine& engine_;
  PJ::DataSourceLibrary& library_;
  PJ::TimeDomainId td_id_;
  std::string source_name_;
  PluginRegistry* registry_;
  PJ::DataSourceHandle handle_;
  std::unique_ptr<PJ::DatastoreSourceWriteHost> write_host_;
  RuntimeHostState runtime_state_;
  std::string last_config_json_;
  std::string last_error_;
  bool is_stream_ = false;
};

}  // namespace proto
