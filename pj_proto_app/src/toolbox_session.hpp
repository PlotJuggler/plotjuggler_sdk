#pragma once

#include <QObject>
#include <memory>
#include <optional>
#include <string>

#include "pj_base/toolbox_protocol.h"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/plugin_data_host.hpp"
#include "pj_plugins/host/service_registry_builder.hpp"
#include "pj_plugins/host/toolbox_handle.hpp"
#include "pj_plugins/host/toolbox_library.hpp"
#include "plugin_registry.hpp"

namespace PJ {
class ColorMapRegistry;
}  // namespace PJ

namespace proto {

class ToolboxSession : public QObject {
  Q_OBJECT

 public:
  ToolboxSession(
      PJ::DataEngine& engine, PJ::ToolboxLibrary& library, PJ::ColorMapRegistry& colormap_registry, std::string name,
      QObject* parent = nullptr);

  /// Bind hosts and load persisted config. Returns false on error.
  bool init(const std::string& config_json = "{}");

  /// Open the plugin's dialog (modal or non-modal per capability). Returns true if accepted.
  bool runDialog(QWidget* parent);

  /// Flush any pending writes to the DataEngine.
  void flushPending();

  [[nodiscard]] const std::string& name() const {
    return name_;
  }
  [[nodiscard]] bool hasDialog() const;
  [[nodiscard]] std::string saveConfig() const;

 signals:
  void dataChanged();
  void dialogOpened();
  void dialogClosed();

 public:
  // Public so the file-scope static vtable lambdas can cast to it.
  struct RuntimeState {
    std::string last_error;
    ToolboxSession* session = nullptr;
  };

 private:
  static PJ_toolbox_runtime_host_t makeRuntimeHost(ToolboxSession* self);

  bool isNonModal() const;

  PJ::DataEngine& engine_;
  PJ::ToolboxLibrary& library_;
  PJ::ColorMapRegistry& colormap_registry_;
  std::string name_;
  PJ::ToolboxHandle handle_;
  std::unique_ptr<PJ::DatastoreToolboxHost> toolbox_host_;
  std::optional<PJ::ServiceRegistryBuilder> bind_registry_;

  // Runtime host state — must outlive handle_
  RuntimeState runtime_state_;
  bool dialog_running_ = false;
};

}  // namespace proto
