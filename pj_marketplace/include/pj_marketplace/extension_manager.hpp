#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QMetaObject>
#include <QObject>
#include <QString>

#include "pj_base/diagnostic_sink.hpp"
#include "pj_marketplace/extension.hpp"
#include "pj_marketplace/installed_extension.hpp"
#include "pj_marketplace/platform_utils.hpp"

namespace PJ {

class DownloadManager;

// One user-visible diagnostic emitted by marketplace lifecycle operations.
struct ExtensionDiagnostic {
  QString id;       ///< Registry or embedded plugin id, when known.
  QString message;  ///< Human-readable diagnostic.
  bool is_error = false;
  QDateTime timestamp;  ///< UTC timestamp.
};

// Manages marketplace extension installs, updates, uninstalls, and startup cleanup.
// Installed metadata is derived from embedded DSO manifests, not local sidecars.
class ExtensionManager : public QObject {
  Q_OBJECT

 public:
  // Creates an owned DownloadManager and uses the standard user directories.
  ExtensionManager();

  // Uses the supplied downloader and directories; tests pass isolated temp paths.
  // The optional sink receives the same diagnostics as diagnosticReported() —
  // hosts can subscribe to one stream that also carries non-marketplace events.
  explicit ExtensionManager(
      DownloadManager* downloader, const QString& extensions_dir = PlatformUtils::extensionsDir(),
      const QString& pending_dir = PlatformUtils::pendingDir(), DiagnosticSink sink = {},
      QObject* parent = nullptr);

  // Starts an async install for the current platform.
  void install(const Extension& ext);

  // Removes an installed extension or schedules Windows cleanup after restart.
  void uninstall(const QString& extension_id);

  // Replaces an installed extension, staging on Windows and backing up elsewhere.
  void update(const Extension& ext);

  // Promotes validated staged installs from PlatformUtils::pendingDir().
  void applyPendingInstalls();

  // Deletes extension directories previously marked for restart cleanup.
  void applyPendingUninstalls();

  // Returns true when the latest disk scan found this extension id.
  bool isInstalled(const QString& id) const;

  // Rebuilds installed state by scanning extension directories for plugin DSOs.
  void refreshInstalledFromDisk();

  // Replaces the installed-state cache with a caller-provided snapshot.
  void setInstalledExtensions(QMap<QString, InstalledExtension> installed);

  // Returns true when a staged install has a matching intent and valid DSO.
  bool hasPendingInstall(const QString& id) const;

  // Returns true when an installed directory is marked for restart cleanup.
  bool hasPendingUninstall(const QString& id) const;

  // Returns true when the installed version is newer than the registry version.
  bool hasNewerInstalledVersion(const Extension& ext) const;

  // Compares registry and installed versions using QVersionNumber.
  bool hasUpdate(const Extension& ext) const;

  // Returns the current installed-extension snapshot keyed by id.
  QMap<QString, InstalledExtension> installedExtensions() const;

  // Returns recent lifecycle diagnostics for UI display.
  QList<ExtensionDiagnostic> diagnostics() const;

  // Clears the in-memory diagnostic history.
  void clearDiagnostics();

  // Root directory where extension DSOs are discovered and managed.
  QString extensionsDir() const { return extensions_dir_; }

#ifdef PJ_MARKETPLACE_TESTING
  // Test hook for forcing direct or staged install paths.
  void testDoInstall(const Extension& ext, bool staging, bool allow_existing = false) {
    doInstall(ext, staging, allow_existing);
  }

#endif

 signals:
  // Emitted when an install or update starts.
  void installStarted(const QString& id);

  // Emitted with percentage progress for the active download.
  void installProgress(const QString& id, int percent);

  // Emitted when install or update completes.
  void installFinished(const QString& id, bool success);

  // Human-readable failure detail; followed by installFinished(id, false).
  void installError(const QString& id, const QString& error_message);

  // Emitted on Windows when the extension is staged and will be active after a restart.
  void installPendingRestart(const QString& id);

  // Emitted when uninstall completes.
  void uninstallFinished(const QString& id, bool success);

  // Human-readable uninstall failure detail.
  void uninstallError(const QString& id, const QString& error_message);

  // Emitted when uninstall requires restart cleanup.
  void uninstallPendingRestart(const QString& id);

  // Emitted whenever a diagnostic is appended to diagnostics().
  void diagnosticReported(const QString& id, const QString& message, bool is_error);

 private:
  // Called by both constructors to finish setup after members are assigned.
  void initComponents();

  // Shared install implementation for direct and staged destinations.
  void doInstall(const Extension& ext, bool staging, bool allow_existing = false);

  // Disconnects downloader signals for the current operation.
  void disconnectDlConns();

  // Writes the restart-cleanup marker into an installed extension directory.
  // Returns false if the marker file could not be created — caller must NOT remove
  // the in-memory entry in that case, otherwise the directory will leak.
  bool schedulePendingUninstall(const QString& path);

  // Appends a diagnostic and notifies observers.
  void reportDiagnostic(const QString& id, const QString& message, bool is_error);

  // Emits installError + installFinished(false) and records a diagnostic.
  void emitInstallFailure(const QString& id, const QString& message);

  // Stamps a freshly-promoted directory with its absolute path + mtime and
  // adds it to the installed_ map under `id`. Caller supplies the record
  // already populated from the embedded manifest.
  void registerInstalledExtension(const QString& id, const QString& dst, InstalledExtension record);

  // Emits uninstallError + uninstallFinished(false) and records a diagnostic.
  void emitUninstallFailure(const QString& id, const QString& message);

  DownloadManager* downloader_ = nullptr;
  QString extensions_dir_;
  QString pending_dir_;
  DiagnosticSink sink_;

  QMap<QString, InstalledExtension> installed_;

  // Non-empty while a fetch is running; guards against concurrent install() calls.
  QString pending_id_;
  // ID returned by DownloadManager::fetch(); used to correlate incoming signals.
  int pending_op_id_ = -1;
  // Ensures the disk-space check runs at most once per fetch operation.
  bool disk_space_checked_ = false;
  // Set before calling cancel() to preserve the real reason shown to the user.
  QString cancel_reason_;
  // Transaction directory used by the currently running fetch/extract operation.
  QString pending_extract_dir_;
  // Non-Windows update backup location, used for failure diagnostics.
  QString pending_backup_path_;
  QList<ExtensionDiagnostic> diagnostics_;

  // Stored so we can disconnect cleanly after each operation completes.
  QMetaObject::Connection dl_progress_conn_;
  QMetaObject::Connection dl_finished_conn_;
  QMetaObject::Connection dl_failed_conn_;
  QMetaObject::Connection dl_cancelled_conn_;
};

}  // namespace PJ
