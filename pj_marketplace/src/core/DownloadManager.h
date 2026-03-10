#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

#include "pj_base/expected.hpp"

namespace PJ {

/// Handles the full extension installation pipeline: download → checksum verification → extraction.
///
/// The async fetch() operation is tracked by an integer ID returned at call time.
class DownloadManager : public QObject
{
  Q_OBJECT

public:
  explicit DownloadManager(QObject* parent = nullptr);

  /// Starts the full pipeline: download url, verify expectedChecksum, extract to destinationDir.
  /// Returns a unique ID to track this operation.
  int fetch(const QUrl& url, const QString& expectedChecksum, const QString& destinationDir);

  /// Cancels an in-progress operation. No-op if the ID does not exist.
  void cancel(int id);

signals:
  void started(int id);
  void progress(int id, qint64 bytesReceived, qint64 bytesTotal);
  void finished(int id);
  void cancelled(int id);
  void failed(int id, const QString& error);

private slots:
  void onReplyFinished(QNetworkReply* reply);
  void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
  struct Operation
  {
    QString expectedChecksum;
    QString destinationDir;
  };

  QString calculateSha256(const QByteArray& data) const;
  bool verifyChecksum(const QByteArray& data, const QString& expectedChecksum) const;
  PJ::Expected<void, QString> extractFromMemory(const QByteArray& data,
                                                 const QString& destinationDir) const;

  QNetworkAccessManager* m_network;
  QMap<int, QNetworkReply*> m_activeReplies;
  QMap<int, Operation> m_operations;
  int m_nextId = 1;
};

}  // namespace PJ
