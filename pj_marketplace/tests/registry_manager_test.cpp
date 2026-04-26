// Tests for PJ::RegistryManager
//
// Coverage:
//   [1] Downloads JSON from a local test URL
//   [2] Parses JSON correctly and constructs Extension objects
//   [3] Emits fetchStarted, fetchFinished, and fetchError with the right values
//   [4] Handles network errors gracefully (connection refused, invalid JSON, missing fields)

#include "pj_marketplace/registry_manager.hpp"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QHostAddress>
#include <QSignalSpy>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

// ---------------------------------------------------------------------------
// Minimal HTTP/1.1 server — serves one fixed JSON body per connection
// ---------------------------------------------------------------------------

class TestHttpServer : public QTcpServer {
 public:
  explicit TestHttpServer(QObject* parent = nullptr) : QTcpServer(parent) {
    connect(this, &QTcpServer::newConnection, this, &TestHttpServer::onNewConnection);
  }

  void setResponseBody(const QByteArray& body) {
    body_ = body;
  }

  // Returns the base URL after a successful listen()
  QUrl url() const {
    return QUrl(QString("http://127.0.0.1:%1").arg(serverPort()));
  }

 private:
  void onNewConnection() {
    QTcpSocket* socket = nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
      socket->readAll();  // discard the HTTP request — content doesn't matter for tests

      QByteArray response;
      response += "HTTP/1.1 200 OK\r\n";
      response += "Content-Type: application/json\r\n";
      response += "Content-Length: " + QByteArray::number(body_.size()) + "\r\n";
      response += "Connection: close\r\n";
      response += "\r\n";
      response += body_;

      socket->write(response);
      socket->flush();
      socket->disconnectFromHost();
      socket->deleteLater();
    });
  }

  QByteArray body_;
};

// ---------------------------------------------------------------------------
// JSON fixtures
// ---------------------------------------------------------------------------

// Full extension with every supported field populated
static const QByteArray kFullRegistryJson = R"({
  "extensions": [
    {
      "id": "csv-loader",
      "name": "CSV Loader",
      "version": "1.2.0",
      "description": "Load CSV/TSV files",
      "author": "Test Author",
      "publisher": "test-org",
      "license": "MIT",
      "website": "https://example.com",
      "repository": "https://github.com/example/csv-loader",
      "icon_url": "https://example.com/icon.png",
      "category": "data_loader",
      "min_plotjuggler_version": "3.8.0",
      "tags": ["csv", "tsv", "file"],
      "platforms": {
        "linux-x86_64": {
          "url": "https://example.com/csv-loader-linux.so",
          "checksum": "sha256:deadbeef"
        },
        "windows-x86_64": {
          "url": "https://example.com/csv-loader-win.dll",
          "checksum": "sha256:cafebabe"
        }
      },
      "changelog": {
        "1.2.0": "Added TSV support",
        "1.0.0": "Initial release"
      }
    }
  ]
})";

// Two minimal extensions (required fields only)
static const QByteArray kMultiExtensionJson = R"({
  "extensions": [
    { "id": "ext-a", "name": "Extension A", "version": "1.0.0" },
    { "id": "ext-b", "name": "Extension B", "version": "2.3.1" }
  ]
})";

static const QByteArray kEmptyExtensionsJson = R"({ "extensions": [] })";

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

namespace PJ {
namespace {

class RegistryManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = new TestHttpServer();
    ASSERT_TRUE(server_->listen(QHostAddress::LocalHost))
        << "TestHttpServer failed to bind — check that a loopback interface is available";
  }

  void TearDown() override {
    server_->close();
    delete server_;
  }

  TestHttpServer* server_ = nullptr;
};

// ---------------------------------------------------------------------------
// [3] Signals — fetchStarted
// ---------------------------------------------------------------------------

// fetchStarted is emitted synchronously, before any I/O takes place
TEST_F(RegistryManagerTest, EmitsFetchStartedImmediatelyOnCall) {
  RegistryManager mgr;
  QSignalSpy spy(&mgr, &RegistryManager::fetchStarted);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());

  // No event loop processing needed — must already be emitted
  EXPECT_EQ(spy.count(), 1);
}

// Two consecutive calls each emit fetchStarted
TEST_F(RegistryManagerTest, EmitsFetchStartedOnEachCall) {
  RegistryManager mgr;
  QSignalSpy spy_started(&mgr, &RegistryManager::fetchStarted);
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  mgr.fetchRegistry(server_->url());  // cancels the previous one and starts fresh

  EXPECT_EQ(spy_started.count(), 2);

  // Wait for the second request to complete
  ASSERT_TRUE(spy_finished.wait(3000));
}

// ---------------------------------------------------------------------------
// [1] + [3] Download and fetchFinished(true) on success
// ---------------------------------------------------------------------------

