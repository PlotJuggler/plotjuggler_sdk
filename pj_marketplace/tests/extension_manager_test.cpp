// Tests for PJ::ExtensionManager
//
// Coverage:
//   [1] Install (Linux direct path): download + extract + register + state persisted
//   [2] Install guard conditions: already installed, concurrent, unsupported platform
//   [3] Uninstall: directory removed and state updated; errors on unknown id
//   [4] Update: removes old files and re-installs cleanly
//   [5] hasUpdate: multi-segment semver comparison using registry fixture data
//   [6] applyPendingInstalls: simulates the Windows post-restart staging path
//   [7] State persistence: installed state derived from disk across manager restarts
//   [8] Platform detection: currentPlatform() format and registry key resolution
//   [9] applyPendingUninstalls: deferred directory cleanup via marker file

#include "pj_marketplace/extension_manager.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>

#include "pj_marketplace/download_manager.hpp"
#include "pj_marketplace/extension.hpp"
#include "pj_marketplace/platform_utils.hpp"

namespace PJ {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Spins the Qt event loop until spy receives at least one signal or the deadline expires.
bool waitForSignal(QSignalSpy& spy, int timeout_ms = 5000) {
  QDeadlineTimer deadline(timeout_ms);
  while (spy.isEmpty() && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  return !spy.isEmpty();
}

bool waitForInstallOutcome(QSignalSpy& finished, QSignalSpy& pending_restart, int timeout_ms = 5000) {
  QDeadlineTimer deadline(timeout_ms);
  while (finished.isEmpty() && pending_restart.isEmpty() && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  return !finished.isEmpty() || !pending_restart.isEmpty();
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
        const QByteArray header =
            "HTTP/1.1 200 OK\r\n"
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

  void setBody(const QByteArray& body) {
    body_ = body;
  }

 private:
  QTcpServer server_;
  QByteArray body_;
};

// Builds an in-memory ZIP archive from a map of { relative_path -> file_content }.
QByteArray buildZip(const QMap<QString, QByteArray>& files) {
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

QByteArray readAll(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QString pluginPathForId(const QString& ext_id, const QString& version = "1.0.0") {
  if (ext_id == "mock-data-source" && version == "2.0.0") {
    return QStringLiteral(PJ_MOCK_DATA_SOURCE_V2_PLUGIN_PATH);
  }
  if (ext_id == "mock-data-source") {
    return QStringLiteral(PJ_MOCK_DATA_SOURCE_PLUGIN_PATH);
  }
  if (ext_id == "mock-file-source") {
    return QStringLiteral(PJ_MOCK_FILE_SOURCE_PLUGIN_PATH);
  }
  if (ext_id == "missing-id-source") {
    return QStringLiteral(PJ_MISSING_ID_PLUGIN_PATH);
  }
  return {};
}

QString pluginFileName() {
  return "plugin" + QString::fromStdString(PlatformUtils::pluginExtension());
}

bool writePendingIntentForTest(const QString& staged_dir, const QString& id, const QString& version = "1.0.0") {
  QFile intent(QDir(staged_dir).absoluteFilePath(".pj_pending_install"));
  if (!intent.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const QByteArray data = id.toUtf8() + '\n' + version.toUtf8() + '\n';
  return intent.write(data) == data.size();
}

bool copyFixturePlugin(const QString& dst_dir, const QString& fixture_id, const QString& version = "1.0.0") {
  QDir().mkpath(dst_dir);
  QFile::remove(QDir(dst_dir).absoluteFilePath(pluginFileName()));
  return QFile::copy(pluginPathForId(fixture_id, version), QDir(dst_dir).absoluteFilePath(pluginFileName()));
}

bool directoryHasNoChildren(const QString& path) {
  const QFileInfoList entries =
      QDir(path).entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  return entries.isEmpty();
}

QByteArray pluginZipWithDso(const QString& ext_id, const QString& fixture_id, const QString& version = "1.0.0") {
  const QByteArray plugin = readAll(pluginPathForId(fixture_id, version));
  if (plugin.isEmpty()) {
    return buildZip({{ext_id + "/not-a-plugin.txt", "missing fixture plugin"}});
  }
  return buildZip({
      {ext_id + "/" + pluginFileName(), plugin},
  });
}

// Returns a ZIP with an <id>/ root directory and a real plugin DSO whose
// embedded manifest is the installed-state source of truth. There is no local
// metadata sidecar.
QByteArray dummyPluginZip(const QString& ext_id, const QString& version = "1.0.0") {
  return pluginZipWithDso(ext_id, ext_id, version);
}

QByteArray pluginZipWithTwoDsos(const QString& ext_id, const QString& first_id, const QString& second_id) {
  const QString suffix = QString::fromStdString(PlatformUtils::pluginExtension());
  return buildZip({
      {ext_id + "/first" + suffix, readAll(pluginPathForId(first_id))},
      {ext_id + "/second" + suffix, readAll(pluginPathForId(second_id))},
  });
}

// Builds an Extension whose download artifact for the current platform points to `url`.
// Checksum is empty by default so DownloadManager skips SHA-256 verification.
Extension makeExtension(const QString& id, const QString& version, const QUrl& url, const QString& checksum = {}) {
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
    // Several tests place their own QTemporaryDir under PlatformUtils::configDir()
    // (next to backupDir()) so QDir::rename() into the backup is an atomic same-fs
    // move. QTemporaryDir refuses to create itself when the parent does not exist,
    // which is exactly the state of a fresh CI runner. Pre-create configDir() here
    // so those constructions succeed regardless of host history.
    ASSERT_TRUE(QDir().mkpath(PlatformUtils::configDir()));
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
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_started(mgr_, &ExtensionManager::installStarted);
  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext);

  // installStarted must be synchronous — no event loop needed.
  ASSERT_EQ(spy_started.count(), 1);
  EXPECT_EQ(spy_started.first().at(0).toString(), "mock-data-source");

  ASSERT_TRUE(waitForSignal(spy_finished)) << "installFinished not received within 5 s";
  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "mock-data-source");
  EXPECT_TRUE(spy_finished.first().at(1).toBool()) << "install must succeed";
  EXPECT_TRUE(spy_error.isEmpty());

  EXPECT_TRUE(mgr_->isInstalled("mock-data-source"));
  EXPECT_EQ(mgr_->installedExtensions()["mock-data-source"].version, "1.0.0");
}

// The extracted content lands under extensions_dir/<id>/ after a successful install.
TEST_F(ExtensionManagerTest, InstallCreatesExtensionDirectory) {
  server_.setBody(dummyPluginZip("mock-file-source"));
  const Extension ext = makeExtension("mock-file-source", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy));
  ASSERT_TRUE(spy.first().at(1).toBool());

  EXPECT_TRUE(QDir(ext_dir_.path() + "/mock-file-source").exists());
}

// installProgress signals are forwarded during the download phase.
// Each signal must carry the correct extension id and a percent in [0, 100].
TEST_F(ExtensionManagerTest, InstallEmitsProgressSignals) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_progress(mgr_, &ExtensionManager::installProgress);
  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);

  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy_finished));

  EXPECT_GE(spy_progress.count(), 1);
  for (const QList<QVariant>& args : spy_progress) {
    EXPECT_EQ(args.at(0).toString(), "mock-data-source");
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
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  // First install — must succeed.
  QSignalSpy spy_first(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy_first));
  ASSERT_TRUE(spy_first.first().at(1).toBool());

  // Second install — must be rejected with an error.
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  mgr_->install(ext);
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_EQ(spy_error.first().at(0).toString(), "mock-data-source");
  EXPECT_FALSE(spy_error.first().at(1).toString().isEmpty());
}

