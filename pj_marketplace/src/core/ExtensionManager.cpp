#include "core/ExtensionManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStorageInfo>
#include <QVersionNumber>

#include "core/DownloadManager.h"
#include "core/PlatformUtils.h"

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

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void ExtensionManager::install(const Extension& ext) {
  if (!pending_id_.isEmpty()) {
    emit installError(ext.id, QString("Install of \"%1\" is already in progress").arg(pending_id_));
    return;
  }

  if (isInstalled(ext.id)) {
    emit installError(ext.id, QString("Extension \"%1\" is already installed").arg(ext.id));
    return;
  }

  // Resolve the artifact for the running platform before touching the network.
  const QString platform = PlatformUtils::currentPlatform();
  if (!ext.platforms.contains(platform)) {
    emit installError(ext.id, QString("No artifact available for platform \"%1\"").arg(platform));
    return;
  }
  const Platform& artifact = ext.platforms[platform];

  // On Windows, DLLs that are currently loaded cannot be overwritten. Extract to a
  // staging directory (.pending/) instead and let the user restart to activate.
  const bool staging = PlatformUtils::isWindows();
  const QString dest_dir = (staging ? pending_dir_ : extensions_dir_) + "/" + ext.id;

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
    disconnect_dl_conns();
    disk_space_checked_ = false;

    const QString finished_id = pending_id_;
    pending_id_.clear();
    pending_op_id_ = -1;

    if (staging) {
      // Save metadata so applyPendingInstalls() can reconstruct the record after restart.
      save_pending_meta(ext);
      emit installPendingRestart(finished_id);
    } else {
      InstalledExtension record;
      record.id = ext.id;
      record.version = ext.version;
      record.install_date = QDateTime::currentDateTimeUtc();
      record.path = extensions_dir_ + "/" + ext.id;
      record.enabled = true;

      installed_[ext.id] = record;
      saveState();

      emit installFinished(finished_id, true);
    }
  });

  dl_failed_conn_ = connect(downloader_, &DownloadManager::failed, this, [this](int id, const QString& error) {
    if (id != pending_op_id_) {
      return;
    }
    disconnect_dl_conns();
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
    disconnect_dl_conns();

    const QString cancelled_id = pending_id_;
    pending_id_.clear();
    pending_op_id_ = -1;
    disk_space_checked_ = false;

    // Remove any partial files written to disk before the cancel arrived.
    // Both possible locations are cleaned regardless of platform to handle edge cases.
    QDir(extensions_dir_ + "/" + cancelled_id).removeRecursively();
    QDir(pending_dir_ + "/" + cancelled_id).removeRecursively();

    emit installError(cancelled_id, "Installation was cancelled");
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
    // On Windows the DLL may still be mapped by the host process (F-14, staging deferred to April+).
    // Report the error rather than corrupting the state file with a phantom entry.
    emit uninstallError(
        extension_id, QString("Could not remove directory \"%1\" — the plugin may still be loaded").arg(dir_path));
    emit uninstallFinished(extension_id, false);
    return;
  }

  installed_.remove(extension_id);
  saveState();
  emit uninstallFinished(extension_id, true);
}

void ExtensionManager::update(const Extension& ext) {
  // Remove the current files first so the fetch step gets a clean destination directory.
  if (installed_.contains(ext.id)) {
    QDir(installed_[ext.id].path).removeRecursively();
    installed_.remove(ext.id);
    saveState();
  }
  install(ext);
}

void ExtensionManager::applyPendingInstalls() {
  const QDir pending(pending_dir_);
  if (!pending.exists()) {
    return;
  }

  bool state_changed = false;

  for (const QFileInfo& entry : pending.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString id = entry.fileName();
    const QString src = entry.absoluteFilePath();
    const QString dst = extensions_dir_ + "/" + id;

    // Read the metadata written at staging time to recover version and install date.
    QFile meta_file(src + "/pj_meta.json");
    if (!meta_file.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QJsonObject meta = QJsonDocument::fromJson(meta_file.readAll()).object();
    meta_file.close();

    // Remove any existing installation so the rename cannot fail on a non-empty target.
    QDir(dst).removeRecursively();

    if (!QDir().rename(src, dst)) {
      continue;
    }

    // Remove the metadata file from the now-active directory — it is only needed for staging.
    QFile::remove(dst + "/pj_meta.json");

    InstalledExtension record;
    record.id = id;
    record.version = meta["version"].toString();
    record.install_date = QDateTime::fromString(meta["install_date"].toString(), Qt::ISODate);
    record.path = dst;
    record.enabled = true;

    installed_[id] = record;
    state_changed = true;

    emit installFinished(id, true);
  }

  if (state_changed) {
    saveState();
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
  const QVersionNumber installed = QVersionNumber::fromString(installed_[ext.id].version);
  const QVersionNumber latest = QVersionNumber::fromString(ext.version);
  return QVersionNumber::compare(latest, installed) > 0;
}

QMap<QString, InstalledExtension> ExtensionManager::installedExtensions() const {
  return installed_;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ExtensionManager::disconnect_dl_conns() {
  disconnect(dl_progress_conn_);
  disconnect(dl_finished_conn_);
  disconnect(dl_failed_conn_);
  disconnect(dl_cancelled_conn_);
}

void ExtensionManager::save_pending_meta(const Extension& ext) {
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

// installed.json lives inside extensions_dir so that a test pointing to a temp
// directory gets a fully self-contained state without touching ~/.plotjuggler
static constexpr const char* kStateFileName = "/installed.json";

void ExtensionManager::loadState() {
  QFile file(extensions_dir_ + kStateFileName);
  if (!file.open(QIODevice::ReadOnly)) {
    return;  // First run — no state yet.
  }

  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject()) {
    return;
  }

  for (const QJsonValue& val : doc.object()["installed"].toArray()) {
    if (!val.isObject()) {
      continue;
    }
    const QJsonObject obj = val.toObject();

    InstalledExtension inst;
    inst.id = obj["id"].toString();
    inst.version = obj["version"].toString();
    inst.install_date = QDateTime::fromString(obj["install_date"].toString(), Qt::ISODate);
    inst.path = obj["path"].toString();
    inst.enabled = obj["enabled"].toBool(true);
    inst.backup_path = obj["backup_path"].toString();

    if (!inst.id.isEmpty()) {
      installed_[inst.id] = inst;
    }
  }
}

void ExtensionManager::saveState() {
  QJsonArray array;
  for (const InstalledExtension& inst : installed_) {
    QJsonObject obj;
    obj["id"] = inst.id;
    obj["version"] = inst.version;
    obj["install_date"] = inst.install_date.toString(Qt::ISODate);
    obj["path"] = inst.path;
    obj["enabled"] = inst.enabled;
    if (!inst.backup_path.isEmpty()) {
      obj["backup_path"] = inst.backup_path;
    }
    array.append(obj);
  }

  const QJsonDocument doc(QJsonObject{{"installed", array}});

  QFile file(extensions_dir_ + kStateFileName);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(doc.toJson());
  }
}

}  // namespace PJ
