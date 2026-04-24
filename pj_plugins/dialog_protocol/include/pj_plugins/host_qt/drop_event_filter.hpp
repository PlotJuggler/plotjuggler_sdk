#pragma once

#include <QApplication>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QWidget>
#include <pj_plugins/host/widget_event_builder.hpp>
#include <pj_plugins/host_qt/widget_binding.hpp>
#include <set>
#include <string>
#include <vector>

namespace PJ {

/// Single event filter installed on the dialog root that handles drag-and-drop
/// of PJ fields. Registered widgets are tracked by objectName. When a drop
/// lands on (or inside) a registered widget, the callback fires an itemsDropped event.
class DropEventFilter : public QObject {
  Q_OBJECT

 public:
  DropEventFilter(QWidget* dialog_root, WidgetEventCallback callback)
      : QObject(dialog_root), root_(dialog_root), callback_(std::move(callback)) {
    root_->setAcceptDrops(true);
    root_->installEventFilter(this);
  }

  void addTarget(const std::string& widget_name) { targets_.insert(widget_name); }

 protected:
  bool eventFilter(QObject* /*obj*/, QEvent* event) override {
    if (event->type() == QEvent::DragEnter) {
      auto* e = static_cast<QDragEnterEvent*>(event);
      if (e->mimeData()->hasFormat(kPjFieldMime)) {
        e->acceptProposedAction();
        return true;
      }
    } else if (event->type() == QEvent::DragMove) {
      auto* e = static_cast<QDragMoveEvent*>(event);
      if (e->mimeData()->hasFormat(kPjFieldMime)) {
        // Only accept if the cursor is over a registered target.
        if (findTargetAt(e->position().toPoint())) {
          e->acceptProposedAction();
        } else {
          e->ignore();
        }
        return true;
      }
    } else if (event->type() == QEvent::Drop) {
      auto* e = static_cast<QDropEvent*>(event);
      auto* target_name = findTargetAt(e->position().toPoint());
      if (target_name && e->mimeData()->hasFormat(kPjFieldMime)) {
        auto labels = parseMime(e->mimeData()->data(kPjFieldMime));
        if (!labels.empty()) {
          callback_(*target_name, WidgetEventBuilder::itemsDropped(labels));
          e->acceptProposedAction();
          return true;
        }
      }
    }
    return false;
  }

 private:
  static constexpr const char* kPjFieldMime = "application/x-pj-field";

  QWidget* root_;
  WidgetEventCallback callback_;
  std::set<std::string> targets_;

  /// Walk up from the widget at pos to find a registered drop target.
  const std::string* findTargetAt(QPoint pos) const {
    auto* w = root_->childAt(pos);
    while (w && w != root_) {
      auto name = w->objectName().toStdString();
      auto it = targets_.find(name);
      if (it != targets_.end()) {
        return &(*it);
      }
      w = w->parentWidget();
    }
    return nullptr;
  }

  static std::vector<std::string> parseMime(const QByteArray& data) {
    QDataStream stream(data);
    quint32 count = 0;
    stream >> count;

    std::vector<std::string> labels;
    labels.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
      quint32 topic_id = 0;
      quint32 col_index = 0;
      QString label;
      stream >> topic_id >> col_index >> label;
      if (stream.status() != QDataStream::Ok) {
        break;
      }
      labels.push_back(label.toStdString());
    }
    return labels;
  }
};

}  // namespace PJ