// Calling install() for a second extension while one is already in progress must
// reject the second request immediately via installError.
TEST_F(ExtensionManagerTest, InstallBlocksConcurrentRequests) {
  // A server that accepts TCP connections but never sends any data keeps the first
  // download pending indefinitely without burning CPU or requiring a timeout.
  QTcpServer hanging_server;
  hanging_server.listen(QHostAddress::LocalHost, 0);
  const QUrl hanging_url = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(hanging_server.serverPort()));

  const Extension ext_a = makeExtension("mock-data-source", "1.0.0", hanging_url);
  const Extension ext_b = makeExtension("mock-file-source", "1.0.0", hanging_url);

  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext_a);  // begins — will hang until TearDown cleans up
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  mgr_->install(ext_b);  // must be rejected immediately; pending_id_ is already set

  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_EQ(spy_error.first().at(0).toString(), "mock-file-source");
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

TEST_F(ExtensionManagerTest, InstallRejectsEmbeddedIdMismatch) {
  server_.setBody(pluginZipWithDso("registry-id", "mock-data-source"));
  const Extension ext = makeExtension("registry-id", "1.0.0", server_.url());

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext);

  ASSERT_TRUE(waitForSignal(spy_finished));
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("Embedded plugin id"));
  EXPECT_FALSE(QDir(ext_dir_.path() + "/registry-id").exists());
  EXPECT_FALSE(mgr_->isInstalled("registry-id"));
  EXPECT_FALSE(mgr_->isInstalled("mock-data-source"));
}

TEST_F(ExtensionManagerTest, InstallRejectsEmbeddedVersionMismatch) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "2.0.0", server_.url());

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext);

  ASSERT_TRUE(waitForSignal(spy_finished));
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("Embedded plugin version"));
  EXPECT_FALSE(QDir(ext_dir_.path() + "/mock-data-source").exists());
  EXPECT_FALSE(mgr_->isInstalled("mock-data-source"));
}

TEST_F(ExtensionManagerTest, InstallRejectsManifestMissingId) {
  server_.setBody(dummyPluginZip("missing-id-source"));
  const Extension ext = makeExtension("missing-id-source", "1.0.0", server_.url());

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext);

  ASSERT_TRUE(waitForSignal(spy_finished));
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("not a valid plugin"));
  EXPECT_FALSE(QDir(ext_dir_.path() + "/missing-id-source").exists());
}

