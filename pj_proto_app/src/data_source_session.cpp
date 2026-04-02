#include "data_source_session.hpp"

#include <iostream>

#include "plugin_registry.hpp"

namespace proto {

// --- Runtime host callbacks (static, C-compatible) ---

namespace {

const char* rhGetLastError(void* ctx) {
  auto* s = static_cast<RuntimeHostState*>(ctx);
  return s->last_error.empty() ? nullptr : s->last_error.c_str();
}

void rhReportMessage(void* ctx, PJ_data_source_message_level_t level, PJ_string_view_t msg) {
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

bool rhProgressStart(void* ctx, PJ_string_view_t label, uint64_t, bool) {
  static_cast<RuntimeHostState*>(ctx)->progress_starts++;
  std::cerr << "[progress] start: " << std::string(label.data, label.size) << "\n";
  return true;
}

bool rhProgressUpdate(void* ctx, uint64_t) {
  static_cast<RuntimeHostState*>(ctx)->progress_updates++;
  return !static_cast<RuntimeHostState*>(ctx)->stop_requested.load();
}

void rhProgressFinish(void* ctx) {
  static_cast<RuntimeHostState*>(ctx)->progress_finishes++;
}

bool rhIsStopRequested(void* ctx) {
  return static_cast<RuntimeHostState*>(ctx)->stop_requested.load();
}

void rhNotifyState(void* ctx, PJ_data_source_state_t state) {
  auto* s = static_cast<RuntimeHostState*>(ctx);
  std::lock_guard<std::mutex> lock(s->callback_mutex);
  s->state_transitions.push_back(state);
}

void rhRequestStop(void*, PJ_data_source_state_t, PJ_string_view_t reason) {
  std::cerr << "[plugin] requestStop: " << std::string(reason.data, reason.size) << "\n";
}

bool rhEnsureParserBinding(void* ctx, const PJ_parser_binding_request_t* request, PJ_parser_binding_handle_t* out) {
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

  // Bind write host to parser
  if (!parser->bindWriteHost(write_host->raw())) {
    state->last_error = "failed to bind write host to parser";
    std::cerr << "[bridge] " << state->last_error << "\n";
    return false;
  }

  // Bind schema if provided
  if (request->schema.size > 0) {
    PJ::Span<const uint8_t> schema_span(request->schema.data, request->schema.size);
    if (!parser->bindSchema(type_name, schema_span)) {
      state->last_error = "failed to parse " + std::string(type_name) + ": " + parser->lastError();
      std::cerr << "[bridge] parser schema binding failed for type '" << type_name << "': " << parser->lastError()
                << "\n";
      return false;
    }
  }

  // Load parser config if provided
  if (request->parser_config_json.size > 0) {
    std::string_view config(request->parser_config_json.data, request->parser_config_json.size);
    (void)parser->loadConfig(config);
  }

  uint32_t binding_id = state->next_binding_id++;
  state->parser_bindings.emplace(binding_id, ParserBinding{std::move(parser), std::move(write_host)});

  *out = PJ_parser_binding_handle_t{binding_id};
  std::cerr << "[bridge] bound parser '" << parser_entry->name << "' for topic '" << topic_name << "'\n";
  return true;
}

bool rhPushRawMessage(void* ctx, PJ_parser_binding_handle_t handle, int64_t timestamp_ns, PJ_bytes_view_t payload) {
  auto* state = static_cast<RuntimeHostState*>(ctx);
  auto it = state->parser_bindings.find(handle.id);
  if (it == state->parser_bindings.end()) {
    return false;
  }
  return it->second.parser->parse(timestamp_ns, PJ::Span<const uint8_t>(payload.data, payload.size));
}

int rhShowMessageBox(
    void* ctx, PJ_message_box_type_t type, PJ_string_view_t title, PJ_string_view_t message, int buttons) {
  auto* state = static_cast<RuntimeHostState*>(ctx);
  if (!state->show_message_box_callback) {
    // No callback bound - return positive default (headless mode)
    if (buttons & PJ_MSG_BTN_CONTINUE) return PJ_MSG_BTN_CONTINUE;
    if (buttons & PJ_MSG_BTN_YES) return PJ_MSG_BTN_YES;
    return PJ_MSG_BTN_OK;
  }
  return state->show_message_box_callback(
      type, std::string_view(title.data, title.size), std::string_view(message.data, message.size), buttons);
}

}  // namespace

PJ_data_source_runtime_host_t DataSourceSession::makeRuntimeHost(RuntimeHostState* state) {
  static const PJ_data_source_runtime_host_vtable_t vtable = {
      .protocol_version = PJ_DATA_SOURCE_PROTOCOL_VERSION,
      .struct_size = sizeof(PJ_data_source_runtime_host_vtable_t),
      .get_last_error = rhGetLastError,
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

bool DataSourceSession::setupAndStart(const std::string& config_json) {
  auto ds_result = engine_.createDataset(PJ::DatasetDescriptor{.source_name = source_name_, .time_domain_id = td_id_});
  if (!ds_result) {
    std::cerr << "Failed to create dataset: " << ds_result.error() << "\n";
    return false;
  }

  // Create write host
  PJ_data_source_handle_t source_handle{static_cast<uint32_t>(*ds_result)};
  write_host_ = std::make_unique<PJ::DatastoreSourceWriteHost>(engine_, source_handle);

  // Wire delegated ingest bridge state
  runtime_state_.engine = &engine_;
  runtime_state_.dataset_id = *ds_result;
  runtime_state_.registry = registry_;

  // Bind hosts
  (void)handle_.bindWriteHost(write_host_->raw());
  (void)handle_.bindRuntimeHost(makeRuntimeHost(&runtime_state_));

  // Load config if provided
  if (!config_json.empty()) {
    (void)handle_.loadConfig(config_json);
  }

  return true;
}

bool DataSourceSession::startFileImport(const std::string& config_json) {
  if (!setupAndStart(config_json)) {
    last_error_ = "failed to create dataset or bind hosts";
    return false;
  }

  bool ok = handle_.start();
  if (!ok) {
    last_error_ = handle_.lastError();
    std::cerr << "[import] start failed for '" << source_name_ << "': " << last_error_ << "\n";
  }
  write_host_->flushPending();
  // Flush all parser write hosts (delegated ingest creates per-topic writers)
  for (auto& [id, binding] : runtime_state_.parser_bindings) {
    binding.write_host->flushPending();
  }
  emit importComplete();
  return ok;
}

bool DataSourceSession::startStream(const std::string& config_json) {
  if (!setupAndStart(config_json)) {
    last_error_ = "failed to create dataset or bind hosts";
    return false;
  }
  is_stream_ = true;
  last_config_json_ = config_json;
  bool ok = handle_.start();
  if (!ok) {
    last_error_ = handle_.lastError();
    std::cerr << "[stream] start failed for '" << source_name_ << "': " << last_error_ << "\n";
  }
  return ok;
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
  return handle_.pause();
}

bool DataSourceSession::resumeStream() {
  return handle_.resume();
}

}  // namespace proto
