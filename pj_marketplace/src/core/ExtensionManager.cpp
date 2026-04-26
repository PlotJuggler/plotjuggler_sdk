#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QVersionNumber>
#include <filesystem>

#include "pj_marketplace/download_manager.hpp"
#include "pj_marketplace/extension_manager.hpp"
#include "pj_marketplace/platform_utils.hpp"
#include "pj_plugins/host/plugin_catalog.hpp"

namespace PJ {

namespace {

static constexpr const char* kPendingUninstallMarker = ".pj_pending_uninstall";
static constexpr const char* kPendingInstallIntent = ".pj_pending_install";

QString extRoot(const QString& extensions_dir, const QString& id) {
  return QDir(extensions_dir).absoluteFilePath(id);
}

QString pendingRoot(const QString& pending_dir, const QString& id) {
  return QDir(pending_dir).absoluteFilePath(id);
}

struct DirectoryDiscovery {
  bool found_plugin = false;
  QString error;
  InstalledExtension record;
};

struct PendingInstallIntent {
  bool valid = false;
  QString id;
  QString version;
  QString error;
};

QString pendingInstallIntentPath(const QString& root) {
  return QDir(root).absoluteFilePath(kPendingInstallIntent);
}

DirectoryDiscovery discoverExtensionDirectory(const QString& ext_root) {
  DirectoryDiscovery result;
  const auto scan = scanPluginDsos(std::filesystem::path(ext_root.toStdString()));
  if (!scan) {
    result.error = QString::fromStdString(scan.error());
    return result;
  }

  for (const auto& diag : scan->diagnostics) {
    qWarning(
        "ExtensionManager: plugin discovery diagnostic for '%s': %s", diag.path.string().c_str(), diag.message.c_str());
  }

  if (scan->plugins.empty()) {
    if (!scan->diagnostics.empty()) {
      result.error = QString::fromStdString(scan->diagnostics.front().message);
      return result;
    }
    result.error = QStringLiteral("no valid plugin DSO found");
    return result;
  }

  const PluginDescriptor& first = scan->plugins.front();
  for (const PluginDescriptor& descriptor : scan->plugins) {
    if (descriptor.id != first.id) {
      result.error = QStringLiteral("multiple embedded plugin ids in one extension directory: \"%1\" and \"%2\"")
                         .arg(QString::fromStdString(first.id), QString::fromStdString(descriptor.id));
      return result;
    }
    if (descriptor.version != first.version) {
      result.error = QStringLiteral("multiple embedded plugin versions in one extension directory for \"%1\"")
                         .arg(QString::fromStdString(first.id));
      return result;
    }
  }

  result.found_plugin = true;
  result.record.id = QString::fromStdString(first.id);
  result.record.version = QString::fromStdString(first.version);
  result.record.install_date = QFileInfo(ext_root).lastModified();
  result.record.path = ext_root;
  result.record.enabled = true;
  return result;
}

QString validateRegistryIntent(
    const DirectoryDiscovery& discovered, const QString& registry_id, const QString& registry_version) {
  if (!discovered.found_plugin) {
    return QString("Installed artifact is not a valid plugin: %1").arg(discovered.error);
  }
  if (discovered.record.id != registry_id) {
    return QString("Embedded plugin id \"%1\" does not match registry id \"%2\"")
        .arg(discovered.record.id, registry_id);
  }
  if (discovered.record.version != registry_version) {
    return QString("Embedded plugin version \"%1\" does not match registry version \"%2\"")
        .arg(discovered.record.version, registry_version);
  }
  return {};
}

bool writePendingInstallIntent(const QString& root, const Extension& ext, QString* error) {
  QFile file(pendingInstallIntentPath(root));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error != nullptr) {
      *error = QString("Could not write staged install intent: %1").arg(file.errorString());
    }
    return false;
  }

  const QByteArray data = ext.id.toUtf8() + '\n' + ext.version.toUtf8() + '\n';
  if (file.write(data) != data.size()) {
    if (error != nullptr) {
      *error = QString("Could not write staged install intent: %1").arg(file.errorString());
    }
    return false;
  }
  return true;
}