TEST_F(ExtensionManagerTest, InstallRejectsExtensionDirectoryWithConflictingEmbeddedIds) {
  server_.setBody(pluginZipWithTwoDsos("mock-data-source", "mock-data-source", "mock-file-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext);

  ASSERT_TRUE(waitForSignal(spy_finished));
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("multiple embedded plugin ids"));
  EXPECT_FALSE(QDir(ext_dir_.path() + "/mock-data-source").exists());
}

TEST_F(ExtensionManagerTest, InstallRejectsWrongTopLevelDirectoryWithoutLeavingStrays) {
  server_.setBody(pluginZipWithDso("wrong-root", "mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext);

  ASSERT_TRUE(waitForSignal(spy_finished));
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("top-level directory"));
  EXPECT_FALSE(QDir(ext_dir_.path() + "/wrong-root").exists());
  EXPECT_FALSE(QDir(ext_dir_.path() + "/mock-data-source").exists());
  EXPECT_TRUE(directoryHasNoChildren(ext_dir_.path()));
  EXPECT_FALSE(mgr_->isInstalled("mock-data-source"));
}

TEST_F(ExtensionManagerTest, InstallRejectsExtraTopLevelDirectoryWithoutLeavingStrays) {
  server_.setBody(buildZip({
      {"mock-data-source/" + pluginFileName(), readAll(pluginPathForId("mock-data-source"))},
      {"unrelated-extension/" + pluginFileName(), readAll(pluginPathForId("mock-file-source"))},
  }));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->install(ext);

  ASSERT_TRUE(waitForSignal(spy_finished));
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("top-level directory"));
  EXPECT_FALSE(QDir(ext_dir_.path() + "/mock-data-source").exists());
  EXPECT_FALSE(QDir(ext_dir_.path() + "/unrelated-extension").exists());
  EXPECT_TRUE(directoryHasNoChildren(ext_dir_.path()));
  EXPECT_FALSE(mgr_->isInstalled("mock-data-source"));
  EXPECT_FALSE(mgr_->isInstalled("unrelated-extension"));
}

// ---------------------------------------------------------------------------
// [3] Uninstall
// ---------------------------------------------------------------------------

// A successful uninstall removes the extension directory and clears it from memory.
TEST_F(ExtensionManagerTest, UninstallRemovesDirectoryAndState) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_install(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  const QString ext_path = ext_dir_.path() + "/mock-data-source";
  ASSERT_TRUE(QDir(ext_path).exists());

  QSignalSpy spy_uninstall(mgr_, &ExtensionManager::uninstallFinished);
  mgr_->uninstall("mock-data-source");

  ASSERT_EQ(spy_uninstall.count(), 1);
  EXPECT_EQ(spy_uninstall.first().at(0).toString(), "mock-data-source");
  EXPECT_TRUE(spy_uninstall.first().at(1).toBool());

  EXPECT_FALSE(mgr_->isInstalled("mock-data-source"));
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
  // Ensure clean backup state before test (in case previous run failed mid-test).
  QDir(PlatformUtils::backupDir() + "/mock-data-source-1.0.0").removeRecursively();

  QTemporaryDir local_ext_dir(QDir(PlatformUtils::backupDir()).absoluteFilePath("../test_ext_XXXXXX"));
  ASSERT_TRUE(local_ext_dir.isValid());

  DownloadManager local_dl;
  ExtensionManager local_mgr(&local_dl, local_ext_dir.path(), pending_dir_.path());

  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext_v1 = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_install(&local_mgr, &ExtensionManager::installFinished);
  local_mgr.install(ext_v1);
  ASSERT_TRUE(waitForSignal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  // Prepare a "new version" and trigger the update.
  spy_install.clear();
  server_.setBody(dummyPluginZip("mock-data-source", "2.0.0"));
  const Extension ext_v2 = makeExtension("mock-data-source", "2.0.0", server_.url());
  local_mgr.update(ext_v2);

  ASSERT_TRUE(waitForSignal(spy_install));
  EXPECT_TRUE(spy_install.first().at(1).toBool());
  EXPECT_EQ(local_mgr.installedExtensions()["mock-data-source"].version, "2.0.0");

  QDir(PlatformUtils::backupDir() + "/mock-data-source-1.0.0").removeRecursively();
}

// After a successful update the old version directory must exist in backupDir().
//
// ext_dir is placed under the same filesystem root as backupDir() (~/.plotjuggler/)
// so that QDir::rename() can do an atomic move without a cross-device copy.
TEST_F(ExtensionManagerTest, UpdateBacksUpOldVersionOnSuccess) {
  // Ensure clean backup state before test (in case previous run failed mid-test).
  QDir(PlatformUtils::backupDir() + "/mock-data-source-1.0.0").removeRecursively();

  // Place the extension directory on the same filesystem as backupDir().
  QTemporaryDir local_ext_dir(QDir(PlatformUtils::backupDir()).absoluteFilePath("../test_ext_XXXXXX"));
  ASSERT_TRUE(local_ext_dir.isValid());

  DownloadManager local_dl;
  ExtensionManager local_mgr(&local_dl, local_ext_dir.path(), pending_dir_.path());

  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext_v1 = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_install(&local_mgr, &ExtensionManager::installFinished);
  local_mgr.install(ext_v1);
  ASSERT_TRUE(waitForSignal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  ASSERT_TRUE(QFile::exists(local_ext_dir.path() + "/mock-data-source/" + pluginFileName()));

  spy_install.clear();
  server_.setBody(dummyPluginZip("mock-data-source", "2.0.0"));
  const Extension ext_v2 = makeExtension("mock-data-source", "2.0.0", server_.url());
  local_mgr.update(ext_v2);

  ASSERT_TRUE(waitForSignal(spy_install));
  EXPECT_TRUE(spy_install.first().at(1).toBool()) << "update must succeed";
  EXPECT_EQ(local_mgr.installedExtensions()["mock-data-source"].version, "2.0.0");

  const QString backup_dir = PlatformUtils::backupDir() + "/mock-data-source-1.0.0";
  EXPECT_TRUE(QDir(backup_dir).exists()) << "backup directory must exist after update";
  EXPECT_TRUE(QFile::exists(backup_dir + "/" + pluginFileName())) << "original plugin file must be preserved in backup";

  QDir(backup_dir).removeRecursively();
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

  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext_v1 = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_install(&local_mgr, &ExtensionManager::installFinished);
  local_mgr.install(ext_v1);
  ASSERT_TRUE(waitForSignal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  spy_install.clear();
  // Serve garbage data — libarchive will fail to extract it and DownloadManager
  // will emit failed(), which propagates to installFinished(id, false).
  server_.setBody(QByteArray("not_a_valid_zip"));
  const Extension ext_v2 = makeExtension("mock-data-source", "2.0.0", server_.url());

  QSignalSpy spy_error(&local_mgr, &ExtensionManager::installError);
  local_mgr.update(ext_v2);

  ASSERT_TRUE(waitForSignal(spy_install)) << "installFinished must fire even on failure";
  EXPECT_FALSE(spy_install.first().at(1).toBool()) << "install must have failed";
  EXPECT_FALSE(spy_error.isEmpty()) << "installError must be emitted on failure";

  EXPECT_FALSE(local_mgr.isInstalled("mock-data-source"));

  const QString backup_dir = PlatformUtils::backupDir() + "/mock-data-source-1.0.0";
  EXPECT_TRUE(QDir(backup_dir).exists()) << "backup must survive a failed install — files are recoverable";
  EXPECT_TRUE(QFile::exists(backup_dir + "/" + pluginFileName()))
      << "original plugin binary must be preserved in backup";
  EXPECT_TRUE(spy_error.first().at(1).toString().contains(backup_dir));
  ASSERT_FALSE(local_mgr.diagnostics().isEmpty());
  EXPECT_TRUE(local_mgr.diagnostics().back().message.contains(backup_dir));

  QDir(backup_dir).removeRecursively();
}

// ---------------------------------------------------------------------------
// [5] hasUpdate — version comparison
//
// Extension data below mirrors the registry.json fixture:
//   mock-data-source   v1.0.0
//   mock-file-source v1.0.0
// ---------------------------------------------------------------------------

// Returns false when the extension is not installed.
TEST_F(ExtensionManagerTest, HasUpdateReturnsFalseWhenNotInstalled) {
  Extension ext;
  ext.id = "mock-data-source";
  ext.version = "1.0.0";
  EXPECT_FALSE(mgr_->hasUpdate(ext));
}

// Returns false when the installed and registry versions are identical.
TEST_F(ExtensionManagerTest, HasUpdateReturnsFalseForSameVersion) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy));

  EXPECT_FALSE(mgr_->hasUpdate(ext));
}

// Returns true when the registry version is strictly higher than the installed one.
TEST_F(ExtensionManagerTest, HasUpdateReturnsTrueForNewerVersion) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext_v1 = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_v1);
  ASSERT_TRUE(waitForSignal(spy));

  Extension ext_v2 = ext_v1;
  ext_v2.version = "2.0.0";
  EXPECT_TRUE(mgr_->hasUpdate(ext_v2));
}

