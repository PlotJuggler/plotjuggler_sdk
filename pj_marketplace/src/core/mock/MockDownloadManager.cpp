#include "core/mock/MockDownloadManager.h"

namespace PJ {

MockDownloadManager::MockDownloadManager(QObject* parent) : DownloadManager(parent) {}

int MockDownloadManager::fetch(const QUrl& /*url*/, const QString& /*expectedChecksum*/,
                                const QString& /*destinationDir*/) {
  const int id = next_id_++;

  auto* timer = new QTimer(this);
  ops_[id] = MockOp{timer, 0};

  emit started(id);

  connect(timer, &QTimer::timeout, this, [this, id]() {
    auto it = ops_.find(id);
    if (it == ops_.end()) return;

    it->tick++;
    const qint64 received = (MockOp::kTotalBytes * it->tick) / MockOp::kTicks;
    emit progress(id, received, MockOp::kTotalBytes);

    if (it->tick >= MockOp::kTicks) {
      it->timer->stop();
      it->timer->deleteLater();
      ops_.erase(it);
      emit finished(id);
    }
  });

  timer->start(100);
  return id;
}

void MockDownloadManager::cancel(int id) {
  auto it = ops_.find(id);
  if (it == ops_.end()) return;

  it->timer->stop();
  it->timer->deleteLater();
  ops_.erase(it);
  emit cancelled(id);
}

}  // namespace PJ