PendingInstallIntent readPendingInstallIntent(const QString& root) {
  PendingInstallIntent intent;
  QFile file(pendingInstallIntentPath(root));
  if (!file.exists()) {
    intent.error = "Staged install is missing registry intent";
    return intent;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    intent.error = QString("Could not read staged install intent: %1").arg(file.errorString());
    return intent;
  }

  const QList<QByteArray> lines = file.readAll().split('\n');
  if (lines.size() < 2 || lines[0].trimmed().isEmpty() || lines[1].trimmed().isEmpty()) {
    intent.error = "Staged install registry intent is invalid";
    return intent;
  }

  intent.valid = true;
  intent.id = QString::fromUtf8(lines[0].trimmed());
  intent.version = QString::fromUtf8(lines[1].trimmed());
  return intent;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ExtensionManager::ExtensionManager()
    : QObject(nullptr), extensions_dir_(PlatformUtils::extensionsDir()), pending_dir_(PlatformUtils::pendingDir()) {
  initComponents();
}

ExtensionManager::ExtensionManager(
    DownloadManager* downloader, const QString& extensions_dir, const QString& pending_dir, QObject* parent)
    : QObject(parent), downloader_(downloader), extensions_dir_(extensions_dir), pending_dir_(pending_dir) {
  initComponents();
}

void ExtensionManager::initComponents() {
  if (!downloader_) {
    downloader_ = new DownloadManager(this);
  }
  QDir().mkpath(extensions_dir_);
  refreshInstalledFromDisk();
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void ExtensionManager::install(const Extension& ext) {
  refreshInstalledFromDisk();
  doInstall(ext, /*staging=*/false);
}

void ExtensionManager::doInstall(const Extension& ext, bool staging, bool allow_existing) {
  if (!pending_id_.isEmpty()) {
    emit installError(ext.id, QString("Install of \"%1\" is already in progress").arg(pending_id_));
    emit installFinished(ext.id, false);
    return;
  }

  if (!allow_existing && isInstalled(ext.id)) {
    emit installError(ext.id, QString("Extension \"%1\" is already installed").arg(ext.id));
    emit installFinished(ext.id, false);
    return;
  }

  const QString platform = PlatformUtils::currentPlatform();
  if (!ext.platforms.contains(platform)) {
    emit installError(ext.id, QString("No artifact available for platform \"%1\"").arg(platform));
    emit installFinished(ext.id, false);
    return;
  }
  const Platform& artifact = ext.platforms[platform];

  const QString dest_dir = staging ? pending_dir_ : extensions_dir_;
  if (staging) {
    QDir(pendingRoot(pending_dir_, ext.id)).removeRecursively();
  }

  pending_id_ = ext.id;
  emit installStarted(ext.id);

  dl_progress_conn_ =
      connect(downloader_, &DownloadManager::progress, this, [this](int id, qint64 received, qint64 total) {
        if (id != pending_op_id_) {
          return;
        }

        if (total > 0 && !disk_space_checked_) {
          disk_space_checked_ = true;
          constexpr qint64 kExtractionOverheadFactor = 3;
          if (QStorageInfo(extensions_dir_).bytesAvailable() < total * kExtractionOverheadFactor) {
            cancel_reason_ = "Not enough disk space to install the extension";
            downloader_->cancel(pending_op_id_);
            return;
          }
        }

        const int percent = (total > 0) ? static_cast<int>(received * 100 / total) : 0;
        emit installProgress(pending_id_, percent);
      });

  dl_finished_conn_ = connect(downloader_, &DownloadManager::finished, this, [this, ext, staging](int id) {
    if (id != pending_op_id_) {
      return;
    }
    disconnectDlConns();
    disk_space_checked_ = false;

    const QString finished_id = pending_id_;
    pending_id_.clear();
    pending_op_id_ = -1;

    const QString root = staging ? pendingRoot(pending_dir_, ext.id) : extRoot(extensions_dir_, ext.id);
    const DirectoryDiscovery discovered = discoverExtensionDirectory(root);
    const QString validation_error = validateRegistryIntent(discovered, ext.id, ext.version);
    if (!validation_error.isEmpty()) {
      QDir(root).removeRecursively();
      emit installError(finished_id, validation_error);
      emit installFinished(finished_id, false);
      return;
    }

    if (staging) {
      QString intent_error;
      if (!writePendingInstallIntent(root, ext, &intent_error)) {
        QDir(root).removeRecursively();
        emit installError(finished_id, intent_error);
        emit installFinished(finished_id, false);
        return;
      }
      emit installPendingRestart(finished_id);
      return;
    }

    installed_[ext.id] = discovered.record;
    emit installFinished(finished_id, true);
  });

  dl_failed_conn_ = connect(downloader_, &DownloadManager::failed, this, [this](int id, const QString& error) {
    if (id != pending_op_id_) {
      return;
    }
    disconnectDlConns();
    disk_space_checked_ = false;

    const QString failed_id = pending_id_;
    pending_id_.clear();
    pending_op_id_ = -1;

    emit installError(failed_id, error);
    emit installFinished(failed_id, false);
  });

  dl_cancelled_conn_ = connect(downloader_, &DownloadManager::cancelled, this, [this](int id) {
    if (id != pending_op_id_) {
      return;
    }
    disconnectDlConns();

    const QString cancelled_id = pending_id_;
    pending_id_.clear();
    pending_op_id_ = -1;
    disk_space_checked_ = false;

    QDir(extRoot(extensions_dir_, cancelled_id)).removeRecursively();
    QDir(pendingRoot(pending_dir_, cancelled_id)).removeRecursively();

    const QString reason = cancel_reason_.isEmpty() ? "Installation was cancelled" : cancel_reason_;
    cancel_reason_.clear();
    emit installError(cancelled_id, reason);
    emit installFinished(cancelled_id, false);
  });

  pending_op_id_ = downloader_->fetch(QUrl(artifact.url), artifact.checksum, dest_dir);
}

void ExtensionManager::uninstall(const QString& extension_id) {
  refreshInstalledFromDisk();

  if (!installed_.contains(extension_id)) {
    emit uninstallError(extension_id, QString("Extension \"%1\" is not installed").arg(extension_id));
    emit uninstallFinished(extension_id, false);
    return;
  }

  const QString dir_path = installed_[extension_id].path;

  if (!QDir(dir_path).removeRecursively()) {
    if (PlatformUtils::isWindows()) {
      schedulePendingUninstall(dir_path);
      installed_.remove(extension_id);
      emit uninstallPendingRestart(extension_id);
    } else {
      emit uninstallError(
          extension_id, QString("Could not remove directory \"%1\" — the plugin may still be loaded").arg(dir_path));
      emit uninstallFinished(extension_id, false);
    }
    return;
  }

  installed_.remove(extension_id);
  emit uninstallFinished(extension_id, true);
}

void ExtensionManager::update(const Extension& ext) {
  refreshInstalledFromDisk();

  if (PlatformUtils::isWindows()) {
    doInstall(ext, /*staging=*/true, /*allow_existing=*/true);
    return;
  }

  if (installed_.contains(ext.id)) {
    const QString current_version = installed_[ext.id].version;
    const QString current_path = installed_[ext.id].path;

    const QString candidate = PlatformUtils::backupDir() + "/" + ext.id + "-" + current_version;
    QDir().mkpath(PlatformUtils::backupDir());

    if (!QDir().rename(current_path, candidate)) {
      emit installError(
          ext.id, QString("Could not back up \"%1\" — update aborted to prevent data loss").arg(current_path));
      emit installFinished(ext.id, false);
      return;
    }
    installed_.remove(ext.id);
  }

  doInstall(ext, PlatformUtils::isWindows());
}

void ExtensionManager::applyPendingInstalls() {
  const QDir pending(pending_dir_);
  if (!pending.exists()) {
    return;
  }

  for (const QFileInfo& entry : pending.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString staged_dir = entry.absoluteFilePath();
    const QString staged_name = entry.fileName();

    auto failStagedInstall = [&](const QString& signal_id, const QString& message) {
      qWarning(
          "ExtensionManager: staged install '%s' failed validation: %s", qPrintable(staged_dir), qPrintable(message));
      QDir(staged_dir).removeRecursively();
      emit installError(signal_id, message);
      emit installFinished(signal_id, false);
    };

    if (staged_name.isEmpty()) {
      failStagedInstall(staged_name, "Staged install directory has no id");
      continue;
    }

    const PendingInstallIntent intent = readPendingInstallIntent(staged_dir);
    if (!intent.valid) {
      failStagedInstall(staged_name, intent.error);
      continue;
    }
    if (intent.id != staged_name) {
      failStagedInstall(
          staged_name,
          QString("Staged install directory \"%1\" does not match registry id \"%2\"").arg(staged_name, intent.id));
      continue;
    }

    const DirectoryDiscovery discovered = discoverExtensionDirectory(staged_dir);
    const QString validation_error = validateRegistryIntent(discovered, intent.id, intent.version);
    if (!validation_error.isEmpty()) {
      failStagedInstall(intent.id, validation_error);
      continue;
    }

    const QString dst = extRoot(extensions_dir_, intent.id);
    if (QDir(dst).exists() && !QDir(dst).removeRecursively()) {
      qWarning("ExtensionManager: failed to remove existing extension directory '%s'", qPrintable(dst));
      emit installError(intent.id, QString("Could not remove existing extension directory \"%1\"").arg(dst));
      emit installFinished(intent.id, false);
      continue;
    }
    installed_.remove(intent.id);

    if (!QDir().rename(staged_dir, dst)) {
      qWarning(
          "ExtensionManager: failed to promote staged install '%s' to '%s'", qPrintable(staged_dir), qPrintable(dst));
      emit installError(intent.id, QString("Could not promote staged install to \"%1\"").arg(dst));
      emit installFinished(intent.id, false);
      continue;
    }

    QFile::remove(pendingInstallIntentPath(dst));
    InstalledExtension record = discovered.record;
    record.path = dst;
    record.install_date = QFileInfo(dst).lastModified();
    installed_[intent.id] = record;
    emit installFinished(intent.id, true);
  }
}

void ExtensionManager::applyPendingUninstalls() {
  const QDir dir(extensions_dir_);
  for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (!QFile::exists(entry.absoluteFilePath() + "/" + kPendingUninstallMarker)) {
      continue;
    }
    const QString id = entry.fileName();
    if (QDir(entry.absoluteFilePath()).removeRecursively() && !id.isEmpty()) {
      installed_.remove(id);
    }
  }
}

bool ExtensionManager::isInstalled(const QString& id) const {
  return installed_.contains(id);
}

bool ExtensionManager::hasPendingInstall(const QString& id) const {
  // Marker existence is enough for UI predicates; applyPendingInstalls() does
  // the full validation (intent contents, DSO presence, registry-vs-embedded
  // match) and tears down anything broken on next startup.
  return QFile::exists(pendingInstallIntentPath(pendingRoot(pending_dir_, id)));
}

bool ExtensionManager::hasPendingUninstall(const QString& id) const {
  return QFile::exists(extRoot(extensions_dir_, id) + "/" + kPendingUninstallMarker);
}

bool ExtensionManager::hasUpdate(const Extension& ext) const {
  if (!installed_.contains(ext.id)) {
    return false;
  }

  const QVersionNumber installed_ver = QVersionNumber::fromString(installed_[ext.id].version);
  const QVersionNumber latest = QVersionNumber::fromString(ext.version);
  return QVersionNumber::compare(latest, installed_ver) > 0;
}

QMap<QString, InstalledExtension> ExtensionManager::installedExtensions() const {
  return installed_;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ExtensionManager::disconnectDlConns() {
  disconnect(dl_progress_conn_);
  disconnect(dl_finished_conn_);
  disconnect(dl_failed_conn_);
  disconnect(dl_cancelled_conn_);
}

void ExtensionManager::schedulePendingUninstall(const QString& path) {
  QFile marker(path + "/" + kPendingUninstallMarker);
  marker.open(QIODevice::WriteOnly);  // content irrelevant; existence is the signal
}

void ExtensionManager::refreshInstalledFromDisk() {
  QMap<QString, InstalledExtension> discovered;
  const QDir dir(extensions_dir_);
  for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString root = entry.absoluteFilePath();
    if (QFile::exists(root + "/" + kPendingUninstallMarker)) {
      continue;
    }

    const DirectoryDiscovery item = discoverExtensionDirectory(root);
    if (!item.found_plugin) {
      qWarning("ExtensionManager: ignoring extension directory '%s': %s", qPrintable(root), qPrintable(item.error));
      continue;
    }
    if (discovered.contains(item.record.id)) {
      qWarning(
          "ExtensionManager: duplicate embedded extension id '%s' in '%s'; keeping first", qPrintable(item.record.id),
          qPrintable(root));
      continue;
    }
    discovered[item.record.id] = item.record;
  }
  installed_ = std::move(discovered);
}

}  // namespace PJ
