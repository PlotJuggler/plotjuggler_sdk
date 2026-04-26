#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUuid>
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
static constexpr const char* kQuarantinePrefix = ".pj_quarantine_";
static constexpr int kMaxDiagnostics = 50;

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

QString invalidExtensionIdReason(const QString& id) {
  if (id.isEmpty()) {
    return "Extension id is empty";
  }
  if (id == "." || id == ".." || id.contains('/') || id.contains('\\')) {
    return QString("Extension id \"%1\" is not safe for filesystem paths").arg(id);
  }
  return {};
}

QString makeTransactionRoot(const QString& parent, const QString& id) {
  return QDir(parent).absoluteFilePath(
      QString(".pj_install_%1_%2").arg(id, QUuid::createUuid().toString(QUuid::Id128)));
}

QString candidateRoot(const QString& transaction_root, const QString& id) {
  return QDir(transaction_root).absoluteFilePath(id);
}

void removeDirectoryIfSet(const QString& path) {
  if (!path.isEmpty()) {
    QDir(path).removeRecursively();
  }
}

bool isTransactionDirectoryName(const QString& name) {
  return name.startsWith(".pj_install_");
}

QString validateTransactionContents(const QString& transaction_root, const QString& expected_id) {
  const QDir tx_dir(transaction_root);
  const QFileInfoList entries =
      tx_dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  if (entries.size() != 1 || !entries.first().isDir() || entries.first().fileName() != expected_id) {
    return QString("Downloaded artifact must contain exactly one top-level directory named \"%1\"").arg(expected_id);
  }
  return {};
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

  const QString id = QString::fromUtf8(lines[0].trimmed());
  const QString version = QString::fromUtf8(lines[1].trimmed());
  // Defend against tampered or corrupted intent files: a path-traversal id, or a
  // version that contains anything outside the semver alphabet, must not be
  // trusted later as a directory name or version comparison input.
  if (const QString id_error = invalidExtensionIdReason(id); !id_error.isEmpty()) {
    intent.error = QString("Staged install registry intent has unsafe id: %1").arg(id_error);
    return intent;
  }
  static const QRegularExpression kVersionRe(QStringLiteral("^[0-9A-Za-z._+-]+$"));
  if (!kVersionRe.match(version).hasMatch()) {
    intent.error = QString("Staged install registry intent has unsafe version \"%1\"").arg(version);
    return intent;
  }

  intent.valid = true;
  intent.id = id;
  intent.version = version;
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
    DownloadManager* downloader, const QString& extensions_dir, const QString& pending_dir, DiagnosticSink sink,
    QObject* parent)
    : QObject(parent),
      downloader_(downloader),
      extensions_dir_(extensions_dir),
      pending_dir_(pending_dir),
      sink_(std::move(sink)) {
  initComponents();
}

