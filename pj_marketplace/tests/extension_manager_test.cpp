// Tests for PJ::ExtensionManager
//
// Coverage:
//   [1] Install (Linux direct path): download + extract + register + state persisted
//   [2] Install guard conditions: already installed, concurrent, unsupported platform
//   [3] Uninstall: directory removed and state updated; errors on unknown id
//   [4] Update: removes old files and re-installs cleanly
//   [5] hasUpdate: multi-segment semver comparison using registry fixture data
//   [6] applyPendingInstalls: simulates the Windows post-restart staging path
//   [7] State persistence: installed.json survives across manager restarts
//   [8] Platform detection: currentPlatform() format and registry key resolution

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>

#include <archive.h>
#include <archive_entry.h>

#include "core/DownloadManager.h"
#include "core/ExtensionManager.h"
#include "core/PlatformUtils.h"
#include "models/Extension.h"

namespace PJ {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Spins the Qt event loop until spy receives at least one signal or the deadline expires.
bool wait_for_signal(QSignalSpy& spy, int timeout_ms = 5000) {
  QDeadlineTimer deadline(timeout_ms);
  while (spy.isEmpty() && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  return !spy.isEmpty();
}

// Minimal HTTP/1.1 server that answers every request with a fixed in-memory body.
// Binds to a random loopback port; no external network required.
class LocalHttpServer {
 public:
  LocalHttpServer() {
    server_.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&server_, &QTcpServer::newConnection, [this]() {
      QTcpSocket* socket = server_.nextPendingConnection();
      socket->setParent(&server_);
      QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        socket->readAll();  // discard the HTTP request — content is irrelevant for tests
        const QByteArray header = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: application/octet-stream\r\n"
                                  "Content-Length: " +
                                  QByteArray::number(body_.size()) +
                                  "\r\n"
                                  "Connection: close\r\n\r\n";
        socket->write(header + body_);
        socket->flush();
        socket->disconnectFromHost();
      });
    });
  }

  QUrl url() const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server_.serverPort()));
  }

  void set_body(const QByteArray& body) { body_ = body; }

 private:
  QTcpServer server_;
  QByteArray body_;
};

// Builds an in-memory ZIP archive from a map of { relative_path -> file_content }.
QByteArray build_zip(const QMap<QString, QByteArray>& files) {
  std::vector<char> buf(4 * 1024 * 1024);
  size_t used = 0;

  auto* a = archive_write_new();
  archive_write_set_format_zip(a);
  archive_write_add_filter_none(a);
  archive_write_open_memory(a, buf.data(), buf.size(), &used);

  auto* entry = archive_entry_new();
  for (auto it = files.cbegin(); it != files.cend(); ++it) {
    archive_entry_clear(entry);
    archive_entry_set_pathname(entry, it.key().toUtf8().constData());
    archive_entry_set_size(entry, it.value().size());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_write_header(a, entry);
    archive_write_data(a, it.value().constData(), static_cast<size_t>(it.value().size()));
  }
  archive_entry_free(entry);
  archive_write_close(a);
  archive_write_free(a);

  return QByteArray(buf.data(), static_cast<int>(used));
}

// Returns a minimal single-file ZIP that looks like a real plugin package.
QByteArray dummy_plugin_zip(const QString& ext_id) {
  return build_zip({{ext_id + ".plugin", "placeholder binary content"}});
}

// Builds an Extension whose download artifact for the current platform points to `url`.
// Checksum is empty by default so DownloadManager skips SHA-256 verification.
Extension make_extension(const QString& id, const QString& version, const QUrl& url,
                         const QString& checksum = {}) {
  Extension ext;
  ext.id = id;
  ext.name = id;
  ext.version = version;

  Platform p;
  p.url = url.toString();
  p.checksum = checksum;
  ext.platforms[PlatformUtils::currentPlatform()] = p;
  return ext;
}

// ---------------------------------------------------------------------------
// Test fixture — isolated temp directories and a fresh manager per test
// ---------------------------------------------------------------------------

class ExtensionManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(ext_dir_.isValid());
    ASSERT_TRUE(pending_dir_.isValid());
    downloader_ = new DownloadManager();
    mgr_ = new ExtensionManager(downloader_, ext_dir_.path(), pending_dir_.path());
  }

  void TearDown() override {
    delete mgr_;
    delete downloader_;
  }

  QTemporaryDir ext_dir_;
  QTemporaryDir pending_dir_;
  LocalHttpServer server_;
  DownloadManager* downloader_ = nullptr;
  ExtensionManager* mgr_ = nullptr;
};

