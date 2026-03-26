#pragma once

#include <QMap>
#include <QMetaObject>
#include <QObject>
#include <QString>

#include "pj_marketplace/extension.hpp"
#include "pj_marketplace/installed_extension.hpp"
#include "pj_marketplace/platform_utils.hpp"

namespace PJ {

class DownloadManager;

// Orchestrates the full install/uninstall/update lifecycle for marketplace extensions.
//
// Responsibilities:
//   - Resolves the correct download artifact for the current platform
//   - On Linux: delegates the full pipeline (download + checksum + extraction) to
//     DownloadManager, then registers the extension immediately
//   - On Windows: extracts to a staging directory (.pending/) because in-use DLLs
//     cannot be overwritten; the extension becomes active after the next restart
//   - On Windows uninstall: if the directory cannot be removed (DLL still mapped),
//     schedules it for deletion at the next startup via applyPendingUninstalls()
//   - At startup: applies any pending staged installs via applyPendingInstalls()
//     and deletes any directories deferred from a previous uninstall via applyPendingUninstalls()
//   - Discovers installed extensions by scanning extensions_dir and reading manifest.json
//
// All constructor dependencies are injected, so tests can pass a DownloadManager stub
// and temp directories to exercise the full flow without touching the real filesystem
// or the network.
//
// Only one install/update can run at a time. Calling install() while an operation is
// in progress emits installError() and returns immediately.
class ExtensionManager : public QObject {
  Q_OBJECT

 public:
  // `extensions_dir` and `pending_dir` default to the standard user paths.
  // Pass QTemporaryDir paths in tests to get a clean, isolated state.
  explicit ExtensionManager(
      DownloadManager* downloader,
      const QString& extensions_dir = PlatformUtils::extensionsDir(),
      const QString& pending_dir = PlatformUtils::pendingDir(),
      QObject* parent = nullptr);

  // Starts an async install of `ext` for the running platform.
  // Emits installStarted() synchronously before the download begins.
  // No-op (emits installError) if another install is already in progress or if
  // the extension is already installed — use update() to upgrade.
  void install(const Extension& ext);

  // Synchronously deletes <extensions_dir>/<id>/ and removes the entry from
  // memory. Emits uninstallFinished(id, false) if the directory cannot
  // be removed (e.g. a DLL is still loaded on Windows — F-14 staging is deferred).
  void uninstall(const QString& extension_id);

  // Moves the current version to ~/.plotjuggler/.backup/<id>-<version>/ and
  // re-installs from the registry. If the rename fails (cross-device or DLL locked
  // on Windows) the old directory is deleted instead so the install gets a clean target.
  // On success the backup path is recorded in installed.json so that future automatic
  // rollback (F-13, April+) can find it.
  void update(const Extension& ext);

  // Moves any staged extensions from .pending/ into extensions/ and registers them.
  // Should be called once at application startup. On Linux this is always a no-op
  // because staging is never used, but it is safe to call on any platform.
  void applyPendingInstalls();

  // Deletes any extension directories that could not be removed during a previous
  // uninstall() because their DLL was still loaded (Windows only).
  // Should be called once at application startup. Safe to call on any platform.
  void applyPendingUninstalls();

  bool isInstalled(const QString& id) const;

  // Compares the registry version against the installed one using QVersionNumber,
  // which handles multi-segment semver correctly ("1.10.0" > "1.9.0").
  // Returns false if the extension is not installed.
  bool hasUpdate(const Extension& ext) const;

  // Snapshot of the currently installed extensions, keyed by id.
  QMap<QString, InstalledExtension> installedExtensions() const;

 signals:
  void installStarted(const QString& id);
  void installProgress(const QString& id, int percent);
  void installFinished(const QString& id, bool success);
  // Human-readable description of what went wrong; always followed by installFinished(id, false).
  void installError(const QString& id, const QString& error_message);
  // Emitted on Windows when the extension is staged and will be active after a restart.
  void installPendingRestart(const QString& id);

  void uninstallFinished(const QString& id, bool success);
  void uninstallError(const QString& id, const QString& error_message);
  // Emitted on Windows when the extension is deregistered but its directory could not
  // be removed (DLL still loaded). The directory will be deleted on the next startup
  // via applyPendingUninstalls().
  void uninstallPendingRestart(const QString& id);

 private:
  void loadState();
  void saveState();
  void disconnectDlConns();
  void savePendingMeta(const Extension& ext);
  void schedulePendingUninstall(const QString& path);

  DownloadManager* downloader_;
  QString extensions_dir_;
  QString pending_dir_;

  QMap<QString, InstalledExtension> installed_;

  // Non-empty while a fetch is running; guards against concurrent install() calls.
  QString pending_id_;
  // ID returned by DownloadManager::fetch(); used to correlate incoming signals.
  int pending_op_id_ = -1;
  // Ensures the disk-space check runs at most once per fetch operation.
  bool disk_space_checked_ = false;
  // Set before calling cancel() to preserve the real reason shown to the user.
  QString cancel_reason_;

  // Stored so we can disconnect cleanly after each operation completes.
  QMetaObject::Connection dl_progress_conn_;
  QMetaObject::Connection dl_finished_conn_;
  QMetaObject::Connection dl_failed_conn_;
  QMetaObject::Connection dl_cancelled_conn_;
};

}  // namespace PJ