void ExtensionManager::initComponents() {
  if (!downloader_) {
    downloader_ = new DownloadManager(this);
  }
  if (!QDir().mkpath(extensions_dir_)) {
    reportDiagnostic({}, QString("Could not create extensions directory \"%1\"").arg(extensions_dir_), true);
  }
  // Drain any restart-deferred work before computing the installed snapshot, so the
  // result reflects post-promotion / post-cleanup reality regardless of which
  // MarketplaceWindow ctor (or host wiring) ends up using this manager.
  applyPendingUninstalls();
  applyPendingInstalls();
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
  if (const QString id_error = invalidExtensionIdReason(ext.id); !id_error.isEmpty()) {
    emitInstallFailure(ext.id, id_error);
    return;
  }

  if (!pending_id_.isEmpty()) {
    emitInstallFailure(ext.id, QString("Install of \"%1\" is already in progress").arg(pending_id_));
    return;
  }

  if (!allow_existing && isInstalled(ext.id)) {
    emitInstallFailure(ext.id, QString("Extension \"%1\" is already installed").arg(ext.id));
    return;
  }

  const QString platform = PlatformUtils::currentPlatform();
  if (!ext.platforms.contains(platform)) {
    emitInstallFailure(ext.id, QString("No artifact available for platform \"%1\"").arg(platform));
    return;
  }
  const Platform& artifact = ext.platforms[platform];

  // Extraction goes into a hidden transaction directory on the same filesystem
  // as the final destination, so the eventual rename is atomic. For deferred
  // (Windows) staging that's pending_dir_; for immediate promotion we extract
  // beside extensions_dir_ and rename in-place after validation.
  // The DSO is dlopened and its embedded manifest verified inside the
  // transaction directory BEFORE the rename, then re-verified at the final
  // location AFTER the rename — see the post-promotion check below.
  const QString dest_dir = staging ? pending_dir_ : extensions_dir_;
  QDir().mkpath(dest_dir);
  const QString transaction_root = makeTransactionRoot(dest_dir, ext.id);

  pending_id_ = ext.id;
  pending_extract_dir_ = transaction_root;
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

  dl_finished_conn_ =
      connect(downloader_, &DownloadManager::finished, this, [this, ext, staging, transaction_root](int id) {
        if (id != pending_op_id_) {
          return;
        }
        disconnectDlConns();
        disk_space_checked_ = false;

        const QString finished_id = pending_id_;
        pending_id_.clear();
        pending_op_id_ = -1;

        auto failAfterExtraction = [&](const QString& message) {
          removeDirectoryIfSet(transaction_root);
          pending_extract_dir_.clear();
          emitInstallFailure(finished_id, message);
        };

        if (const QString tx_error = validateTransactionContents(transaction_root, ext.id); !tx_error.isEmpty()) {
          failAfterExtraction(tx_error);
          return;
        }

        const QString root = candidateRoot(transaction_root, ext.id);
        const DirectoryDiscovery discovered = discoverExtensionDirectory(root);
        const QString validation_error = validateRegistryIntent(discovered, ext.id, ext.version);
        if (!validation_error.isEmpty()) {
          failAfterExtraction(validation_error);
          return;
        }

        if (staging) {
          QString intent_error;
          if (!writePendingInstallIntent(root, ext, &intent_error)) {
            failAfterExtraction(intent_error);
            return;
          }

          const QString staged_root = pendingRoot(pending_dir_, ext.id);
          if (QDir(staged_root).exists() && !QDir(staged_root).removeRecursively()) {
            failAfterExtraction(QString("Could not replace existing staged install directory \"%1\"").arg(staged_root));
            return;
          }
          if (!QDir().rename(root, staged_root)) {
            failAfterExtraction(QString("Could not stage install to \"%1\"").arg(staged_root));
            return;
          }

          removeDirectoryIfSet(transaction_root);
          pending_extract_dir_.clear();
          pending_backup_path_.clear();
          emit installPendingRestart(finished_id);
          return;
        }

        const QString dst = extRoot(extensions_dir_, ext.id);
        if (QDir(dst).exists() && !QDir(dst).removeRecursively()) {
          failAfterExtraction(QString("Could not replace existing extension directory \"%1\"").arg(dst));
          return;
        }
        if (!QDir().rename(root, dst)) {
          failAfterExtraction(QString("Could not promote install to \"%1\"").arg(dst));
          return;
        }

        // Double-check: the DSO loaded from the staging area; confirm it still
        // loads from its final location. Catches issues like rpath/relative-path
        // assumptions that hold in pending_dir_ but break in extensions_dir_.
        const DirectoryDiscovery final_check = discoverExtensionDirectory(dst);
        const QString final_error = validateRegistryIntent(final_check, ext.id, ext.version);
        if (!final_error.isEmpty()) {
          QDir(dst).removeRecursively();
          failAfterExtraction(QString("Post-promotion validation failed: %1").arg(final_error));
          return;
        }

        removeDirectoryIfSet(transaction_root);
        pending_extract_dir_.clear();
        pending_backup_path_.clear();
        registerInstalledExtension(ext.id, dst, final_check.record);
        emit installFinished(finished_id, true);
      });

  dl_failed_conn_ =
      connect(downloader_, &DownloadManager::failed, this, [this, transaction_root](int id, const QString& error) {
        if (id != pending_op_id_) {
          return;
        }
        disconnectDlConns();
        disk_space_checked_ = false;

        const QString failed_id = pending_id_;
        pending_id_.clear();
        pending_op_id_ = -1;
        pending_extract_dir_.clear();

        removeDirectoryIfSet(transaction_root);
        emitInstallFailure(failed_id, error);
      });

  dl_cancelled_conn_ = connect(downloader_, &DownloadManager::cancelled, this, [this, transaction_root](int id) {
    if (id != pending_op_id_) {
      return;
    }
    disconnectDlConns();

    const QString cancelled_id = pending_id_;
    pending_id_.clear();
    pending_op_id_ = -1;
    disk_space_checked_ = false;
    pending_extract_dir_.clear();

    removeDirectoryIfSet(transaction_root);

    const QString reason = cancel_reason_.isEmpty() ? "Installation was cancelled" : cancel_reason_;
    cancel_reason_.clear();
    emitInstallFailure(cancelled_id, reason);
  });

  pending_op_id_ = downloader_->fetch(QUrl(artifact.url), artifact.checksum, transaction_root);
}