// QVersionNumber must compare versions numerically, not lexically:
// "10.0.0" > "2.0.0" — a raw string compare would invert this result.
TEST_F(ExtensionManagerTest, HasUpdateHandlesMultiSegmentVersionsCorrectly) {
  server_.setBody(dummyPluginZip("mock-data-source", "2.0.0"));
  const Extension ext_installed = makeExtension("mock-data-source", "2.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_installed);
  ASSERT_TRUE(waitForSignal(spy));

  // "10.0.0" is numerically greater but lexically smaller than "2.0.0".
  Extension ext_registry = ext_installed;
  ext_registry.version = "10.0.0";
  EXPECT_TRUE(mgr_->hasUpdate(ext_registry));
}

TEST_F(ExtensionManagerTest, HasNewerInstalledVersionReturnsTrueWhenLocalVersionIsAhead) {
  server_.setBody(dummyPluginZip("mock-data-source", "2.0.0"));
  const Extension ext_v2 = makeExtension("mock-data-source", "2.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_v2);
  ASSERT_TRUE(waitForSignal(spy));

  Extension ext_v1 = ext_v2;
  ext_v1.version = "1.0.0";
  EXPECT_TRUE(mgr_->hasNewerInstalledVersion(ext_v1));
  EXPECT_FALSE(mgr_->hasUpdate(ext_v1));
}

// Returns false when the registry version is older than the installed one (downgrade scenario).
TEST_F(ExtensionManagerTest, HasUpdateReturnsFalseForOlderVersion) {
  server_.setBody(dummyPluginZip("mock-data-source", "2.0.0"));
  const Extension ext_v2 = makeExtension("mock-data-source", "2.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_v2);
  ASSERT_TRUE(waitForSignal(spy));

  Extension ext_v1 = ext_v2;
  ext_v1.version = "1.0.0";
  EXPECT_FALSE(mgr_->hasUpdate(ext_v1));
}

TEST_F(ExtensionManagerTest, UpdateRejectsDowngradeWhenInstalledVersionIsNewer) {
  server_.setBody(dummyPluginZip("mock-data-source", "2.0.0"));
  const Extension ext_v2 = makeExtension("mock-data-source", "2.0.0", server_.url());

  QSignalSpy spy_install(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext_v2);
  ASSERT_TRUE(waitForSignal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());

  server_.setBody(dummyPluginZip("mock-data-source", "1.0.0"));
  const Extension ext_v1 = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_update(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  mgr_->update(ext_v1);

  ASSERT_TRUE(waitForSignal(spy_update));
  EXPECT_FALSE(spy_update.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("newer than registry"));
  EXPECT_EQ(mgr_->installedExtensions()["mock-data-source"].version, "2.0.0");
}

// ---------------------------------------------------------------------------
// [6] applyPendingInstalls — Windows post-restart staging simulation
//
// On Windows, DLLs in use cannot be overwritten, so update() stages into
// the configured pending directory. On the next startup, applyPendingInstalls() moves
// the directory into extensions/ and registers it from the DSO's embedded manifest.
// These tests create that directory structure
// manually and verify the promotion logic on any platform (the function is
// always safe to call).
// ---------------------------------------------------------------------------

// applyPendingInstalls() promotes a staged extension to extensions/ and registers it.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsPromotesStagedExtension) {
  const QString staged_dir = pending_dir_.path() + "/mock-data-source";
  ASSERT_TRUE(copyFixturePlugin(staged_dir, "mock-data-source"));
  ASSERT_TRUE(writePendingIntentForTest(staged_dir, "mock-data-source"));

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  mgr_->applyPendingInstalls();

  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "mock-data-source");
  EXPECT_TRUE(spy_finished.first().at(1).toBool());

  // Extension must be queryable as installed.
  EXPECT_TRUE(mgr_->isInstalled("mock-data-source"));
  EXPECT_EQ(mgr_->installedExtensions()["mock-data-source"].version, "1.0.0");

  // The active directory lives under extensions_dir, not pending_dir.
  EXPECT_TRUE(QDir(ext_dir_.path() + "/mock-data-source").exists());
  EXPECT_FALSE(QDir(staged_dir).exists());
}

TEST_F(ExtensionManagerTest, StageInstallRejectsEmbeddedIdMismatchBeforeRestart) {
  server_.setBody(pluginZipWithDso("registry-id", "mock-data-source"));
  const Extension ext = makeExtension("registry-id", "1.0.0", server_.url());

  QSignalSpy spy_pending(mgr_, &ExtensionManager::installPendingRestart);
  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->testDoInstall(ext, /*staging=*/true);

  ASSERT_TRUE(waitForInstallOutcome(spy_finished, spy_pending));
  EXPECT_EQ(spy_pending.count(), 0);
  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "registry-id");
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("Embedded plugin id"));
  EXPECT_FALSE(QDir(pending_dir_.path() + "/registry-id").exists());
}