// ---------------------------------------------------------------------------
// [1] Direct install (Linux path)
// ---------------------------------------------------------------------------

// A fresh install downloads the ZIP, extracts it, registers the extension, and
// emits the correct signal sequence: installStarted → installFinished(id, true).
TEST_F(ExtensionManagerTest, InstallDirectRegistersExtension) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy_started(mgr_, &ExtensionManager::installStarted);
  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext);

  // installStarted must be synchronous — no event loop needed.
  ASSERT_EQ(spy_started.count(), 1);
  EXPECT_EQ(spy_started.first().at(0).toString(), "csv-loader");

  ASSERT_TRUE(wait_for_signal(spy_finished)) << "installFinished not received within 5 s";
  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "csv-loader");
  EXPECT_TRUE(spy_finished.first().at(1).toBool()) << "install must succeed";
  EXPECT_TRUE(spy_error.isEmpty());

  EXPECT_TRUE(mgr_->isInstalled("csv-loader"));
  EXPECT_EQ(mgr_->installedExtensions()["csv-loader"].version, "1.0.0");
}

// The extracted content lands under extensions_dir/<id>/ after a successful install.
TEST_F(ExtensionManagerTest, InstallCreatesExtensionDirectory) {
  server_.set_body(dummy_plugin_zip("can-bus-parser"));
  const Extension ext = make_extension("can-bus-parser", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(wait_for_signal(spy));
  ASSERT_TRUE(spy.first().at(1).toBool());

  EXPECT_TRUE(QDir(ext_dir_.path() + "/can-bus-parser").exists());
}

// installProgress signals are forwarded during the download phase.
// Each signal must carry the correct extension id and a percent in [0, 100].
TEST_F(ExtensionManagerTest, InstallEmitsProgressSignals) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy_progress(mgr_, &ExtensionManager::installProgress);
  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);

  mgr_->install(ext);
  ASSERT_TRUE(wait_for_signal(spy_finished));

  EXPECT_GE(spy_progress.count(), 1);
  for (const QList<QVariant>& args : spy_progress) {
    EXPECT_EQ(args.at(0).toString(), "csv-loader");
    const int pct = args.at(1).toInt();
    EXPECT_GE(pct, 0);
    EXPECT_LE(pct, 100);
  }
}

// ---------------------------------------------------------------------------
// [2] Install guard conditions
// ---------------------------------------------------------------------------

// Calling install() for an extension that is already installed must emit installError
// immediately — it must not start a new download.
TEST_F(ExtensionManagerTest, InstallRejectsAlreadyInstalledExtension) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext = make_extension("csv-loader", "1.0.0", server_.url());

  // First install — must succeed.
  QSignalSpy spy_first(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(wait_for_signal(spy_first));
  ASSERT_TRUE(spy_first.first().at(1).toBool());

  // Second install — must be rejected with an error.
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  mgr_->install(ext);
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_EQ(spy_error.first().at(0).toString(), "csv-loader");
  EXPECT_FALSE(spy_error.first().at(1).toString().isEmpty());
}

// Calling install() for a second extension while one is already in progress must
// reject the second request immediately via installError.
TEST_F(ExtensionManagerTest, InstallBlocksConcurrentRequests) {
  // A server that accepts TCP connections but never sends any data keeps the first
  // download pending indefinitely without burning CPU or requiring a timeout.
  QTcpServer hanging_server;
  hanging_server.listen(QHostAddress::LocalHost, 0);
  const QUrl hanging_url =
      QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(hanging_server.serverPort()));

  const Extension ext_a = make_extension("csv-loader", "1.0.0", hanging_url);
  const Extension ext_b = make_extension("can-bus-parser", "1.0.0", hanging_url);

  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext_a);  // begins — will hang until TearDown cleans up
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  mgr_->install(ext_b);  // must be rejected immediately; pending_id_ is already set

  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_EQ(spy_error.first().at(0).toString(), "can-bus-parser");
  EXPECT_FALSE(spy_error.first().at(1).toString().isEmpty());
}

