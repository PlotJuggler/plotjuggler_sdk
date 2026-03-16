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

  /// Starts the full pipeline: download url, verify expected_checksum, extract to destination_dir.
  /// Returns a unique ID to track this operation.
  int fetch(const QUrl& url, const QString& expected_checksum, const QString& destination_dir);

  /// Cancels an in-progress operation. No-op if the ID does not exist.
  void cancel(int id);

signals:
  void started(int id);
  void progress(int id, qint64 bytes_received, qint64 bytes_total);
  void finished(int id);
  void cancelled(int id);
  void failed(int id, const QString& error);

private slots:
  void onReplyFinished(QNetworkReply* reply);
  void onDownloadProgress(qint64 bytes_received, qint64 bytes_total);

private:
  struct Operation
  {
    QString expected_checksum;
    QString destination_dir;
  };

  QString calculateSha256(const QByteArray& data) const;
  bool verifyChecksum(const QByteArray& data, const QString& expected_checksum) const;
  PJ::Expected<void, QString> extractFromMemory(const QByteArray& data,
                                                 const QString& destination_dir) const;

  QNetworkAccessManager* network_;
  QMap<int, QNetworkReply*> active_replies_;
  QMap<int, Operation> operations_;
  int next_id_ = 1;
};

}  // namespace PJ
