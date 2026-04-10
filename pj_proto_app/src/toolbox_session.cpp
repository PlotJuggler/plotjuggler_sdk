#include "toolbox_session.hpp"

#include <iostream>

#include "pj_plugins/host_qt/dialog_engine.hpp"

namespace proto {

// ---------------------------------------------------------------------------
// Runtime host vtable — captureless C callbacks via context pointer
// ---------------------------------------------------------------------------

static const PJ_toolbox_runtime_host_vtable_t kRuntimeVtable = {
    .protocol_version = PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION,
    .struct_size = sizeof(PJ_toolbox_runtime_host_vtable_t),

    .get_last_error =
        [](void* ctx) -> const char* {
          auto* s = static_cast<ToolboxSession::RuntimeState*>(ctx);
          return s->last_error.empty() ? nullptr : s->last_error.c_str();
        },

    .report_message =
        [](void* ctx, PJ_toolbox_message_level_t level, PJ_string_view_t msg) {
          (void)ctx;
          const char* lvl = level == PJ_TOOLBOX_MESSAGE_ERROR     ? "ERROR"
                            : level == PJ_TOOLBOX_MESSAGE_WARNING ? "WARNING"
                                                                  : "INFO";
          std::cerr << "[Toolbox " << lvl << "] " << std::string(msg.data, msg.size) << "\n";
        },

    .notify_data_changed =
        [](void* ctx) {
          auto* s = static_cast<ToolboxSession::RuntimeState*>(ctx);
          if (s->session) emit s->session->dataChanged();
        },
};

// ---------------------------------------------------------------------------
// ToolboxSession
// ---------------------------------------------------------------------------

ToolboxSession::ToolboxSession(PJ::DataEngine& engine, PJ::ToolboxLibrary& library,
                               std::string name, QObject* parent)
    : QObject(parent),
      engine_(engine),
      library_(library),
      name_(std::move(name)),
      handle_(library_.createHandle()) {}

bool ToolboxSession::init(const std::string& config_json) {
  if (!handle_.valid()) return false;

  toolbox_host_ = std::make_unique<PJ::DatastoreToolboxHost>(engine_);
  runtime_state_.session = this;

  if (!handle_.bindToolboxHost(toolbox_host_->raw())) {
    std::cerr << "Toolbox '" << name_ << "': bindToolboxHost failed: " << handle_.lastError() << "\n";
    return false;
  }

  auto runtime_host = makeRuntimeHost(this);
  if (!handle_.bindRuntimeHost(runtime_host)) {
    std::cerr << "Toolbox '" << name_ << "': bindRuntimeHost failed: " << handle_.lastError() << "\n";
    return false;
  }

  if (!config_json.empty() && config_json != "{}") {
    (void)handle_.loadConfig(config_json);
  }

  return true;
}

bool ToolboxSession::hasDialog() const {
  return handle_.valid() &&
         (handle_.capabilities() & PJ_TOOLBOX_CAPABILITY_HAS_DIALOG) != 0;
}

bool ToolboxSession::runDialog(QWidget* parent) {
  if (!hasDialog()) return false;
  if (dialog_running_) return false;  // prevent re-entrant opens on non-modal dialogs

  auto vt_result = library_.resolveDialogVtable();
  if (!vt_result) return false;

  auto* dialog_ctx = handle_.dialogContext();
  if (dialog_ctx == nullptr) return false;

  auto dialog_handle = PJ::DialogHandle::borrowed(*vt_result, dialog_ctx);

  PJ::DialogEngineConfig config;
  config.non_modal = isNonModal();

  PJ::DialogEngine dialog_engine(std::move(dialog_handle), config);

  dialog_running_ = true;
  auto result = dialog_engine.showDialog(parent);
  dialog_running_ = false;

  if (result == PJ::DialogResult::kRejected) return false;

  (void)handle_.loadConfig(dialog_engine.savedConfig());
  flushPending();
  return true;
}

std::string ToolboxSession::saveConfig() const {
  return handle_.valid() ? handle_.saveConfig() : "{}";
}

void ToolboxSession::flushPending() {
  if (toolbox_host_) toolbox_host_->flushPending();
}

bool ToolboxSession::isNonModal() const {
  return handle_.valid() && (handle_.capabilities() & PJ_TOOLBOX_CAPABILITY_NON_MODAL_DIALOG) != 0;
}

PJ_toolbox_runtime_host_t ToolboxSession::makeRuntimeHost(ToolboxSession* self) {
  return PJ_toolbox_runtime_host_t{
      .ctx = &self->runtime_state_,
      .vtable = &kRuntimeVtable,
  };
}

}  // namespace proto