void ExtensionManager::uninstall(const QString& extension_id) {
  refreshInstalledFromDisk();

  if (!installed_.contains(extension_id)) {
    emitUninstallFailure(extension_id, QString("Extension \"%1\" is not installed").arg(extension_id));
    return;
  }

  const QString dir_path = installed_[extension_id].path;

  if (!QDir(dir_path).removeRecursively()) {
    if (PlatformUtils::isWindows()) {
      if (!schedulePendingUninstall(dir_path)) {
        emitUninstallFailure(
            extension_id,
            QString("Could not mark \"%1\" for restart cleanup; uninstall not scheduled").arg(dir_path));
        return;
      }
      installed_.remove(extension_id);
      emit uninstallPendingRestart(extension_id);
    } else {
      emitUninstallFailure(
          extension_id, QString("Could not remove directory \"%1\" — the plugin may still be loaded").arg(dir_path));
    }
    return;
  }

  installed_.remove(extension_id);
  emit uninstallFinished(extension_id, true);
}

void ExtensionManager::update(const Extension& ext) {
  refreshInstalledFromDisk();

  if (PlatformUtils::isWindows()) {
    if (hasNewerInstalledVersion(ext)) {
      emitInstallFailure(
          ext.id, QString("Installed version \"%1\" is newer than registry version \"%2\"; downgrade is not allowed")
                      .arg(installed_[ext.id].version, ext.version));
      return;
    }
    doInstall(ext, /*staging=*/true, /*allow_existing=*/true);
    return;
  }

  const QString platform = PlatformUtils::currentPlatform();
  if (!ext.platforms.contains(platform)) {
    emitInstallFailure(ext.id, QString("No artifact available for platform \"%1\"").arg(platform));
    return;
  }

  if (installed_.contains(ext.id)) {
    const QString current_version = installed_[ext.id].version;
    const QString current_path = installed_[ext.id].path;

    if (QVersionNumber::compare(QVersionNumber::fromString(current_version), QVersionNumber::fromString(ext.version)) >
        0) {
      emitInstallFailure(
          ext.id, QString("Installed version \"%1\" is newer than registry version \"%2\"; downgrade is not allowed")
                      .arg(current_version, ext.version));
      return;
    }

    const QString candidate = PlatformUtils::backupDir() + "/" + ext.id + "-" + current_version;
    QDir().mkpath(PlatformUtils::backupDir());

    if (!QDir().rename(current_path, candidate)) {
      emitInstallFailure(
          ext.id, QString("Could not back up \"%1\" — update aborted to prevent data loss").arg(current_path));
      return;
    }
    pending_backup_path_ = candidate;
    installed_.remove(ext.id);
  }

  // The Windows branch returned earlier; here we always promote immediately.
  doInstall(ext, /*staging=*/false, /*allow_existing=*/true);
}