// If the Extension's platforms map does not contain the current platform, install()
// must emit installError without initiating any download.
TEST_F(ExtensionManagerTest, InstallRejectsUnsupportedPlatform) {
  Extension ext;
  ext.id = "fft-toolbox";
  ext.name = "FFT Toolbox";
  ext.version = "1.0.0";
  // Only register an artifact for a platform we will never run on.
  ext.platforms["nonexistent-platform"] = Platform{"http://example.com/dummy.zip", ""};

  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  QSignalSpy spy_started(mgr_, &ExtensionManager::installStarted);

  mgr_->install(ext);

  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_EQ(spy_error.first().at(0).toString(), "fft-toolbox");
  EXPECT_FALSE(spy_error.first().at(1).toString().isEmpty());
  EXPECT_EQ(spy_started.count(), 0) << "installStarted must not fire when the platform is unsupported";
}

// ---------------------------------------------------------------------------
// [3] Uninstall
// ---------------------------------------------------------------------------

// A successful uninstall removes the extension directory and updates installed.json.
TEST_F(ExtensionManagerTest, UninstallRemovesDirectoryAndState) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy_install(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(wait_for_signal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  const QString ext_path = ext_dir_.path() + "/csv-loader";
  ASSERT_TRUE(QDir(ext_path).exists());

  QSignalSpy spy_uninstall(mgr_, &ExtensionManager::uninstallFinished);
  mgr_->uninstall("csv-loader");

  ASSERT_EQ(spy_uninstall.count(), 1);
  EXPECT_EQ(spy_uninstall.first().at(0).toString(), "csv-loader");
  EXPECT_TRUE(spy_uninstall.first().at(1).toBool());

  EXPECT_FALSE(mgr_->isInstalled("csv-loader"));
  EXPECT_FALSE(QDir(ext_path).exists());
}

// Attempting to uninstall an extension that was never installed must emit
// uninstallError and uninstallFinished(id, false) without touching the filesystem.
TEST_F(ExtensionManagerTest, UninstallUnknownExtensionEmitsError) {
  QSignalSpy spy_error(mgr_, &ExtensionManager::uninstallError);
  QSignalSpy spy_finished(mgr_, &ExtensionManager::uninstallFinished);

  mgr_->uninstall("nonexistent-extension");

  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_EQ(spy_error.first().at(0).toString(), "nonexistent-extension");
  EXPECT_FALSE(spy_error.first().at(1).toString().isEmpty());

  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
}

// ---------------------------------------------------------------------------
// [4] Update
// ---------------------------------------------------------------------------

// update() backs up the current version and re-installs from the registry.
// The new version is registered with the correct version string after completion.
TEST_F(ExtensionManagerTest, UpdateReinstallsWithNewVersion) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext_v1 = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy_install(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_v1);
  ASSERT_TRUE(wait_for_signal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  // Prepare a "new version" and trigger the update.
  spy_install.clear();
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext_v2 = make_extension("csv-loader", "2.0.0", server_.url());
  mgr_->update(ext_v2);

  ASSERT_TRUE(wait_for_signal(spy_install));
  EXPECT_TRUE(spy_install.first().at(1).toBool());
  EXPECT_EQ(mgr_->installedExtensions()["csv-loader"].version, "2.0.0");
}

// After a successful update the old version directory must exist in backupDir()
// and the new installed record must carry the backup_path.
//
// ext_dir is placed under the same filesystem root as backupDir() (~/.plotjuggler/)
// so that QDir::rename() can do an atomic move without a cross-device copy.
TEST_F(ExtensionManagerTest, UpdateBacksUpOldVersionOnSuccess) {
  // Place the extension directory on the same filesystem as backupDir().
  QTemporaryDir local_ext_dir(QDir(PlatformUtils::backupDir()).absoluteFilePath("../test_ext_XXXXXX"));
  ASSERT_TRUE(local_ext_dir.isValid());

  DownloadManager local_dl;
  ExtensionManager local_mgr(&local_dl, local_ext_dir.path(), pending_dir_.path());

  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext_v1 = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy_install(&local_mgr, &ExtensionManager::installFinished);
  local_mgr.install(ext_v1);
  ASSERT_TRUE(wait_for_signal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  ASSERT_TRUE(QFile::exists(local_ext_dir.path() + "/csv-loader/csv-loader.plugin"));

  spy_install.clear();
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext_v2 = make_extension("csv-loader", "2.0.0", server_.url());
  local_mgr.update(ext_v2);

  ASSERT_TRUE(wait_for_signal(spy_install));
  EXPECT_TRUE(spy_install.first().at(1).toBool()) << "update must succeed";
  EXPECT_EQ(local_mgr.installedExtensions()["csv-loader"].version, "2.0.0");

  const QString backup_path = PlatformUtils::backupDir() + "/csv-loader-1.0.0";
  EXPECT_TRUE(QDir(backup_path).exists()) << "backup directory must exist after update";
  EXPECT_TRUE(QFile::exists(backup_path + "/csv-loader.plugin"))
      << "original plugin file must be preserved in backup";
  EXPECT_EQ(local_mgr.installedExtensions()["csv-loader"].backup_path, backup_path);

  QDir(backup_path).removeRecursively();
}

// When the install step fails after the backup, the old version files must still
// be recoverable from backupDir() — no data is permanently lost.
//
// ext_dir is placed under the same filesystem root as backupDir() (~/.plotjuggler/)
// so that QDir::rename() can do an atomic move without a cross-device copy.
TEST_F(ExtensionManagerTest, UpdateKeepsBackupWhenInstallFails) {
  QTemporaryDir local_ext_dir(QDir(PlatformUtils::backupDir()).absoluteFilePath("../test_ext_XXXXXX"));
  ASSERT_TRUE(local_ext_dir.isValid());

  DownloadManager local_dl;
  ExtensionManager local_mgr(&local_dl, local_ext_dir.path(), pending_dir_.path());

  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext_v1 = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy_install(&local_mgr, &ExtensionManager::installFinished);
  local_mgr.install(ext_v1);
  ASSERT_TRUE(wait_for_signal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  spy_install.clear();
  // Serve garbage data — libarchive will fail to extract it and DownloadManager
  // will emit failed(), which propagates to installFinished(id, false).
  server_.set_body(QByteArray("not_a_valid_zip"));
  const Extension ext_v2 = make_extension("csv-loader", "2.0.0", server_.url());

  QSignalSpy spy_error(&local_mgr, &ExtensionManager::installError);
  local_mgr.update(ext_v2);

  ASSERT_TRUE(wait_for_signal(spy_install)) << "installFinished must fire even on failure";
  EXPECT_FALSE(spy_install.first().at(1).toBool()) << "install must have failed";
  EXPECT_FALSE(spy_error.isEmpty()) << "installError must be emitted on failure";

  EXPECT_FALSE(local_mgr.isInstalled("csv-loader"));

  const QString backup_path = PlatformUtils::backupDir() + "/csv-loader-1.0.0";
  EXPECT_TRUE(QDir(backup_path).exists())
      << "backup must survive a failed install — files are recoverable";
  EXPECT_TRUE(QFile::exists(backup_path + "/csv-loader.plugin"))
      << "original plugin binary must be preserved in backup";

  QDir(backup_path).removeRecursively();
}

// ---------------------------------------------------------------------------
// [5] hasUpdate — version comparison
//
// Extension data below mirrors the registry.json fixture:
//   csv-loader   v1.0.0
//   can-bus-parser v1.0.0
// ---------------------------------------------------------------------------

// Returns false when the extension is not installed.
TEST_F(ExtensionManagerTest, HasUpdateReturnsFalseWhenNotInstalled) {
  Extension ext;
  ext.id = "csv-loader";
  ext.version = "1.0.0";
  EXPECT_FALSE(mgr_->hasUpdate(ext));
}

// Returns false when the installed and registry versions are identical.
TEST_F(ExtensionManagerTest, HasUpdateReturnsFalseForSameVersion) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(wait_for_signal(spy));

  EXPECT_FALSE(mgr_->hasUpdate(ext));
}

// Returns true when the registry version is strictly higher than the installed one.
TEST_F(ExtensionManagerTest, HasUpdateReturnsTrueForNewerVersion) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext_v1 = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_v1);
  ASSERT_TRUE(wait_for_signal(spy));

  Extension ext_v2 = ext_v1;
  ext_v2.version = "2.0.0";
  EXPECT_TRUE(mgr_->hasUpdate(ext_v2));
}

