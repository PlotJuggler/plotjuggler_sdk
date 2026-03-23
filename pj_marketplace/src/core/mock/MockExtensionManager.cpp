#include "core/mock/MockExtensionManager.h"

namespace PJ {

MockExtensionManager::MockExtensionManager(QObject* parent)
    : ExtensionManager(nullptr, "/tmp/pj_mock_ext", "/tmp/pj_mock_pending", parent) {
  mock_installed_["csv-loader"] =
      InstalledExtension{"csv-loader", "1.0.0", {}, "/usr/lib/plotjuggler/csv-loader.so", true, {}};
  mock_installed_["ros2-streaming"] = InstalledExtension{
      "ros2-streaming", "1.1.0", {}, "/usr/lib/plotjuggler/ros2-streaming.so", true, {}};
}

// ─── Public API ──────────────────────────────────────────────────────────────

void MockExtensionManager::install(const Extension& ext) {
  if (progress_timer_ && progress_timer_->isActive()) {
    emit installError(ext.id, "Another installation is already in progress");
    return;
  }
  startMockOperation(ext, false);
}

void MockExtensionManager::uninstall(const QString& extension_id) {
  mock_installed_.remove(extension_id);
  emit uninstallFinished(extension_id, true);
}

void MockExtensionManager::update(const Extension& ext) {
  if (progress_timer_ && progress_timer_->isActive()) {
    emit installError(ext.id, "Another installation is already in progress");
    return;
  }
  startMockOperation(ext, true);
}

bool MockExtensionManager::isInstalled(const QString& id) const {
  return mock_installed_.contains(id);
}

bool MockExtensionManager::hasUpdate(const Extension& ext) const {
  if (!mock_installed_.contains(ext.id)) return false;
  return mock_installed_[ext.id].version != ext.version;
}

QMap<QString, InstalledExtension> MockExtensionManager::installedExtensions() const {
  return mock_installed_;
}

// ─── Mock progress simulation ────────────────────────────────────────────────

void MockExtensionManager::startMockOperation(const Extension& ext, bool is_update) {
  pending_ext_ = ext;
  pending_is_update_ = is_update;
  tick_ = 0;

  emit installStarted(ext.id);

  progress_timer_ = new QTimer(this);
  connect(progress_timer_, &QTimer::timeout, this, [this]() {
    tick_++;
    emit installProgress(pending_ext_.id, tick_ * (100 / kTicks));

    if (tick_ >= kTicks) {
      progress_timer_->stop();
      progress_timer_->deleteLater();
      progress_timer_ = nullptr;

      if (pending_is_update_) {
        mock_installed_[pending_ext_.id].version = pending_ext_.version;
      } else {
        mock_installed_[pending_ext_.id] = InstalledExtension{
            pending_ext_.id, pending_ext_.version, {},
            "/usr/lib/plotjuggler/" + pending_ext_.id + ".so", true, {}};
      }

      emit installFinished(pending_ext_.id, true);
    }
  });
  progress_timer_->start(100);
}

}  // namespace PJ
