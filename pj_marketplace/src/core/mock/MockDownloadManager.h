#pragma once

#include <QMap>
#include <QObject>
#include <QTimer>

#include "core/DownloadManager.h"

namespace PJ {

/// Simulates the full DownloadManager pipeline using QTimers instead of real HTTP.
///
/// fetch() returns an id immediately and then emits:
///   started(id), progress(id, 0..total, total) every 100 ms, finished(id)
///
/// cancel() stops the timer and emits cancelled(id).
class MockDownloadManager : public DownloadManager {
  Q_OBJECT

 public:
  explicit MockDownloadManager(QObject* parent = nullptr);
  ~MockDownloadManager() override = default;

  int fetch(const QUrl& url, const QString& expectedChecksum,
            const QString& destinationDir) override;

  void cancel(int id) override;

 private:
  struct MockOp {
    QTimer* timer;
    int tick = 0;
    static constexpr int kTicks = 10;
    static constexpr qint64 kTotalBytes = 1024 * 1024;  // 1 MiB (fake)
  };

  QMap<int, MockOp> ops_;
  int next_id_ = 1;
};

}  // namespace PJ
