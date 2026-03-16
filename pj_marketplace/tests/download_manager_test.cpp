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
bool wait_for_signal(QSignalSpy& spy, int timeout_ms = 5000)
{
  QDeadlineTimer deadline(timeout_ms);
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
    server_.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&server_, &QTcpServer::newConnection, [this]() {
      QTcpSocket* socket = server_.nextPendingConnection();
      socket->setParent(&server_);
      QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        socket->readAll();  // consume the HTTP request
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

  QUrl url() const
  {
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server_.serverPort()));
  }

  void set_body(const QByteArray& body) { body_ = body; }

private:
  QTcpServer server_;
  QByteArray body_;
};

// ---------------------------------------------------------------------------
// Helper: builds an in-memory ZIP from a map of {filename -> content}
// ---------------------------------------------------------------------------

QByteArray build_zip(const QMap<QString, QByteArray>& files)
{
  std::vector<char> buffer(4 * 1024 * 1024);
  size_t used = 0;

  auto write_deleter = [](struct archive* a) { archive_write_free(a); };
  std::unique_ptr<struct archive, decltype(write_deleter)> a(archive_write_new(), write_deleter);

  archive_write_set_format_zip(a.get());
  archive_write_add_filter_none(a.get());
  archive_write_open_memory(a.get(), buffer.data(), buffer.size(), &used);

  auto entry_deleter = [](struct archive_entry* e) { archive_entry_free(e); };
  std::unique_ptr<struct archive_entry, decltype(entry_deleter)> entry(archive_entry_new(),
                                                                       entry_deleter);

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

static QString sha256_hex(const QByteArray& data)
{
  return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(DownloadManagerTest, InvalidUrlEmitsFailed)
{
  PJ::DownloadManager dm;
  QSignalSpy failed_spy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy started_spy(&dm, &PJ::DownloadManager::started);

  const int id = dm.fetch(QUrl("http://255.255.255.255/nonexistent"), {}, {});

  EXPECT_TRUE(wait_for_signal(started_spy));
  EXPECT_EQ(started_spy.first().at(0).toInt(), id);

  EXPECT_TRUE(wait_for_signal(failed_spy));
  EXPECT_EQ(failed_spy.first().at(0).toInt(), id);
  EXPECT_FALSE(failed_spy.first().at(1).toString().isEmpty());
}

TEST(DownloadManagerTest, SuccessfulDownloadExtractsFiles)
{
  const QByteArray zip_data = build_zip({{"hello.txt", "world"}});
  const QString checksum = QStringLiteral("sha256:") + sha256_hex(zip_data);

  LocalHttpServer server;
  server.set_body(zip_data);

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy finished_spy(&dm, &PJ::DownloadManager::finished);
  QSignalSpy failed_spy(&dm, &PJ::DownloadManager::failed);

  dm.fetch(server.url(), checksum, tmp.path());

  EXPECT_TRUE(wait_for_signal(finished_spy));
  EXPECT_TRUE(failed_spy.isEmpty());
  EXPECT_TRUE(QFile::exists(tmp.path() + "/hello.txt"));
}

TEST(DownloadManagerTest, EmptyChecksumSkipsVerification)
{
  const QByteArray zip_data = build_zip({{"readme.txt", "content"}});

  LocalHttpServer server;
  server.set_body(zip_data);

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy finished_spy(&dm, &PJ::DownloadManager::finished);

  dm.fetch(server.url(), {}, tmp.path());

  EXPECT_TRUE(wait_for_signal(finished_spy));
  EXPECT_TRUE(QFile::exists(tmp.path() + "/readme.txt"));
}

TEST(DownloadManagerTest, ChecksumMismatchEmitsFailed)
{
  const QByteArray zip_data = build_zip({{"file.txt", "content"}});

  LocalHttpServer server;
  server.set_body(zip_data);

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy failed_spy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy finished_spy(&dm, &PJ::DownloadManager::finished);

  dm.fetch(server.url(),
           QStringLiteral("sha256:0000000000000000000000000000000000000000000000000000000000000000"),
           tmp.path());

  EXPECT_TRUE(wait_for_signal(failed_spy));
  EXPECT_TRUE(finished_spy.isEmpty());
  EXPECT_TRUE(failed_spy.first().at(1).toString().contains("Checksum"));
}

TEST(DownloadManagerTest, InvalidZipEmitsFailed)
{
  LocalHttpServer server;
  server.set_body(QByteArray("this is not a zip"));

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy failed_spy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy finished_spy(&dm, &PJ::DownloadManager::finished);

  dm.fetch(server.url(), {}, tmp.path());

  EXPECT_TRUE(wait_for_signal(failed_spy));
  EXPECT_TRUE(finished_spy.isEmpty());
}

TEST(DownloadManagerTest, PathTraversalInZipEmitsFailed)
{
  const QByteArray zip_data = build_zip({{"../../evil.txt", "malicious"}});

  LocalHttpServer server;
  server.set_body(zip_data);

  PJ::DownloadManager dm;
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  QSignalSpy failed_spy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy finished_spy(&dm, &PJ::DownloadManager::finished);

  dm.fetch(server.url(), {}, tmp.path());

  EXPECT_TRUE(wait_for_signal(failed_spy));
  EXPECT_TRUE(finished_spy.isEmpty());
}

TEST(DownloadManagerTest, CancelEmitsCancelled)
{
  // Server that accepts connections but never sends a response — download hangs indefinitely.
  QTcpServer hanging_server;
  hanging_server.listen(QHostAddress::LocalHost, 0);

  PJ::DownloadManager dm;
  QSignalSpy cancelled_spy(&dm, &PJ::DownloadManager::cancelled);
  QSignalSpy failed_spy(&dm, &PJ::DownloadManager::failed);
  QSignalSpy finished_spy(&dm, &PJ::DownloadManager::finished);

  const int id = dm.fetch(
      QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(hanging_server.serverPort())), {}, {});

  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  dm.cancel(id);

  EXPECT_TRUE(wait_for_signal(cancelled_spy, 2000));
  EXPECT_EQ(cancelled_spy.first().at(0).toInt(), id);
  EXPECT_TRUE(failed_spy.isEmpty());
  EXPECT_TRUE(finished_spy.isEmpty());
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
