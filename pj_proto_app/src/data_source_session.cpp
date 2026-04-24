#include "data_source_session.hpp"

#include <iostream>

#include "pj_base/sdk/service_traits.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"
#include "plugin_registry.hpp"

namespace proto {

// --- Runtime host callbacks (static, C-compatible) ---

namespace {

void rhReportMessage(void* ctx, PJ_data_source_message_level_t level, PJ_string_view_t msg) noexcept {
  auto* s = static_cast<RuntimeHostState*>(ctx);
  std::string m(msg.data, msg.size);
  std::lock_guard<std::mutex> lock(s->callback_mutex);
  auto& entry = s->messages[m];
  entry.level = level;
  entry.count++;
  // Only print to stderr on first occurrence
  if (entry.count == 1) {
    std::cerr << "[plugin] " << m << "\n";
  }
}

bool rhProgressStart(void* ctx, PJ_string_view_t label, uint64_t, bool, PJ_error_t* /*out_error*/) noexcept {
  static_cast<RuntimeHostState*>(ctx)->progress_starts++;
  try {
    std::cerr << "[progress] start: " << std::string(label.data, label.size) << "\n";
  } catch (...) {}
  return true;
}

bool rhProgressUpdate(void* ctx, uint64_t) noexcept {
  static_cast<RuntimeHostState*>(ctx)->progress_updates++;
  return !static_cast<RuntimeHostState*>(ctx)->stop_requested.load();
}

void rhProgressFinish(void* ctx) noexcept {
  static_cast<RuntimeHostState*>(ctx)->progress_finishes++;
}

bool rhIsStopRequested(void* ctx) noexcept {
  return static_cast<RuntimeHostState*>(ctx)->stop_requested.load();
}

void rhNotifyState(void* ctx, PJ_data_source_state_t state) noexcept {
  auto* s = static_cast<RuntimeHostState*>(ctx);
  try {
    std::lock_guard<std::mutex> lock(s->callback_mutex);
    s->state_transitions.push_back(state);
  } catch (...) {}
}

void rhRequestStop(void*, PJ_data_source_state_t, PJ_string_view_t reason) noexcept {
  try {
    std::cerr << "[plugin] requestStop: " << std::string(reason.data, reason.size) << "\n";
  } catch (...) {}
}

bool rhEnsureParserBinding(
    void* ctx, const PJ_parser_binding_request_t* request, PJ_parser_binding_handle_t* out,
    PJ_error_t* /*out_error*/) noexcept {
  try {
    auto* state = static_cast<RuntimeHostState*>(ctx);
    if (state->registry == nullptr || state->engine == nullptr) {
      return false;
    }

    std::string_view encoding(request->parser_encoding.data, request->parser_encoding.size);
    std::string_view topic_name(request->topic_name.data, request->topic_name.size);
    std::string_view type_name(request->type_name.data, request->type_name.size);

    auto* parser_entry = state->registry->findParserByEncoding(encoding);
    if (parser_entry == nullptr) {
      state->last_error = "no parser found for encoding '" + std::string(encoding) + "'";
      std::cerr << "[bridge] " << state->last_error << "\n";
      return false;
    }

    // Create parser instance
    auto parser = std::make_unique<PJ::MessageParserHandle>(parser_entry->library.createHandle());
    if (!parser->valid()) {
      state->last_error = "failed to create parser instance for '" + std::string(encoding) + "'";
      std::cerr << "[bridge] " << state->last_error << "\n";
      return false;
    }

    // Create a topic in the datastore for this channel
    auto topic_result =
        state->engine->createTopic(state->dataset_id, PJ::TopicDescriptor{.name = std::string(topic_name)});
    if (!topic_result) {
      state->last_error = "failed to create topic '" + std::string(topic_name) + "': " + topic_result.error();
      std::cerr << "[bridge] " << state->last_error << "\n";
      return false;
    }

    PJ_topic_handle_t topic_handle{static_cast<uint32_t>(*topic_result)};

    // Create parser write host scoped to this topic
    auto write_host = std::make_unique<PJ::DatastoreParserWriteHost>(*state->engine, topic_handle);

    // Bind parser via service registry. The builder must outlive this scope
    // because the plugin may hold a view into it; we move it into ParserBinding.
    auto registry_builder = std::make_unique<PJ::ServiceRegistryBuilder>();
    registry_builder->registerService<PJ::sdk::ParserWriteHostService>(write_host->raw());
    if (auto s = parser->bind(registry_builder->view()); !s) {
      state->last_error = "failed to bind parser services: " + s.error();
      std::cerr << "[bridge] " << state->last_error << "\n";
      return false;
    }

    // Bind schema if provided by request
    if (request->schema.size > 0) {
      PJ::Span<const uint8_t> schema_span(request->schema.data, request->schema.size);
      if (auto s = parser->bindSchema(type_name, schema_span); !s) {
        state->last_error = "failed to bind schema for " + std::string(type_name) + ": " + s.error();
        std::cerr << "[bridge] " << state->last_error << "\n";
        return false;
      }
    }

    // Load parser config: prefer request config, fall back to dialog config
    std::string_view parser_config;
    if (request->parser_config_json.size > 0) {
      parser_config = std::string_view(request->parser_config_json.data, request->parser_config_json.size);
    } else if (!state->parser_config_json.empty()) {
      parser_config = state->parser_config_json;
    }

    if (!parser_config.empty()) {
      if (auto s = parser->loadConfig(parser_config); !s) {
        state->last_error = "failed to load parser config: " + s.error();
        std::cerr << "[bridge] " << state->last_error << "\n";
        return false;
      }
    }

    uint32_t binding_id = state->next_binding_id++;
    state->parser_bindings.emplace(
        binding_id, ParserBinding{std::move(registry_builder), std::move(write_host), std::move(parser)});

    *out = PJ_parser_binding_handle_t{binding_id};
    std::cerr << "[bridge] bound parser '" << parser_entry->name << "' for topic '" << topic_name << "'\n";
    return true;
  } catch (...) {
    return false;
  }
}

bool rhPushRawMessage(
    void* ctx, PJ_parser_binding_handle_t handle, int64_t timestamp_ns, PJ_bytes_view_t payload,
    PJ_error_t* /*out_error*/) noexcept {
  try {
    auto* state = static_cast<RuntimeHostState*>(ctx);
    auto it = state->parser_bindings.find(handle.id);
    if (it == state->parser_bindings.end()) {
      state->last_error = "invalid parser binding handle";
      return false;
    }
    if (auto s = it->second.parser->parse(timestamp_ns, PJ::Span<const uint8_t>(payload.data, payload.size)); !s) {
      state->last_error = s.error();
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

int rhShowMessageBox(
    void* ctx, PJ_message_box_type_t type, PJ_string_view_t title, PJ_string_view_t message, int buttons) noexcept {
  auto* state = static_cast<RuntimeHostState*>(ctx);
  if (!state->show_message_box_callback) {
    // No callback bound - return positive default (headless mode)
    if (buttons & PJ_MSG_BTN_CONTINUE) {
      return PJ_MSG_BTN_CONTINUE;
    }
    if (buttons & PJ_MSG_BTN_YES) {
      return PJ_MSG_BTN_YES;
    }
    return PJ_MSG_BTN_OK;
  }
  return state->show_message_box_callback(
      type, std::string_view(title.data, title.size), std::string_view(message.data, message.size), buttons);
}

const char* rhListAvailableEncodings(void* ctx) noexcept {
  try {
    auto* state = static_cast<RuntimeHostState*>(ctx);
    if (state->registry == nullptr) {
      return nullptr;
    }
    state->available_encodings_cache = state->registry->listAvailableEncodings();
    return state->available_encodings_cache.c_str();
  } catch (...) {
    return nullptr;
  }
}

}  // namespace

PJ_data_source_runtime_host_t DataSourceSession::makeRuntimeHost(RuntimeHostState* state) {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_runtime_host_vtable_t),
      .report_message = rhReportMessage,
      .progress_start = rhProgressStart,
      .progress_update = rhProgressUpdate,
      .progress_finish = rhProgressFinish,
      .is_stop_requested = rhIsStopRequested,
      .notify_state = rhNotifyState,
      .request_stop = rhRequestStop,
      .ensure_parser_binding = rhEnsureParserBinding,
      .push_raw_message = rhPushRawMessage,
      .show_message_box = rhShowMessageBox,
      .list_available_encodings = rhListAvailableEncodings,
  };
  return PJ_data_source_runtime_host_t{.ctx = state, .vtable = &vtable};
}

DataSourceSession::DataSourceSession(
    PJ::DataEngine& engine, PJ::DataSourceLibrary& library, PJ::TimeDomainId td_id, std::string source_name,
    PluginRegistry* registry, QObject* parent)
    : QObject(parent),
      engine_(engine),
      library_(library),
      td_id_(td_id),
      source_name_(std::move(source_name)),
      registry_(registry),
      handle_(library.createHandle()) {}

bool DataSourceSession::bindForDialog() {
  // v3 contract: bind() must be one-shot. To satisfy the dialog's need for a
  // bound runtime host (for stream plugins that call listAvailableEncodings
  // inside their dialog's pre-populate path) AND avoid a second bind call
  // later, we create the dataset + write_host up-front so the FULL registry
  // is ready before the dialog is shown.
  //
  // Side-effect: if the user cancels the dialog, the dataset remains as an
  // empty placeholder in the engine. Acceptable for now (datasets are cheap
  // metadata); a future cleanup pass can add a delete-on-cancel path.
  if (bound_) {
    return true;  // idempotent — avoid a second bind if called twice
  }

  auto ds_result = engine_.createDataset(PJ::DatasetDescriptor{.source_name = source_name_, .time_domain_id = td_id_});
  if (!ds_result) {
    runtime_state_.last_error = "failed to create dataset: " + ds_result.error();
    std::cerr << "[session] " << runtime_state_.last_error << "\n";
    return false;
  }

  PJ_data_source_handle_t source_handle{static_cast<uint32_t>(*ds_result)};
  write_host_ = std::make_unique<PJ::DatastoreSourceWriteHost>(engine_, source_handle);

  runtime_state_.engine = &engine_;
  runtime_state_.dataset_id = *ds_result;
  runtime_state_.registry = registry_;

  bind_registry_.emplace();
  bind_registry_->registerService<PJ::sdk::SourceWriteHostService>(write_host_->raw());
  bind_registry_->registerService<PJ::sdk::DataSourceRuntimeHostService>(makeRuntimeHost(&runtime_state_));

  if (auto s = handle_.bind(bind_registry_->view()); !s) {
    runtime_state_.last_error = "bind failed: " + s.error();
    std::cerr << "[session] " << runtime_state_.last_error << "\n";
    return false;
  }

  bound_ = true;
  return true;
}

bool DataSourceSession::applyConfigAndStart(const std::string& config_json) {
  if (!bound_) {
    runtime_state_.last_error = "session not bound; call bindForDialog() first";
    return false;
  }
  if (!config_json.empty()) {
    if (auto s = handle_.loadConfig(config_json); !s) {
      runtime_state_.last_error = "loadConfig failed: " + s.error();
      return false;
    }
  }
  auto status = handle_.start();
  if (!status) {
    runtime_state_.last_error = status.error();
  }
  return static_cast<bool>(status);
}

bool DataSourceSession::startFileImport(const std::string& config_json) {
  if (!applyConfigAndStart(config_json)) {
    std::cerr << "[import] start failed for '" << source_name_ << "': " << runtime_state_.last_error << "\n";
    write_host_->flushPending();
    for (auto& [id, binding] : runtime_state_.parser_bindings) {
      binding.write_host->flushPending();
    }
    emit importComplete();
    return false;
  }
  write_host_->flushPending();
  for (auto& [id, binding] : runtime_state_.parser_bindings) {
    binding.write_host->flushPending();
  }
  emit importComplete();
  return true;
}

bool DataSourceSession::startStream(const std::string& config_json) {
  is_stream_ = true;
  last_config_json_ = config_json;
  if (!applyConfigAndStart(config_json)) {
    std::cerr << "[stream] start failed for '" << source_name_ << "': " << runtime_state_.last_error << "\n";
    return false;
  }
  return true;
}

void DataSourceSession::stopStream() {
  runtime_state_.stop_requested.store(true);
  handle_.stop();
}

void DataSourceSession::poll() {
  (void)handle_.poll();
  if (write_host_) {
    write_host_->flushPending();
  }
  // Flush all parser write hosts (delegated ingest creates per-topic writers)
  for (auto& [id, binding] : runtime_state_.parser_bindings) {
    binding.write_host->flushPending();
  }
}

void DataSourceSession::requestStop() {
  runtime_state_.stop_requested.store(true);
}

bool DataSourceSession::pauseStream() {
  auto s = handle_.pause();
  if (!s) {
    runtime_state_.last_error = s.error();
  }
  return static_cast<bool>(s);
}

bool DataSourceSession::resumeStream() {
  auto s = handle_.resume();
  if (!s) {
    runtime_state_.last_error = s.error();
  }
  return static_cast<bool>(s);
}

}  // namespace proto
