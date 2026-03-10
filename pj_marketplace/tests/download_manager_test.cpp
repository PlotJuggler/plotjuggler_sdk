#include "core/DownloadManager.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>

#include <archive.h>
#include <archive_entry.h>

namespace {

// Spins the event loop until spy receives at least one signal or timeout expires.
bool waitForSignal(QSignalSpy& spy, int timeoutMs = 5000)
{
  QDeadlineTimer deadline(timeoutMs);
  while (spy.isEmpty() && !deadline.hasExpired())
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  return !spy.isEmpty();
}

// ---------------------------------------------------------------------------
// Minimal HTTP/1.1 server that serves a fixed body to all incoming requests.
// Binds to a random loopback port; no external network required.
// ---------------------------------------------------------------------------

class LocalHttpServer
{
public:
  LocalHttpServer()
  {
    m_server.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&m_server, &QTcpServer::newConnection, [this]() {
      QTcpSocket* socket = m_server.nextPendingConnection();
      socket->setParent(&m_server);
      QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        socket->readAll();  // consume the HTTP request
        const QByteArray header = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: application/octet-stream\r\n"
                                  "Content-Length: " +
                                  QByteArray::number(m_body.size()) +
                                  "\r\n"
                                  "Connection: close\r\n\r\n";
        socket->write(header + m_body);
        socket->flush();
        socket->disconnectFromHost();
      });
    });
  }

  QUrl url() const
  {
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_server.serverPort()));
  }

  void setBody(const QByteArray& body) { m_body = body; }

private:
  QTcpServer m_server;
  QByteArray m_body;
};

// ---------------------------------------------------------------------------
// Helper: builds an in-memory ZIP from a map of {filename -> content}
// ---------------------------------------------------------------------------

QByteArray buildZip(const QMap<QString, QByteArray>& files)
{
  std::vector<char> buffer(4 * 1024 * 1024);
  size_t used = 0;

  auto writeDeleter = [](struct archive* a) { archive_write_free(a); };
  std::unique_ptr<struct archive, decltype(writeDeleter)> a(archive_write_new(), writeDeleter);

  archive_write_set_format_zip(a.get());
  archive_write_add_filter_none(a.get());
  archive_write_open_memory(a.get(), buffer.data(), buffer.size(), &used);

  auto entryDeleter = [](struct archive_entry* e) { archive_entry_free(e); };
  std::unique_ptr<struct archive_entry, decltype(entryDeleter)> entry(archive_entry_new(),
                                                                       entryDeleter);

  for (auto it = files.cbegin(); it != files.cend(); ++it)
  {
    archive_entry_clear(entry.get());
    archive_entry_set_pathname(entry.get(), it.key().toUtf8().constData());
    archive_entry_set_size(entry.get(), it.value().size());
    archive_entry_set_filetype(entry.get(), AE_IFREG);
    archive_entry_set_perm(entry.get(), 0644);
    archive_write_header(a.get(), entry.get());
    archive_write_data(a.get(), it.value().constData(), static_cast<size_t>(it.value().size()));
  }

  archive_write_close(a.get());
  return QByteArray(buffer.data(), static_cast<int>(used));
}

