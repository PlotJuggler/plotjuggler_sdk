#include "toolbox_session.hpp"

#include <iostream>

#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/sdk/toolbox_plugin_base.hpp"
#include "pj_datastore/colormap_registry_host.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"
#include "pj_plugins/host_qt/dialog_engine.hpp"

namespace proto {

// ---------------------------------------------------------------------------
// Runtime host vtable — captureless C callbacks via context pointer
// ---------------------------------------------------------------------------

static const PJ_toolbox_runtime_host_vtable_t kRuntimeVtable = {
    .protocol_version = PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION,
    .struct_size = sizeof(PJ_toolbox_runtime_host_vtable_t),

    .report_message =
        [](void* ctx, PJ_toolbox_message_level_t level, PJ_string_view_t msg) noexcept {
          (void)ctx;
          try {
            const char* lvl = level == PJ_TOOLBOX_MESSAGE_ERROR     ? "ERROR"
                              : level == PJ_TOOLBOX_MESSAGE_WARNING ? "WARNING"
                                                                    : "INFO";
            std::cerr << "[Toolbox " << lvl << "] " << std::string(msg.data, msg.size) << "\n";
          } catch (...) {}
        },

    .notify_data_changed =
        [](void* ctx) noexcept {
          try {
            auto* s = static_cast<ToolboxSession::RuntimeState*>(ctx);
            if (s->session) {
              emit s->session->dataChanged();
            }
          } catch (...) {}
        },
};

// ---------------------------------------------------------------------------
// ToolboxSession
// ---------------------------------------------------------------------------

ToolboxSession::ToolboxSession(
    PJ::DataEngine& engine, PJ::ToolboxLibrary& library, PJ::ColorMapRegistry& colormap_registry, std::string name,
    QObject* parent)
    : QObject(parent),
      engine_(engine),
      library_(library),
      colormap_registry_(colormap_registry),
      name_(std::move(name)),
      handle_(library_.createHandle()) {}

bool ToolboxSession::init(const std::string& config_json) {
  if (!handle_.valid()) {
    return false;
  }

  toolbox_host_ = std::make_unique<PJ::DatastoreToolboxHost>(engine_);
  runtime_state_.session = this;

  // Build the service registry: toolbox_host + runtime + colormap.
  bind_registry_.emplace();
  bind_registry_->registerService<PJ::sdk::ToolboxHostService>(toolbox_host_->raw());
  bind_registry_->registerService<PJ::sdk::ToolboxRuntimeHostService>(makeRuntimeHost(this));
  bind_registry_->registerService<PJ::sdk::ColorMapRegistryService>(PJ::makeColorMapRegistryHost(colormap_registry_));

  if (auto s = handle_.bind(bind_registry_->view()); !s) {
    std::cerr << "Toolbox '" << name_ << "': bind failed: " << s.error() << "\n";
    return false;
  }

  if (!config_json.empty() && config_json != "{}") {
    (void)handle_.loadConfig(config_json);
  }

  return true;
}

bool ToolboxSession::hasDialog() const {
  return handle_.valid() && (handle_.capabilities() & PJ_TOOLBOX_CAPABILITY_HAS_DIALOG) != 0;
}

bool ToolboxSession::runDialog(QWidget* parent) {
  if (!hasDialog()) {
    return false;
  }
  if (dialog_running_) {
    return false;  // prevent re-entrant opens on non-modal dialogs
  }

  auto vt_result = library_.resolveDialogVtable();
  if (!vt_result) {
    return false;
  }

  auto borrowed = handle_.getDialog();
  if (borrowed.ctx == nullptr) {
    return false;
  }

  auto dialog_handle = PJ::DialogHandle::borrowed(*vt_result, borrowed.ctx);

  PJ::DialogEngineConfig config;
  config.non_modal = isNonModal();

  PJ::DialogEngine dialog_engine(std::move(dialog_handle), config);

  dialog_running_ = true;
  emit dialogOpened();
  auto result = dialog_engine.showDialog(parent);
  dialog_running_ = false;
  emit dialogClosed();

  // Always persist the plugin's config after the dialog closes, regardless of
  // whether the user clicked OK or Close/X. Toolbox dialogs (unlike file-open
  // dialogs) are persistent workspaces — closing them should not discard state.
  (void)handle_.loadConfig(dialog_engine.savedConfig());
  flushPending();

  if (result == PJ::DialogResult::kRejected) {
    return false;
  }
  return true;
}

std::string ToolboxSession::saveConfig() const {
  if (!handle_.valid()) {
    return "{}";
  }
  std::string out;
  if (auto s = const_cast<PJ::ToolboxHandle&>(handle_).saveConfig(out); !s) {
    return "{}";
  }
  return out;
}

void ToolboxSession::flushPending() {
  if (toolbox_host_) {
    toolbox_host_->flushPending();
  }
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
