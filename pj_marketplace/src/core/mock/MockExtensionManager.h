#pragma once

#include <QMap>
#include <QObject>
#include <QTimer>

#include "core/ExtensionManager.h"
#include "models/InstalledExtension.h"

namespace PJ {

/// Simulates the full ExtensionManager lifecycle using QTimers instead of real downloads.
///
/// Pre-populated with two installed extensions (csv-loader v1.0.0, ros2-streaming v1.1.0).
/// install() / update() emit installStarted → installProgress(0..100) → installFinished(true).
/// uninstall() removes the entry and emits uninstallFinished(true) immediately.
///
/// Used by the standalone Marketplace app until the real ExtensionManager is wired up.
class MockExtensionManager : public ExtensionManager {
  Q_OBJECT

 public:
  explicit MockExtensionManager(QObject* parent = nullptr);
  ~MockExtensionManager() override = default;

  void install(const Extension& ext) override;
  void uninstall(const QString& extension_id) override;
  void update(const Extension& ext) override;

  bool isInstalled(const QString& id) const override;
  bool hasUpdate(const Extension& ext) const override;
  QMap<QString, InstalledExtension> installedExtensions() const override;

 private:
  void start_mock_operation(const Extension& ext, bool is_update);

  QMap<QString, InstalledExtension> mock_installed_;
  QTimer* progress_timer_ = nullptr;
  Extension pending_ext_;
  bool pending_is_update_ = false;
  int tick_ = 0;

  static constexpr int kTicks = 10;
};

}  // namespace PJ