void ExtensionManager::applyPendingInstalls() {
  const QDir pending(pending_dir_);
  if (!pending.exists()) {
    return;
  }

  for (const QFileInfo& entry :
       pending.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
    const QString staged_dir = entry.absoluteFilePath();
    const QString staged_name = entry.fileName();
    if (isTransactionDirectoryName(staged_name)) {
      removeDirectoryIfSet(staged_dir);
      continue;
    }
    if (staged_name.startsWith(kQuarantinePrefix)) {
      // Leftover from a previous failed promotion. Skip — manual inspection only.
      continue;
    }

    auto failStagedInstall = [&](const QString& signal_id, const QString& message) {
      qWarning(
          "ExtensionManager: staged install '%s' failed validation: %s", qPrintable(staged_dir), qPrintable(message));
      QString final_message = message;
      if (!QDir(staged_dir).removeRecursively()) {
        // Removal can fail on Windows when the DSO is still locked by another
        // process. Move the broken stage aside so the next startup does not
        // re-validate the same payload and emit the same diagnostic forever.
        const QString quarantine = QDir(pending_dir_).absoluteFilePath(
            QString(kQuarantinePrefix) + entry.fileName() + "_" + QUuid::createUuid().toString(QUuid::Id128));
        if (QDir().rename(staged_dir, quarantine)) {
          final_message += QString(" Moved to quarantine: \"%1\".").arg(quarantine);
        } else {
          final_message += QString(" Could not remove or quarantine \"%1\" — manual cleanup required.").arg(staged_dir);
        }
      }
      emitInstallFailure(signal_id, final_message);
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

    // Mirror the Linux/macOS backup that `update()` performs synchronously:
    // move the existing dir aside before the staged version takes its place,
    // so a Windows update never silently destroys the previous install.
    pending_backup_path_.clear();
    DirectoryDiscovery existing;
    if (QDir(dst).exists()) {
      existing = discoverExtensionDirectory(dst);
      const QString version_tag = (existing.found_plugin && !existing.record.version.isEmpty())
                                      ? existing.record.version
                                      : QString("unknown-") + QUuid::createUuid().toString(QUuid::Id128);

      QDir().mkpath(PlatformUtils::backupDir());
      QString candidate = QDir(PlatformUtils::backupDir()).absoluteFilePath(intent.id + "-" + version_tag);
      if (QDir(candidate).exists()) {
        // Leftover backup from an earlier failed update with the same
        // id/version. Don't clobber it — keep both.
        candidate += "_" + QUuid::createUuid().toString(QUuid::Id128);
      }

      if (!QDir().rename(dst, candidate)) {
        qWarning("ExtensionManager: failed to back up '%s' before promoting staged install", qPrintable(dst));
        emitInstallFailure(
            intent.id,
            QString("Could not back up \"%1\" before update — staged install left in \"%2\"").arg(dst, staged_dir));
        continue;
      }
      pending_backup_path_ = candidate;
      installed_.remove(intent.id);
    }

    if (!QDir().rename(staged_dir, dst)) {
      qWarning(
          "ExtensionManager: failed to promote staged install '%s' to '%s'", qPrintable(staged_dir), qPrintable(dst));
      QString message = QString("Could not promote staged install to \"%1\"").arg(dst);

      // Best-effort rollback so the user is never left with no extension. If
      // the rollback rename also fails, leave pending_backup_path_ set so
      // emitInstallFailure appends "Previous version remains in backup".
      if (!pending_backup_path_.isEmpty() && QDir().rename(pending_backup_path_, dst)) {
        message += " Previous version restored.";
        pending_backup_path_.clear();
        if (existing.found_plugin) {
          registerInstalledExtension(intent.id, dst, existing.record);
        }
      }
      emitInstallFailure(intent.id, message);
      continue;
    }

    QFile::remove(pendingInstallIntentPath(dst));
    registerInstalledExtension(intent.id, dst, discovered.record);
    pending_backup_path_.clear();
    emit installFinished(intent.id, true);
  }
}

void ExtensionManager::applyPendingUninstalls() {
  const QDir dir(extensions_dir_);
  for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
    if (!QFile::exists(entry.absoluteFilePath() + "/" + kPendingUninstallMarker)) {
      continue;
    }
    const QString id = entry.fileName();
    if (QDir(entry.absoluteFilePath()).removeRecursively()) {
      if (!id.isEmpty()) {
        installed_.remove(id);
      }
    } else {
      // Leave the marker in place so the next startup retries; surface so the
      // user sees that a deferred uninstall is stuck.
      reportDiagnostic(
          id,
          QString("Could not remove extension directory \"%1\"; restart the application or close any process using it")
              .arg(entry.absoluteFilePath()),
          true);
    }
  }
}