TEST_F(ExtensionManagerTest, StageInstallRejectsEmbeddedVersionMismatchBeforeRestart) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "2.0.0", server_.url());

  QSignalSpy spy_pending(mgr_, &ExtensionManager::installPendingRestart);
  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);

  mgr_->testDoInstall(ext, /*staging=*/true);

  ASSERT_TRUE(waitForInstallOutcome(spy_finished, spy_pending));
  EXPECT_EQ(spy_pending.count(), 0);
  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "mock-data-source");
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("Embedded plugin version"));
  EXPECT_FALSE(QDir(pending_dir_.path() + "/mock-data-source").exists());
}

TEST_F(ExtensionManagerTest, ApplyPendingInstallsRejectsStagedVersionMismatchAgainstRegistryIntent) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_pending(mgr_, &ExtensionManager::installPendingRestart);
  QSignalSpy spy_stage_finished(mgr_, &ExtensionManager::installFinished);

  mgr_->testDoInstall(ext, /*staging=*/true);

  ASSERT_TRUE(waitForInstallOutcome(spy_stage_finished, spy_pending));
  ASSERT_EQ(spy_pending.count(), 1);
  ASSERT_EQ(spy_stage_finished.count(), 0);

  const QString staged_plugin = pending_dir_.path() + "/mock-data-source/" + pluginFileName();
  ASSERT_TRUE(QFile::remove(staged_plugin));
  ASSERT_TRUE(QFile::copy(pluginPathForId("mock-data-source", "2.0.0"), staged_plugin));

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  mgr_->applyPendingInstalls();

  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "mock-data-source");
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("Embedded plugin version"));
  EXPECT_FALSE(QDir(pending_dir_.path() + "/mock-data-source").exists());
  EXPECT_FALSE(QDir(ext_dir_.path() + "/mock-data-source").exists());
  EXPECT_FALSE(mgr_->isInstalled("mock-data-source"));
}

