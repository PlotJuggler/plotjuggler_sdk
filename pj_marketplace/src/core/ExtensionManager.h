#pragma once

#include <QMap>
#include <QMetaObject>
#include <QObject>
#include <QString>

#include "core/PlatformUtils.h"
#include "models/Extension.h"
#include "models/InstalledExtension.h"

namespace PJ {

class DownloadManager;
class ZipExtractor;

// Orchestrates the full install/uninstall/update lifecycle for marketplace extensions.
//
// Responsibilities:
//   - Resolves the correct download artifact for the current platform
//   - Delegates the HTTP download to DownloadManager
//   - Verifies the SHA-256 checksum before touching disk
//   - Delegates ZIP extraction to ZipExtractor
//   - Persists installation state to <extensions_dir>/installed.json
//
// All constructor dependencies are injected, so tests can pass a DownloadManager stub
// and a temp directory to exercise the full flow without touching the real filesystem
// or the network.
//
// Only one install/update can run at a time. Calling install() while a download is
// in progress emits installError() and returns immediately.
class ExtensionManager : public QObject {
  Q_OBJECT

 public:
  // `extensions_dir` defaults to the standard ~/.plotjuggler/extensions/ path.
  // Pass a QTemporaryDir path in tests to get a clean, isolated state file.
  explicit ExtensionManager(
      DownloadManager* downloader, ZipExtractor* extractor,
      const QString& extensions_dir = PlatformUtils::extensionsDir(), QObject* parent = nullptr);

  // Starts an async install of `ext` for the running platform.
  // Emits installStarted() synchronously before the download begins.
  // No-op (emits installError) if another install is already in progress.
  void install(const Extension& ext);

  // Synchronously deletes <extensions_dir>/<id>/ and removes the entry from
  // installed.json. Emits uninstallFinished(id, false) if the directory cannot
  // be removed (e.g. a DLL is still loaded on Windows — F-14 staging is deferred).
  void uninstall(const QString& extension_id);

  // Removes the current installation files and re-installs from the registry.
  // F-12 (backup before update) is deferred to April+ per PLAN.md, so the old
  // directory is deleted before the new download begins.
  void update(const Extension& ext);

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

  void uninstallFinished(const QString& id, bool success);
  void uninstallError(const QString& id, const QString& error_message);

 private:
  void loadState();
  void saveState();

  // Called from the DownloadManager::downloadFinished handler.
  // Runs the post-download pipeline: checksum → mkdir → extract → register.
  void finalize_install(
      const Extension& ext, const QString& zip_path, const QString& expected_checksum, bool download_ok,
      const QString& download_error);

  DownloadManager* downloader_;
  ZipExtractor* extractor_;
  QString extensions_dir_;

  QMap<QString, InstalledExtension> installed_;

  // Non-empty while a download is running; guards against concurrent install() calls.
  QString pending_id_;

  // Stored so we can disconnect cleanly after each install completes.
  QMetaObject::Connection dl_progress_conn_;
  QMetaObject::Connection dl_finished_conn_;
};

}  // namespace PJ