TEST_F(RegistryManagerTest, EmitsFetchFinishedTrueOnSuccessfulDownload) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());

  ASSERT_TRUE(spy_finished.wait(3000)) << "fetchFinished was not emitted within 3 seconds";
  ASSERT_EQ(spy_finished.count(), 1);
  EXPECT_TRUE(spy_finished.first().at(0).toBool());
}

// ---------------------------------------------------------------------------
// [2] Parsing — required fields
// ---------------------------------------------------------------------------

TEST_F(RegistryManagerTest, ParsesRequiredFields) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  const QList<Extension> exts = mgr.extensions();
  ASSERT_EQ(exts.size(), 1);

  const Extension& ext = exts.at(0);
  EXPECT_EQ(ext.id, "csv-loader");
  EXPECT_EQ(ext.name, "CSV Loader");
  EXPECT_EQ(ext.version, "1.2.0");
}

// [2] Optional string fields
TEST_F(RegistryManagerTest, ParsesOptionalStringFields) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  const QList<Extension> exts = mgr.extensions();
  ASSERT_EQ(exts.size(), 1);

  const Extension& ext = exts.at(0);
  EXPECT_EQ(ext.description, "Load CSV/TSV files");
  EXPECT_EQ(ext.author, "Test Author");
  EXPECT_EQ(ext.publisher, "test-org");
  EXPECT_EQ(ext.license, "MIT");
  EXPECT_EQ(ext.website, "https://example.com");
  EXPECT_EQ(ext.repository, "https://github.com/example/csv-loader");
  EXPECT_EQ(ext.icon_url, "https://example.com/icon.png");
  EXPECT_EQ(ext.category, "data_loader");
  EXPECT_EQ(ext.min_plotjuggler_version, "3.8.0");
}

// [2] Tags array
TEST_F(RegistryManagerTest, ParsesTags) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  const QStringList tags = mgr.extensions().at(0).tags;
  ASSERT_EQ(tags.size(), 3);
  EXPECT_TRUE(tags.contains("csv"));
  EXPECT_TRUE(tags.contains("tsv"));
  EXPECT_TRUE(tags.contains("file"));
}

// [2] Platforms map (url + checksum per platform key)
TEST_F(RegistryManagerTest, ParsesPlatformArtifacts) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  const auto& platforms = mgr.extensions().at(0).platforms;
  ASSERT_EQ(platforms.size(), 2);

  ASSERT_TRUE(platforms.contains("linux-x86_64"));
  EXPECT_EQ(platforms["linux-x86_64"].url, "https://example.com/csv-loader-linux.so");
  EXPECT_EQ(platforms["linux-x86_64"].checksum, "sha256:deadbeef");

  ASSERT_TRUE(platforms.contains("windows-x86_64"));
  EXPECT_EQ(platforms["windows-x86_64"].url, "https://example.com/csv-loader-win.dll");
}

// [2] Changelog map (version -> description)
TEST_F(RegistryManagerTest, ParsesChangelog) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  const auto& changelog = mgr.extensions().at(0).changelog;
  ASSERT_EQ(changelog.size(), 2);
  EXPECT_EQ(changelog["1.2.0"], "Added TSV support");
  EXPECT_EQ(changelog["1.0.0"], "Initial release");
}

// [2] Registry with multiple extensions
TEST_F(RegistryManagerTest, ParsesMultipleExtensions) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kMultiExtensionJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  const QList<Extension> exts = mgr.extensions();
  ASSERT_EQ(exts.size(), 2);
  EXPECT_EQ(exts.at(0).id, "ext-a");
  EXPECT_EQ(exts.at(1).id, "ext-b");
}

// [2] Empty extensions array is valid — results in an empty list
TEST_F(RegistryManagerTest, AcceptsEmptyExtensionsArray) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kEmptyExtensionsJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  EXPECT_TRUE(spy_finished.first().at(0).toBool());
  EXPECT_TRUE(mgr.extensions().isEmpty());
}

// [2] findById returns the matching extension
TEST_F(RegistryManagerTest, FindByIdReturnsCorrectExtension) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  const Extension ext = mgr.findById("csv-loader");
  EXPECT_EQ(ext.id, "csv-loader");
  EXPECT_EQ(ext.name, "CSV Loader");
}

// [2] findById returns a default-constructed (empty id) extension when not found
TEST_F(RegistryManagerTest, FindByIdReturnsEmptyExtensionOnMiss) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));

  EXPECT_TRUE(mgr.findById("nonexistent-plugin").id.isEmpty());
}

// ---------------------------------------------------------------------------
// [4] Network error handling — connection refused
// ---------------------------------------------------------------------------