// QVersionNumber must compare multi-segment versions numerically, not lexically:
// "1.10.0" > "1.9.0" — a raw string compare would invert this result.
TEST_F(ExtensionManagerTest, HasUpdateHandlesMultiSegmentVersionsCorrectly) {
  server_.set_body(dummy_plugin_zip("can-bus-parser"));
  const Extension ext_installed = make_extension("can-bus-parser", "1.9.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_installed);
  ASSERT_TRUE(wait_for_signal(spy));

  // "1.10.0" is numerically greater but lexically smaller than "1.9.0".
  Extension ext_registry = ext_installed;
  ext_registry.version = "1.10.0";
  EXPECT_TRUE(mgr_->hasUpdate(ext_registry));
}

// Returns false when the registry version is older than the installed one (downgrade scenario).
TEST_F(ExtensionManagerTest, HasUpdateReturnsFalseForOlderVersion) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext_v2 = make_extension("csv-loader", "2.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_v2);
  ASSERT_TRUE(wait_for_signal(spy));

  Extension ext_v1 = ext_v2;
  ext_v1.version = "1.0.0";
  EXPECT_FALSE(mgr_->hasUpdate(ext_v1));
}

// ---------------------------------------------------------------------------
// [6] applyPendingInstalls — Windows post-restart staging simulation
//
// On Windows, DLLs in use cannot be overwritten, so install() extracts to
// .pending/<id>/ instead and saves a pj_meta.json metadata file. On the next
// startup, applyPendingInstalls() moves the directory into extensions/ and
// registers it. These tests create that directory structure manually and verify
// the promotion logic on any platform (the function is always safe to call).
// ---------------------------------------------------------------------------

// applyPendingInstalls() promotes a staged extension to extensions/, registers it,
// emits installFinished(id, true), and removes the pj_meta.json staging artifact.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsPromotesStagedExtension) {
  // Replicate what save_pending_meta() and DownloadManager::fetch() produce on Windows.
  const QString staged_dir = pending_dir_.path() + "/mcap-loader";
  ASSERT_TRUE(QDir().mkpath(staged_dir));

  QFile plugin_file(staged_dir + "/mcap-loader.plugin");
  ASSERT_TRUE(plugin_file.open(QIODevice::WriteOnly));
  plugin_file.write("placeholder binary");
  plugin_file.close();

  QJsonObject meta;
  meta["id"] = "mcap-loader";
  meta["version"] = "1.0.0";
  meta["install_date"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  QFile meta_file(staged_dir + "/pj_meta.json");
  ASSERT_TRUE(meta_file.open(QIODevice::WriteOnly));
  meta_file.write(QJsonDocument(meta).toJson());
  meta_file.close();

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  mgr_->applyPendingInstalls();

  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "mcap-loader");
  EXPECT_TRUE(spy_finished.first().at(1).toBool());

  // Extension must be queryable as installed.
  EXPECT_TRUE(mgr_->isInstalled("mcap-loader"));
  EXPECT_EQ(mgr_->installedExtensions()["mcap-loader"].version, "1.0.0");

  // The active directory lives under extensions_dir, not pending_dir.
  EXPECT_TRUE(QDir(ext_dir_.path() + "/mcap-loader").exists());
  EXPECT_FALSE(QDir(staged_dir).exists());

  // pj_meta.json must be cleaned up so it does not pollute the active install.
  EXPECT_FALSE(QFile::exists(ext_dir_.path() + "/mcap-loader/pj_meta.json"));
}