static QString sha256Hex(const QByteArray& data)
{
  return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(DownloadManagerTest, InvalidUrlEmitsFailed)
{
  PJ::DownloadManager dm;
  QSignalSpy failedSpy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy startedSpy(&dm, &PJ::DownloadManager::started);

  const int id = dm.fetch(QUrl("http://255.255.255.255/nonexistent"), {}, {});

  EXPECT_TRUE(waitForSignal(startedSpy));
  EXPECT_EQ(startedSpy.first().at(0).toInt(), id);

  EXPECT_TRUE(waitForSignal(failedSpy));
  EXPECT_EQ(failedSpy.first().at(0).toInt(), id);
  EXPECT_FALSE(failedSpy.first().at(1).toString().isEmpty());
}

TEST(DownloadManagerTest, SuccessfulDownloadExtractsFiles)
{
  const QByteArray zipData = buildZip({{"hello.txt", "world"}});
  const QString checksum = QStringLiteral("sha256:") + sha256Hex(zipData);

  LocalHttpServer server;
  server.setBody(zipData);

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy finishedSpy(&dm, &PJ::DownloadManager::finished);
  QSignalSpy failedSpy(&dm, &PJ::DownloadManager::failed);

  dm.fetch(server.url(), checksum, tmp.path());

  EXPECT_TRUE(waitForSignal(finishedSpy));
  EXPECT_TRUE(failedSpy.isEmpty());
  EXPECT_TRUE(QFile::exists(tmp.path() + "/hello.txt"));
}

TEST(DownloadManagerTest, EmptyChecksumSkipsVerification)
{
  const QByteArray zipData = buildZip({{"readme.txt", "content"}});

  LocalHttpServer server;
  server.setBody(zipData);

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy finishedSpy(&dm, &PJ::DownloadManager::finished);

  dm.fetch(server.url(), {}, tmp.path());

  EXPECT_TRUE(waitForSignal(finishedSpy));
  EXPECT_TRUE(QFile::exists(tmp.path() + "/readme.txt"));
}

TEST(DownloadManagerTest, ChecksumMismatchEmitsFailed)
{
  const QByteArray zipData = buildZip({{"file.txt", "content"}});

  LocalHttpServer server;
  server.setBody(zipData);

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy failedSpy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy finishedSpy(&dm, &PJ::DownloadManager::finished);

  dm.fetch(server.url(),
           QStringLiteral("sha256:0000000000000000000000000000000000000000000000000000000000000000"),
           tmp.path());

  EXPECT_TRUE(waitForSignal(failedSpy));
  EXPECT_TRUE(finishedSpy.isEmpty());
  EXPECT_TRUE(failedSpy.first().at(1).toString().contains("Checksum"));
}

TEST(DownloadManagerTest, InvalidZipEmitsFailed)
{
  LocalHttpServer server;
  server.setBody(QByteArray("this is not a zip"));

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy failedSpy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy finishedSpy(&dm, &PJ::DownloadManager::finished);

  dm.fetch(server.url(), {}, tmp.path());

  EXPECT_TRUE(waitForSignal(failedSpy));
  EXPECT_TRUE(finishedSpy.isEmpty());
}

TEST(DownloadManagerTest, PathTraversalInZipEmitsFailed)
{
  const QByteArray zipData = buildZip({{"../../evil.txt", "malicious"}});

  LocalHttpServer server;
  server.setBody(zipData);

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy failedSpy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy finishedSpy(&dm, &PJ::DownloadManager::finished);

  dm.fetch(server.url(), {}, tmp.path());

  EXPECT_TRUE(waitForSignal(failedSpy));
  EXPECT_TRUE(finishedSpy.isEmpty());
}

TEST(DownloadManagerTest, CancelEmitsCancelled)
{
  // Server that accepts connections but never sends a response — download hangs indefinitely.
  QTcpServer hangingServer;
  hangingServer.listen(QHostAddress::LocalHost, 0);

  PJ::DownloadManager dm;
  QSignalSpy cancelledSpy(&dm, &PJ::DownloadManager::cancelled);
  QSignalSpy failedSpy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy finishedSpy(&dm, &PJ::DownloadManager::finished);

  const int id = dm.fetch(
      QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(hangingServer.serverPort())), {}, {});

  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  dm.cancel(id);

  EXPECT_TRUE(waitForSignal(cancelledSpy, 2000));
  EXPECT_EQ(cancelledSpy.first().at(0).toInt(), id);
  EXPECT_TRUE(failedSpy.isEmpty());
  EXPECT_TRUE(finishedSpy.isEmpty());
}

TEST(DownloadManagerTest, MultipleOperationsHaveUniqueIds)
{
  PJ::DownloadManager dm;

  const int id1 = dm.fetch(QUrl("http://255.255.255.255/1"), {}, {});
  const int id2 = dm.fetch(QUrl("http://255.255.255.255/2"), {}, {});
  const int id3 = dm.fetch(QUrl("http://255.255.255.255/3"), {}, {});

  EXPECT_NE(id1, id2);
  EXPECT_NE(id2, id3);
  EXPECT_NE(id1, id3);

  dm.cancel(id1);
  dm.cancel(id2);
  dm.cancel(id3);
}

}  // namespace

// ---------------------------------------------------------------------------
// main: required to initialise QCoreApplication before GTest runs
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