// A staged directory that lacks the registry-intent marker is rejected and removed;
// otherwise every startup would silently skip the same broken stage forever.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsRejectsEmptyStagingDirectory) {
  const QString staged_dir = pending_dir_.path() + "/bad-extension";
  ASSERT_TRUE(QDir().mkpath(staged_dir));

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  mgr_->applyPendingInstalls();

  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "bad-extension");
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(spy_error.first().at(1).toString().contains("registry intent"));
  EXPECT_FALSE(mgr_->isInstalled("bad-extension"));
  EXPECT_FALSE(QDir(staged_dir).exists());
  ASSERT_FALSE(mgr_->diagnostics().isEmpty());
  EXPECT_TRUE(mgr_->diagnostics().back().message.contains("registry intent"));
}

// Windows update path: when applyPendingInstalls() promotes a staged update over an
// existing install, it must move the previous version into PlatformUtils::backupDir()
// before the rename, mirroring the synchronous backup that update() performs on
// Linux/macOS. Without this, a Windows update would silently overwrite the previous
// version with no recovery path.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsBacksUpExistingExtensionBeforePromotion) {
  // Place ext_dir + pending_dir on the same filesystem as backupDir() so all the
  // QDir::rename moves are atomic (no cross-device copy fallback).
  QTemporaryDir local_ext_dir(QDir(PlatformUtils::backupDir()).absoluteFilePath("../test_ext_XXXXXX"));
  QTemporaryDir local_pending_dir(QDir(PlatformUtils::backupDir()).absoluteFilePath("../test_pending_XXXXXX"));
  ASSERT_TRUE(local_ext_dir.isValid());
  ASSERT_TRUE(local_pending_dir.isValid());
  // Clean any stale backup from a previous failed run.
  QDir(PlatformUtils::backupDir() + "/mock-data-source-1.0.0").removeRecursively();

  DownloadManager local_dl;
  ExtensionManager local_mgr(&local_dl, local_ext_dir.path(), local_pending_dir.path());

  // 1. Install v1 directly to populate extensions/<id>/.
  server_.setBody(dummyPluginZip("mock-data-source"));
  QSignalSpy spy_install(&local_mgr, &ExtensionManager::installFinished);
  local_mgr.install(makeExtension("mock-data-source", "1.0.0", server_.url()));
  ASSERT_TRUE(waitForSignal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());
  spy_install.clear();

  // 2. Manually stage a v2 update with intent file (mirrors what doInstall(staging=true)
  //    leaves on disk on Windows before the user restarts).
  const QString staged_dir = local_pending_dir.path() + "/mock-data-source";
  ASSERT_TRUE(copyFixturePlugin(staged_dir, "mock-data-source", "2.0.0"));
  ASSERT_TRUE(writePendingIntentForTest(staged_dir, "mock-data-source", "2.0.0"));

  // 3. Restart-time apply.
  local_mgr.applyPendingInstalls();

  ASSERT_EQ(spy_install.count(), 1);
  EXPECT_TRUE(spy_install.first().at(1).toBool()) << "staged update must promote";
  EXPECT_EQ(local_mgr.installedExtensions()["mock-data-source"].version, "2.0.0");

  // 4. The previous version must be preserved in backup, recoverable manually.
  const QString backup_dir = PlatformUtils::backupDir() + "/mock-data-source-1.0.0";
  EXPECT_TRUE(QDir(backup_dir).exists())
      << "applyPendingInstalls must back up the previous version before overwriting it";
  EXPECT_TRUE(QFile::exists(backup_dir + "/" + pluginFileName()))
      << "previous plugin file must survive in backup for manual rollback";

  QDir(backup_dir).removeRecursively();
}

// applyPendingInstalls() is a no-op when the pending directory contains no sub-directories.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsIsNoOpForEmptyDirectory) {
  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->applyPendingInstalls();
  EXPECT_EQ(spy.count(), 0);
}

// Multiple staged extensions in the pending directory are all promoted in a single call.
TEST_F(ExtensionManagerTest, ApplyPendingInstallsPromotesMultipleExtensions) {
  for (const QString& id : QStringList{"mock-data-source", "mock-file-source"}) {
    const QString staged = pending_dir_.path() + "/" + id;
    ASSERT_TRUE(copyFixturePlugin(staged, id));
    ASSERT_TRUE(writePendingIntentForTest(staged, id));
  }

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->applyPendingInstalls();

  EXPECT_EQ(spy.count(), 2);
  EXPECT_TRUE(mgr_->isInstalled("mock-data-source"));
  EXPECT_TRUE(mgr_->isInstalled("mock-file-source"));
}

