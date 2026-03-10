#include "DownloadManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>

#include <archive.h>
#include <archive_entry.h>

#include "pj_base/expected.hpp"

namespace PJ {

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

PJ::Expected<void, QString> DownloadManager::extractFromMemory(const QByteArray& data,
                                                       const QString& destinationDir) const
{
  QDir destDir(destinationDir);
  if (!destDir.exists() && !destDir.mkpath(QStringLiteral(".")))
  {
    return PJ::unexpected(
        QStringLiteral("Could not create destination directory: %1").arg(destinationDir));
  }

  // Trailing separator ensures prefix check is exact and not fooled by
  // sibling directories sharing a common prefix (e.g. /tmp/foo vs /tmp/foo_evil).
  const QString safeRoot = destDir.absolutePath() + QLatin1Char('/');

  auto archiveDeleter = [](struct archive* a) { archive_read_free(a); };
  std::unique_ptr<struct archive, decltype(archiveDeleter)> a(archive_read_new(), archiveDeleter);

  archive_read_support_format_zip(a.get());

  if (archive_read_open_memory(a.get(), data.constData(), static_cast<size_t>(data.size())) !=
      ARCHIVE_OK)
  {
    return PJ::unexpected(
        QStringLiteral("Could not open ZIP: %1").arg(QString::fromUtf8(archive_error_string(a.get()))));
  }

  struct archive_entry* entry;
  int r;
  while ((r = archive_read_next_header(a.get(), &entry)) == ARCHIVE_OK)
  {
    const QString entryName = QString::fromUtf8(archive_entry_pathname(entry));
    const QString targetPath = destDir.filePath(entryName);

    // Guard against path-traversal attacks (e.g. entries containing "../")
    if (!QFileInfo(targetPath).absoluteFilePath().startsWith(safeRoot))
    {
      return PJ::unexpected(
          QStringLiteral("Unsafe path detected in ZIP entry: %1").arg(entryName));
    }

    if (archive_entry_filetype(entry) == AE_IFDIR)
    {
      destDir.mkpath(entryName);
      continue;
    }

    // Ensure the parent directory exists before writing
    QFileInfo fi(targetPath);
    QDir().mkpath(fi.absolutePath());

    QFile outFile(targetPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
      return PJ::unexpected(QStringLiteral("No write permission for: %1").arg(targetPath));
    }

    const void* buf;
    size_t size;
    la_int64_t offset;
    for (;;)
    {
      int rc = archive_read_data_block(a.get(), &buf, &size, &offset);
      if (rc == ARCHIVE_EOF)
        break;
      if (rc != ARCHIVE_OK)
      {
        outFile.close();
        return PJ::unexpected(
            QStringLiteral("Error reading ZIP entry '%1': %2")
                .arg(entryName, QString::fromUtf8(archive_error_string(a.get()))));
      }
      outFile.write(static_cast<const char*>(buf), static_cast<qint64>(size));
    }
    outFile.close();
  }

  if (r != ARCHIVE_EOF)
  {
    return PJ::unexpected(
        QStringLiteral("Error during extraction: %1").arg(QString::fromUtf8(archive_error_string(a.get()))));
  }

  return {};
}

// ---------------------------------------------------------------------------
// DownloadManager
// ---------------------------------------------------------------------------

DownloadManager::DownloadManager(QObject* parent)
  : QObject(parent), m_network(new QNetworkAccessManager(this))
{
  connect(m_network, &QNetworkAccessManager::finished, this, &DownloadManager::onReplyFinished);
}

int DownloadManager::fetch(const QUrl& url,
                           const QString& expectedChecksum,
                           const QString& destinationDir)
{
  const int id = m_nextId++;

  QNetworkReply* reply = m_network->get(QNetworkRequest(url));
  reply->setProperty("operationId", id);

  m_activeReplies.insert(id, reply);
  m_operations.insert(id, {expectedChecksum, destinationDir});

  connect(reply, &QNetworkReply::downloadProgress, this, &DownloadManager::onDownloadProgress);

  emit started(id);
  return id;
}

void DownloadManager::cancel(int id)
{
  QNetworkReply* reply = m_activeReplies.value(id, nullptr);
  if (reply)
  {
    reply->abort();
  }
}

void DownloadManager::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
  auto* reply = qobject_cast<QNetworkReply*>(sender());
  if (!reply)
  {
    return;
  }
  emit progress(reply->property("operationId").toInt(), bytesReceived, bytesTotal);
}

void DownloadManager::onReplyFinished(QNetworkReply* reply)
{
  const int id = reply->property("operationId").toInt();
  m_activeReplies.remove(id);
  const Operation op = m_operations.take(id);

  if (reply->error() != QNetworkReply::NoError)
  {
    if (reply->error() == QNetworkReply::OperationCanceledError)
    {
      reply->deleteLater();
      emit cancelled(id);
      return;
    }
    const QString error = reply->errorString();
    reply->deleteLater();
    emit failed(id, error);
    return;
  }

  const QByteArray data = reply->readAll();
  reply->deleteLater();

  if (!op.expectedChecksum.isEmpty() && !verifyChecksum(data, op.expectedChecksum))
  {
    emit failed(id, QStringLiteral("Checksum mismatch"));
    return;
  }

  auto extractResult = extractFromMemory(data, op.destinationDir);
  if (!extractResult)
  {
    emit failed(id, extractResult.error());
    return;
  }

  emit finished(id);
}

QString DownloadManager::calculateSha256(const QByteArray& data) const
{
  return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool DownloadManager::verifyChecksum(const QByteArray& data, const QString& expectedChecksum) const
{
  QString expected = expectedChecksum;
  if (expected.startsWith(QStringLiteral("sha256:")))
  {
    expected = expected.mid(7);
  }
  return calculateSha256(data) == expected;
}

}  // namespace PJ