// An entry in .pending/ that lacks pj_meta.json is silently skipped — it may be
// a leftover from an incomplete extraction and must not cause a crash or bad state.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsSkipsDirectoryWithoutMetaFile) {
  const QString staged_dir = pending_dir_.path() + "/bad-extension";
  ASSERT_TRUE(QDir().mkpath(staged_dir));
  // Intentionally omit pj_meta.json to simulate a broken staging directory.

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->applyPendingInstalls();

  EXPECT_EQ(spy.count(), 0);
  EXPECT_FALSE(mgr_->isInstalled("bad-extension"));
}

// applyPendingInstalls() is a no-op when the pending directory contains no sub-directories.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsIsNoOpForEmptyDirectory) {
  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->applyPendingInstalls();
  EXPECT_EQ(spy.count(), 0);
}

// Multiple staged extensions in .pending/ are all promoted in a single call.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsPromotesMultipleExtensions) {
  for (const QString& id : QStringList{"csv-loader", "can-bus-parser"}) {
    const QString staged = pending_dir_.path() + "/" + id;
    ASSERT_TRUE(QDir().mkpath(staged));

    QJsonObject meta;
    meta["id"] = id;
    meta["version"] = "1.0.0";
    meta["install_date"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QFile f(staged + "/pj_meta.json");
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(meta).toJson());
  }

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->applyPendingInstalls();

  EXPECT_EQ(spy.count(), 2);
  EXPECT_TRUE(mgr_->isInstalled("csv-loader"));
  EXPECT_TRUE(mgr_->isInstalled("can-bus-parser"));
}