TEST_F(ExtensionManagerTest, ApplyPendingInstallsRejectsEmbeddedIdMismatch) {
  const QString staged_dir = pending_dir_.path() + "/registry-id";
  ASSERT_TRUE(copyFixturePlugin(staged_dir, "mock-data-source"));
  ASSERT_TRUE(writePendingIntentForTest(staged_dir, "registry-id"));

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  mgr_->applyPendingInstalls();

  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "registry-id");
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_FALSE(QDir(ext_dir_.path() + "/registry-id").exists());
  EXPECT_FALSE(mgr_->isInstalled("registry-id"));
  EXPECT_FALSE(mgr_->isInstalled("mock-data-source"));
}

TEST_F(ExtensionManagerTest, ApplyPendingInstallsKeepsExistingInstallWhenStagedUpdateFailsValidation) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_install(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy_install));
  ASSERT_TRUE(spy_install.first().at(1).toBool());
  ASSERT_TRUE(QDir(ext_dir_.path() + "/mock-data-source").exists());

  const QString staged_dir = pending_dir_.path() + "/mock-data-source";
  ASSERT_TRUE(copyFixturePlugin(staged_dir, "mock-file-source"));
  ASSERT_TRUE(writePendingIntentForTest(staged_dir, "mock-data-source"));

  spy_install.clear();
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  mgr_->applyPendingInstalls();

  ASSERT_EQ(spy_install.count(), 1);
  EXPECT_FALSE(spy_install.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_TRUE(mgr_->isInstalled("mock-data-source"));
  EXPECT_TRUE(QDir(ext_dir_.path() + "/mock-data-source").exists());
  EXPECT_FALSE(QDir(staged_dir).exists());
}

TEST_F(ExtensionManagerTest, ApplyPendingInstallsRejectsBrokenDso) {
  const QString staged_dir = pending_dir_.path() + "/bad-extension";
  ASSERT_TRUE(QDir().mkpath(staged_dir));

  QFile broken(QDir(staged_dir).absoluteFilePath(pluginFileName()));
  ASSERT_TRUE(broken.open(QIODevice::WriteOnly));
  broken.write("not a shared library");
  broken.close();
  ASSERT_TRUE(writePendingIntentForTest(staged_dir, "bad-extension"));

  QSignalSpy spy_finished(mgr_, &ExtensionManager::installFinished);
  QSignalSpy spy_error(mgr_, &ExtensionManager::installError);
  mgr_->applyPendingInstalls();

  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_EQ(spy_finished.first().at(0).toString(), "bad-extension");
  EXPECT_FALSE(spy_finished.first().at(1).toBool());
  ASSERT_EQ(spy_error.count(), 1);
  EXPECT_FALSE(QDir(ext_dir_.path() + "/bad-extension").exists());
}

TEST_F(ExtensionManagerTest, HasPendingInstallRequiresDsoAndRegistryIntent) {
  const QString staged_dir = pending_dir_.path() + "/mock-data-source";
  ASSERT_TRUE(QDir().mkpath(staged_dir));

  QSignalSpy spy_pending(mgr_, &ExtensionManager::installPendingRestart);
  EXPECT_FALSE(mgr_->hasPendingInstall("mock-data-source"));
  EXPECT_EQ(spy_pending.count(), 0);

  ASSERT_TRUE(writePendingIntentForTest(staged_dir, "mock-data-source"));
  EXPECT_FALSE(mgr_->hasPendingInstall("mock-data-source"));
  ASSERT_TRUE(QFile::remove(QDir(staged_dir).absoluteFilePath(".pj_pending_install")));

  ASSERT_TRUE(copyFixturePlugin(staged_dir, "mock-data-source"));
  EXPECT_FALSE(mgr_->hasPendingInstall("mock-data-source"));

  ASSERT_TRUE(writePendingIntentForTest(staged_dir, "mock-data-source"));
  EXPECT_TRUE(mgr_->hasPendingInstall("mock-data-source"));
  EXPECT_EQ(spy_pending.count(), 0);
}

// ---------------------------------------------------------------------------
// [7] State persistence
// ---------------------------------------------------------------------------

// A new ExtensionManager pointing to the same directory discovers the same extensions
// by scanning disk — this simulates an application restart.
TEST_F(ExtensionManagerTest, StatePersistsAcrossManagerRestarts) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy));
  ASSERT_TRUE(spy.first().at(1).toBool());

  // Simulate restart: a brand-new manager reads the same extensions_dir.
  DownloadManager downloader2;
  ExtensionManager mgr2(&downloader2, ext_dir_.path(), pending_dir_.path());

  EXPECT_TRUE(mgr2.isInstalled("mock-data-source"));
  EXPECT_EQ(mgr2.installedExtensions()["mock-data-source"].version, "1.0.0");
}

// Uninstalling removes the directory from disk; a fresh manager scanning the same
// directory must not report the extension as installed.
TEST_F(ExtensionManagerTest, UninstallRemovesEntryFromPersistentState) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy_install(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy_install));

  mgr_->uninstall("mock-data-source");

  DownloadManager downloader2;
  ExtensionManager mgr2(&downloader2, ext_dir_.path(), pending_dir_.path());
  EXPECT_FALSE(mgr2.isInstalled("mock-data-source"));
}

