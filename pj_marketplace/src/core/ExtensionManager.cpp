#include "pj_marketplace/extension_manager.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStorageInfo>
#include <QVersionNumber>

#include "pj_marketplace/download_manager.hpp"
#include "pj_marketplace/platform_utils.hpp"

namespace PJ {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ExtensionManager::ExtensionManager(DownloadManager* downloader, const QString& extensions_dir,
                                   const QString& pending_dir, QObject* parent)
    : QObject(parent), downloader_(downloader), extensions_dir_(extensions_dir), pending_dir_(pending_dir) {
  QDir().mkpath(extensions_dir_);
  loadState();
}

static constexpr const char* kManifestFileName = "manifest.json";
static constexpr const char* kPendingUninstallMarker = ".pj_pending_uninstall";

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void ExtensionManager::install(const Extension& ext) {
  if (!pending_id_.isEmpty()) {
    emit installError(ext.id, QString("Install of \"%1\" is already in progress").arg(pending_id_));
    emit installFinished(ext.id, false);
    return;
  }

  if (isInstalled(ext.id)) {
    emit installError(ext.id, QString("Extension \"%1\" is already installed").arg(ext.id));
    emit installFinished(ext.id, false);
    return;
  }

  // Resolve the artifact for the running platform before touching the network.
  const QString platform = PlatformUtils::currentPlatform();
  if (!ext.platforms.contains(platform)) {
    emit installError(ext.id, QString("No artifact available for platform \"%1\"").arg(platform));
    emit installFinished(ext.id, false);
    return;
  }
  const Platform& artifact = ext.platforms[platform];

  // On Windows, DLLs that are currently loaded cannot be overwritten. Extract to a
  // staging directory (.pending/) instead and let the user restart to activate.
  const bool staging = PlatformUtils::isWindows();
  const QString dest_dir = staging ? pending_dir_ : extensions_dir_;

  pending_id_ = ext.id;
  emit installStarted(ext.id);

  dl_progress_conn_ =
      connect(downloader_, &DownloadManager::progress, this, [this](int id, qint64 received, qint64 total) {
        if (id != pending_op_id_) {
          return;
        }

        if (total > 0 && !disk_space_checked_) {
          disk_space_checked_ = true;
          // Extracted content is typically 2-4x the compressed size; 3 is a conservative estimate.
          // If Content-Length is absent (total == 0) the check is skipped entirely.
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

  // Capture ext and staging by value: they may go out of scope before the fetch completes.
  dl_finished_conn_ = connect(downloader_, &DownloadManager::finished, this, [this, ext, staging](int id) {
    if (id != pending_op_id_) {
      return;
    }
    disconnectDlConns();
    disk_space_checked_ = false;

    const QString finished_id = pending_id_;
    pending_id_.clear();
    pending_op_id_ = -1;

    if (staging) {
      emit installPendingRestart(finished_id);
    } else {
      const QString ext_root = extensions_dir_ + "/" + ext.id;

      InstalledExtension record;
      record.id = ext.id;
      record.version = ext.version;
      record.install_date = QDateTime::currentDateTimeUtc();
      record.path = ext_root;
      record.enabled = true;

      QFile manifest_file(ext_root + "/" + kManifestFileName);
      if (manifest_file.open(QIODevice::ReadOnly)) {
        const QJsonObject manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
        if (!manifest["version"].toString().isEmpty()) {
          record.version = manifest["version"].toString();
        }
      }

      installed_[ext.id] = record;
      emit installFinished(finished_id, true);
    }
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

    // Partial files are intentionally preserved on failure: the directory may have contained
    // a previous valid installation that pre-dates this failed attempt. Cleanup on cancel
    // is handled separately because a cancel is always user-initiated on a fresh install.
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

    // Remove any partial files written to disk before the cancel arrived.
    // Both possible locations are cleaned regardless of platform to handle edge cases.
    QDir(extensions_dir_ + "/" + cancelled_id).removeRecursively();
    QDir(pending_dir_ + "/" + cancelled_id).removeRecursively();

    const QString reason = cancel_reason_.isEmpty() ? "Installation was cancelled" : cancel_reason_;
    cancel_reason_.clear();
    emit installError(cancelled_id, reason);
    emit installFinished(cancelled_id, false);
  });

  pending_op_id_ = downloader_->fetch(QUrl(artifact.url), artifact.checksum, dest_dir);
}

void ExtensionManager::uninstall(const QString& extension_id) {
  if (!installed_.contains(extension_id)) {
    emit uninstallError(extension_id, QString("Extension \"%1\" is not installed").arg(extension_id));
    emit uninstallFinished(extension_id, false);
    return;
  }

  const QString dir_path = installed_[extension_id].path;

  if (!QDir(dir_path).removeRecursively()) {
    if (PlatformUtils::isWindows()) {
      // The DLL is still mapped by the host process. Deregister the extension immediately
      // and mark the directory for deletion at the next startup.
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
  QString backup_path;

  if (installed_.contains(ext.id)) {
    const QString current_version = installed_[ext.id].version;
    const QString current_path    = installed_[ext.id].path;

    // Back up the current version before downloading the new one (F-12).
    // If the install subsequently fails, the files remain in backup_path and
    // can be restored manually until automatic rollback (F-13, April+) is implemented.
    const QString candidate = PlatformUtils::backupDir() + "/" + ext.id + "-" + current_version;
    QDir().mkpath(PlatformUtils::backupDir());

    if (!QDir().rename(current_path, candidate)) {
      emit installError(ext.id,
          QString("Could not back up \"%1\" — update aborted to prevent data loss")
              .arg(current_path));
      emit installFinished(ext.id, false);
      return;
    }
    backup_path = candidate;

    installed_.remove(ext.id);
  }

  // Once the install completes successfully, attach the backup location to the
  // new record so future rollback code (F-13, April+) can find it.
  if (!backup_path.isEmpty()) {
    connect(this, &ExtensionManager::installFinished, this,
        [this, backup_path](const QString& finished_id, bool success) {
          if (success && installed_.contains(finished_id)) {
            installed_[finished_id].backup_path = backup_path;
            saveState();
          }
        },
        Qt::SingleShotConnection);
  }

  install(ext);
}

void ExtensionManager::applyPendingInstalls() {
  const QDir pending(pending_dir_);
  if (!pending.exists()) {
    return;
  }

  for (const QFileInfo& entry : pending.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString src = entry.absoluteFilePath();
    const QString dst = extensions_dir_ + "/" + entry.fileName();

    // manifest.json is part of the artifact — read it before moving the directory.
    QFile manifest_file(src + "/" + kManifestFileName);
    if (!manifest_file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
    manifest_file.close();

    const QString id = manifest["id"].toString();
    if (id.isEmpty()) {
      continue;
    }

    // Remove any existing installation so the rename cannot fail on a non-empty target.
    QDir(dst).removeRecursively();

    if (!QDir().rename(src, dst)) {
      continue;
    }

    InstalledExtension record;
    record.id = id;
    record.version = manifest["version"].toString();
    record.install_date = QDateTime::currentDateTimeUtc();
    record.path = dst;
    record.enabled = true;

    installed_[id] = record;
    emit installFinished(id, true);
  }
}

void ExtensionManager::applyPendingUninstalls() {
  const QDir dir(extensions_dir_);
  for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (!QFile::exists(entry.absoluteFilePath() + "/" + kPendingUninstallMarker)) {
      continue;
    }
    QDir(entry.absoluteFilePath()).removeRecursively();
  }
}

bool ExtensionManager::isInstalled(const QString& id) const {
  return installed_.contains(id);
}

bool ExtensionManager::hasUpdate(const Extension& ext) const {
  if (!installed_.contains(ext.id)) {
    return false;
  }

  // QVersionNumber handles multi-segment comparison correctly:
  // "1.10.0" > "1.9.0", unlike a raw string compare which would invert them.
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

void ExtensionManager::savePendingMeta(const Extension& ext) {
  QJsonObject obj;
  obj["id"] = ext.id;
  obj["version"] = ext.version;
  obj["install_date"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

  QFile file(pending_dir_ + "/" + ext.id + "/pj_meta.json");
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(obj).toJson());
  }
}

// ---------------------------------------------------------------------------
// Private — state persistence
// ---------------------------------------------------------------------------

void ExtensionManager::loadState() {
  const QDir dir(extensions_dir_);
  for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString ext_root = entry.absoluteFilePath();

    QFile manifest_file(ext_root + "/" + kManifestFileName);
    if (!manifest_file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
    const QString id = manifest["id"].toString();
    if (id.isEmpty()) {
      continue;
    }

    InstalledExtension inst;
    inst.id = id;
    inst.version = manifest["version"].toString();
    inst.install_date = entry.lastModified();
    inst.path = ext_root;
    inst.enabled = true;

    installed_[id] = inst;
  }
}

void ExtensionManager::saveState() {
  // QJsonArray array;
  // for (const InstalledExtension& inst : installed_) {
  //   QJsonObject obj;
  //   obj["id"] = inst.id;
  //   obj["version"] = inst.version;
  //   obj["install_date"] = inst.install_date.toString(Qt::ISODate);
  //   obj["path"] = inst.path;
  //   obj["enabled"] = inst.enabled;
  //   if (!inst.backup_path.isEmpty()) {
  //     obj["backup_path"] = inst.backup_path;
  //   }
  //   array.append(obj);
  // }

  // const QJsonDocument doc(QJsonObject{{"installed", array}});

  // static constexpr const char* kStateFileName = "/installed.json";
  // QFile file(extensions_dir_ + kStateFileName);
  // if (file.open(QIODevice::WriteOnly)) {
  //   file.write(doc.toJson());
  // }
}

}  // namespace PJ
