#pragma once

#include <QObject>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
#include "pj_plugins/host/service_registry_builder.hpp"

namespace proto {

class PluginRegistry;

/// State for one parser binding: parser instance + its write host + the
/// service registry builder the parser was bound through (kept alive so the
/// fat-pointer services remain valid for the binding's lifetime).
/// Destruction order (reverse of declaration) matters: the parser's
/// destructor may flush pending writes through write_host, so parser must
/// die BEFORE write_host. The registry_builder only supplies fat pointers
/// at bind-time; after bind, the plugin holds its own copies, so the
/// builder can die first.
struct ParserBinding {
  std::unique_ptr<PJ::ServiceRegistryBuilder> registry_builder;
  std::unique_ptr<PJ::DatastoreParserWriteHost> write_host;
  std::unique_ptr<PJ::MessageParserHandle> parser;
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

  // Parser dialog config (used for binding schemas)
  std::string parser_config_json;
};

class DataSourceSession : public QObject {
  Q_OBJECT

 public:
  DataSourceSession(
      PJ::DataEngine& engine, PJ::DataSourceLibrary& library, PJ::TimeDomainId td_id, std::string source_name,
      PluginRegistry* registry, QObject* parent = nullptr);

  /// Bind the plugin with the full service registry (source_write + runtime).
  /// Creates the dataset + write_host up-front so the registry is complete
  /// before the dialog is shown — the v3 protocol requires `bind()` to be
  /// called exactly once per plugin instance. Idempotent: a second call is
  /// a no-op.
  ///
  /// Must be called before showing the dialog. startFileImport/startStream
  /// assume the session is already bound.
  [[nodiscard]] bool bindForDialog();

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
    return static_cast<PJ_data_source_state_t>(handle_.currentState());
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
    return runtime_state_.last_error;
  }

  /// Bind the message box callback from the Qt layer. Must be called before start.
  void setMessageBoxCallback(ShowMessageBoxCallback callback) {
    runtime_state_.show_message_box_callback = std::move(callback);
  }

  /// Set the parser dialog config (for schema binding in delegated ingest).
  void setParserConfig(std::string config) {
    runtime_state_.parser_config_json = std::move(config);
  }

 signals:
  void importComplete();
  void streamDataReady();

 private:
  static PJ_data_source_runtime_host_t makeRuntimeHost(RuntimeHostState* state);

  bool applyConfigAndStart(const std::string& config_json);

  PJ::DataEngine& engine_;
  PJ::DataSourceLibrary& library_;
  PJ::TimeDomainId td_id_;
  std::string source_name_;
  PluginRegistry* registry_;
  PJ::DataSourceHandle handle_;
  std::unique_ptr<PJ::DatastoreSourceWriteHost> write_host_;
  RuntimeHostState runtime_state_;
  /// Single service-registry slot for the plugin's lifetime. Populated in
  /// bindRuntimeHostForDialog() with the runtime-only registry, then
  /// replaced in setupAndStart() with the full (source_write + runtime)
  /// registry. `optional` is used because ServiceRegistryBuilder is
  /// non-movable (see its declaration) — `emplace` reconstructs in place.
  std::optional<PJ::ServiceRegistryBuilder> bind_registry_;
  std::string last_config_json_;
  bool is_stream_ = false;
  bool bound_ = false;
};

}  // namespace proto