TEST_F(ExtensionManagerTest, RefreshEvictsExtensionWhenDsoIsRemovedExternally) {
  server_.setBody(dummyPluginZip("mock-data-source"));
  const Extension ext = makeExtension("mock-data-source", "1.0.0", server_.url());

  QSignalSpy spy(mgr_, &ExtensionManager::installFinished);
  mgr_->install(ext);
  ASSERT_TRUE(waitForSignal(spy));
  ASSERT_TRUE(spy.first().at(1).toBool());
  ASSERT_TRUE(QFile::remove(ext_dir_.path() + "/mock-data-source/" + pluginFileName()));

  mgr_->refreshInstalledFromDisk();

  EXPECT_FALSE(mgr_->isInstalled("mock-data-source"));
  EXPECT_TRUE(mgr_->installedExtensions().isEmpty());
}

TEST_F(ExtensionManagerTest, LoadStateIgnoresDirectoriesWithoutValidPluginDso) {
  const QString invalid_dir = ext_dir_.path() + "/old-layout";
  ASSERT_TRUE(QDir().mkpath(invalid_dir));
  QFile readme(invalid_dir + "/README.txt");
  ASSERT_TRUE(readme.open(QIODevice::WriteOnly));
  readme.write("not a plugin");
  readme.close();

  mgr_->refreshInstalledFromDisk();

  EXPECT_FALSE(mgr_->isInstalled("old-layout"));
  EXPECT_TRUE(mgr_->installedExtensions().isEmpty());
}

// A new manager pointing to an empty extensions directory starts with no installed
// extensions — no crash or undefined behaviour on first run.
TEST_F(ExtensionManagerTest, FreshManagerHasNoInstalledExtensions) {
  EXPECT_TRUE(mgr_->installedExtensions().isEmpty());
}

// ---------------------------------------------------------------------------
// [9] applyPendingUninstalls — deferred directory cleanup simulation
//
// On Windows, uninstall() writes a .pj_pending_uninstall marker inside the
// extension directory when removeRecursively() fails. On the next startup,
// applyPendingUninstalls() removes every extension directory that carries that
// marker. These tests create that state manually so the cleanup logic can be
// verified on any platform.
// ---------------------------------------------------------------------------

// A directory containing the marker is removed by applyPendingUninstalls().
TEST_F(ExtensionManagerTest, ApplyPendingUninstallsRemovesMarkedDirectory) {
  const QString ext_path = ext_dir_.path() + "/mock-data-source";
  ASSERT_TRUE(QDir().mkpath(ext_path));
  QFile marker(ext_path + "/.pj_pending_uninstall");
  ASSERT_TRUE(marker.open(QIODevice::WriteOnly));
  marker.close();

  mgr_->applyPendingUninstalls();

  EXPECT_FALSE(QDir(ext_path).exists());
}

// A directory without the marker is left untouched.
TEST_F(ExtensionManagerTest, ApplyPendingUninstallsIgnoresUnmarkedDirectory) {
  const QString ext_path = ext_dir_.path() + "/mock-data-source";
  ASSERT_TRUE(QDir().mkpath(ext_path));

  mgr_->applyPendingUninstalls();

  EXPECT_TRUE(QDir(ext_path).exists());
}

// applyPendingUninstalls() is a no-op when extensions_dir contains no sub-directories.
TEST_F(ExtensionManagerTest, ApplyPendingUninstallsIsNoOpForEmptyDirectory) {
  mgr_->applyPendingUninstalls();  // must not crash
}

// ---------------------------------------------------------------------------
// [8] Platform detection
// ---------------------------------------------------------------------------

// currentPlatform() must return a non-empty string in "<os>-<arch>" format.
TEST(PlatformDetectionTest, CurrentPlatformHasExpectedFormat) {
  const QString platform = PlatformUtils::currentPlatform();
  EXPECT_FALSE(platform.isEmpty());
  EXPECT_TRUE(platform.contains('-')) << "Expected '<os>-<arch>' format, got: " << platform.toStdString();
}

// On the primary Linux x86_64 build/CI host, the reported platform must match the
// key used in the registry fixture so that install() can resolve the download artifact.
TEST(PlatformDetectionTest, LinuxX86PlatformMatchesRegistryKey) {
  EXPECT_EQ(PlatformUtils::currentPlatform(), "linux-x86_64");
}

// Verify that PlatformUtils::currentPlatform() returns a key that would exist
// in a typical registry entry, so install() can resolve the download artifact.
TEST(PlatformDetectionTest, CurrentPlatformResolvesRegistryArtifact) {
  // Test fixture with fake URLs - we only check that the platform key exists
  Extension ext;
  ext.id = "test-extension";
  ext.version = "1.0.0";
  ext.platforms["linux-x86_64"] = {
      "https://example.com/test/extension-linux-x86_64.zip",
      "sha256:0000000000000000000000000000000000000000000000000000000000000000"};
  ext.platforms["windows-x86_64"] = {
      "https://example.com/test/extension-windows-x64.zip",
      "sha256:0000000000000000000000000000000000000000000000000000000000000000"};

  EXPECT_TRUE(ext.platforms.contains(PlatformUtils::currentPlatform()))
      << "Platform '" << PlatformUtils::currentPlatform().toStdString()
      << "' is not listed in the mock-data-source registry entry";
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