// ---------------------------------------------------------------------------
// [7] State persistence
// ---------------------------------------------------------------------------

// installed.json must be readable by a new ExtensionManager instance pointing to the
// same directory — this simulates an application restart.
TEST_F(ExtensionManagerTest, StatePersistsAcrossManagerRestarts) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(wait_for_signal(spy));
  ASSERT_TRUE(spy.first().at(1).toBool());

  // Simulate restart: a brand-new manager reads the same extensions_dir.
  DownloadManager downloader2;
  ExtensionManager mgr2(&downloader2, ext_dir_.path(), pending_dir_.path());

  EXPECT_TRUE(mgr2.isInstalled("csv-loader"));
  EXPECT_EQ(mgr2.installedExtensions()["csv-loader"].version, "1.0.0");
}

// Uninstalling removes the record from installed.json; a fresh manager must not
// report the extension as installed.
TEST_F(ExtensionManagerTest, UninstallRemovesEntryFromPersistentState) {
  server_.set_body(dummy_plugin_zip("csv-loader"));
  const Extension ext = make_extension("csv-loader", "1.0.0", server_.url());

  QSignalSpy spy_install(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(wait_for_signal(spy_install));

  mgr_->uninstall("csv-loader");

  DownloadManager downloader2;
  ExtensionManager mgr2(&downloader2, ext_dir_.path(), pending_dir_.path());
  EXPECT_FALSE(mgr2.isInstalled("csv-loader"));
}

// A new manager that finds no installed.json starts with an empty extension list —
// no crash or undefined behaviour on first run.
TEST_F(ExtensionManagerTest, FreshManagerHasNoInstalledExtensions) {
  EXPECT_TRUE(mgr_->installedExtensions().isEmpty());
}

// ---------------------------------------------------------------------------
// [8] Platform detection
// ---------------------------------------------------------------------------

// currentPlatform() must return a non-empty string in "<os>-<arch>" format.
TEST(PlatformDetectionTest, CurrentPlatformHasExpectedFormat) {
  const QString platform = PlatformUtils::currentPlatform();
  EXPECT_FALSE(platform.isEmpty());
  EXPECT_TRUE(platform.contains('-'))
      << "Expected '<os>-<arch>' format, got: " << platform.toStdString();
}

// On the primary Linux x86_64 build/CI host, the reported platform must match the
// key used in the registry fixture so that install() can resolve the download artifact.
TEST(PlatformDetectionTest, LinuxX86PlatformMatchesRegistryKey) {
  EXPECT_EQ(PlatformUtils::currentPlatform(), "linux-x86_64");
}

// The csv-loader entry from pj-plugin-registry/registry.json must be resolvable on
// the running host — isWindows() is the gate used by install() to choose the staging path.
TEST(PlatformDetectionTest, CurrentPlatformResolvesRegistryArtifact) {
  // Mirrors the csv-loader entry from pj-plugin-registry/registry.json verbatim.
  Extension ext;
  ext.id = "csv-loader";
  ext.version = "1.0.0";
  ext.platforms["linux-x86_64"] = {
      "https://cloud.ibrobotics.com/public.php/dav/files/9xBz6zdDn5WYJ6c/?accept=zip",
      "sha256:324e8016b38bce3365d4f4b71035eb8e6518445e06a599f9dd2d7e2ecbc50c02"};
  ext.platforms["windows-x86_64"] = {
      "https://cloud.ibrobotics.com/public.php/dav/files/aQgzSYywW2onrB3/?accept=zip",
      "sha256:324e8016b38bce3365d4f4b71035eb8e6518445e06a599f9dd2d7e2ecbc50c02"};

  EXPECT_TRUE(ext.platforms.contains(PlatformUtils::currentPlatform()))
      << "Platform '" << PlatformUtils::currentPlatform().toStdString()
      << "' is not listed in the csv-loader registry entry";
}

// On Linux, install() must write directly to extensions_dir (no staging).
// isWindows() must return false to confirm the code path is exercised.
TEST(PlatformDetectionTest, IsWindowsReturnsFalseOnLinux) {
  EXPECT_FALSE(PlatformUtils::isWindows());
}

}  // namespace
}  // namespace PJ

// ---------------------------------------------------------------------------
// main — QCoreApplication is required for the Qt network event loop
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