bool ExtensionManager::isInstalled(const QString& id) const {
  return installed_.contains(id);
}

bool ExtensionManager::hasPendingInstall(const QString& id) const {
  if (!invalidExtensionIdReason(id).isEmpty()) {
    return false;
  }
  const QString root = pendingRoot(pending_dir_, id);
  const PendingInstallIntent intent = readPendingInstallIntent(root);
  if (!intent.valid || intent.id != id) {
    return false;
  }
  const DirectoryDiscovery discovered = discoverExtensionDirectory(root);
  return validateRegistryIntent(discovered, intent.id, intent.version).isEmpty();
}

bool ExtensionManager::hasPendingUninstall(const QString& id) const {
  return QFile::exists(extRoot(extensions_dir_, id) + "/" + kPendingUninstallMarker);
}

bool ExtensionManager::hasNewerInstalledVersion(const Extension& ext) const {
  if (!installed_.contains(ext.id)) {
    return false;
  }

  const QVersionNumber installed_ver = QVersionNumber::fromString(installed_[ext.id].version);
  const QVersionNumber registry_ver = QVersionNumber::fromString(ext.version);
  return QVersionNumber::compare(installed_ver, registry_ver) > 0;
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

QList<ExtensionDiagnostic> ExtensionManager::diagnostics() const {
  return diagnostics_;
}

void ExtensionManager::clearDiagnostics() {
  diagnostics_.clear();
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

bool ExtensionManager::schedulePendingUninstall(const QString& path) {
  QFile marker(path + "/" + kPendingUninstallMarker);
  // Content is irrelevant; existence is the signal. Failure here means the next
  // startup's applyPendingUninstalls() will not see the marker and the directory
  // would leak forever — surface it so the caller can fail the uninstall.
  return marker.open(QIODevice::WriteOnly);
}

void ExtensionManager::reportDiagnostic(const QString& id, const QString& message, bool is_error) {
  diagnostics_.append(ExtensionDiagnostic{id, message, is_error, QDateTime::currentDateTimeUtc()});
  while (diagnostics_.size() > kMaxDiagnostics) {
    diagnostics_.removeFirst();
  }
  emit diagnosticReported(id, message, is_error);
  if (sink_) {
    sink_(Diagnostic{
        is_error ? DiagnosticLevel::kError : DiagnosticLevel::kInfo,
        "ExtensionManager",
        id.toStdString(),
        message.toStdString(),
        std::chrono::system_clock::now(),
    });
  }
}

void ExtensionManager::emitInstallFailure(const QString& id, const QString& message) {
  QString diagnostic = message;
  if (!pending_backup_path_.isEmpty()) {
    diagnostic += QString(" Previous version remains in backup: \"%1\".").arg(pending_backup_path_);
  }
  pending_backup_path_.clear();
  reportDiagnostic(id, diagnostic, true);
  emit installError(id, diagnostic);
  emit installFinished(id, false);
}

void ExtensionManager::emitUninstallFailure(const QString& id, const QString& message) {
  reportDiagnostic(id, message, true);
  emit uninstallError(id, message);
  emit uninstallFinished(id, false);
}

void ExtensionManager::registerInstalledExtension(const QString& id, const QString& dst, InstalledExtension record) {
  record.path = dst;
  record.install_date = QFileInfo(dst).lastModified();
  installed_[id] = record;
}

void ExtensionManager::refreshInstalledFromDisk() {
  QMap<QString, InstalledExtension> discovered;
  const QDir dir(extensions_dir_);
  for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
    const QString root = entry.absoluteFilePath();
    if (isTransactionDirectoryName(entry.fileName())) {
      removeDirectoryIfSet(root);
      continue;
    }
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

void ExtensionManager::setInstalledExtensions(QMap<QString, InstalledExtension> installed) {
  installed_ = std::move(installed);
}

}  // namespace PJ