// Closing the server before the request ensures nothing is listening on that port
TEST_F(RegistryManagerTest, EmitsFetchErrorOnConnectionRefused) {
  const quint16 dead_port = server_->serverPort();
  server_->close();

  RegistryManager mgr;
  QSignalSpy spy_error(&mgr, &RegistryManager::fetchError);
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  mgr.fetchRegistry(QUrl(QString("http://127.0.0.1:%1/registry.json").arg(dead_port)));

  ASSERT_TRUE(spy_finished.wait(5000)) << "fetchFinished was not emitted after a connection error";
  EXPECT_FALSE(spy_finished.first().at(0).toBool());
  ASSERT_GE(spy_error.count(), 1);
  EXPECT_FALSE(spy_error.first().at(0).toString().isEmpty());
}

// ---------------------------------------------------------------------------
// [4] Parse error handling
// ---------------------------------------------------------------------------

TEST_F(RegistryManagerTest, EmitsFetchErrorOnMalformedJson) {
  RegistryManager mgr;
  QSignalSpy spy_error(&mgr, &RegistryManager::fetchError);
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody("{ this is: definitely [not valid json !!!");
  mgr.fetchRegistry(server_->url());

  ASSERT_TRUE(spy_finished.wait(3000));
  EXPECT_FALSE(spy_finished.first().at(0).toBool());
  ASSERT_GE(spy_error.count(), 1);
  EXPECT_TRUE(mgr.extensions().isEmpty());
}

// [4] JSON root is an array instead of an object
TEST_F(RegistryManagerTest, EmitsFetchErrorWhenRootIsNotObject) {
  RegistryManager mgr;
  QSignalSpy spy_error(&mgr, &RegistryManager::fetchError);
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(R"([{"id":"x","name":"X","version":"1.0"}])");
  mgr.fetchRegistry(server_->url());

  ASSERT_TRUE(spy_finished.wait(3000));
  EXPECT_FALSE(spy_finished.first().at(0).toBool());
  EXPECT_GE(spy_error.count(), 1);
}

// [4] Root object is missing the "extensions" key
TEST_F(RegistryManagerTest, EmitsFetchErrorWhenExtensionsKeyMissing) {
  RegistryManager mgr;
  QSignalSpy spy_error(&mgr, &RegistryManager::fetchError);
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(R"({"plugins":[]})");
  mgr.fetchRegistry(server_->url());

  ASSERT_TRUE(spy_finished.wait(3000));
  EXPECT_FALSE(spy_finished.first().at(0).toBool());
  EXPECT_GE(spy_error.count(), 1);
}

// [4] Required field "id" is absent from an extension entry
TEST_F(RegistryManagerTest, EmitsFetchErrorOnMissingRequiredFieldId) {
  RegistryManager mgr;
  QSignalSpy spy_error(&mgr, &RegistryManager::fetchError);
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(R"({"extensions":[{"name":"No ID","version":"1.0.0"}]})");
  mgr.fetchRegistry(server_->url());

  ASSERT_TRUE(spy_finished.wait(3000));
  EXPECT_FALSE(spy_finished.first().at(0).toBool());
  EXPECT_GE(spy_error.count(), 1);
  EXPECT_TRUE(mgr.extensions().isEmpty());
}

// [4] Required field "version" is absent from an extension entry
TEST_F(RegistryManagerTest, EmitsFetchErrorOnMissingRequiredFieldVersion) {
  RegistryManager mgr;
  QSignalSpy spy_error(&mgr, &RegistryManager::fetchError);
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  server_->setResponseBody(R"({"extensions":[{"id":"ext-x","name":"Ext X"}]})");
  mgr.fetchRegistry(server_->url());

  ASSERT_TRUE(spy_finished.wait(3000));
  EXPECT_FALSE(spy_finished.first().at(0).toBool());
  EXPECT_GE(spy_error.count(), 1);
}

// [4] extensions() is empty after a parse error, even if a previous fetch succeeded
TEST_F(RegistryManagerTest, ExtensionsEmptyAfterParseError) {
  RegistryManager mgr;
  QSignalSpy spy_finished(&mgr, &RegistryManager::fetchFinished);

  // First request succeeds
  server_->setResponseBody(kFullRegistryJson);
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));
  ASSERT_EQ(mgr.extensions().size(), 1);

  spy_finished.clear();

  // Second request returns invalid JSON — list must be cleared
  server_->setResponseBody("not json at all");
  mgr.fetchRegistry(server_->url());
  ASSERT_TRUE(spy_finished.wait(3000));
  EXPECT_TRUE(mgr.extensions().isEmpty());
}

}  // namespace
}  // namespace PJ

// ---------------------------------------------------------------------------
// main — QCoreApplication is required for the QNetworkAccessManager event loop
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
