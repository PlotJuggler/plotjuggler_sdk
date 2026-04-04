#include "chart_panel.hpp"

#include <QContextMenuEvent>
#include <QDataStream>
#include <QMimeData>
#include <cmath>
#include <limits>

#include "pj_datastore/query.hpp"
#include "pj_datastore/reader.hpp"

namespace proto {

ChartPanel::ChartPanel(const PJ::DataEngine& engine, QWidget* parent)
    : QChartView(new QChart(), parent), engine_(engine) {
  setAcceptDrops(true);
  setRenderHint(QPainter::Antialiasing);

  x_axis_ = new QValueAxis();
  x_axis_->setTitleText("Time (s)");
  chart()->addAxis(x_axis_, Qt::AlignBottom);

  y_axis_ = new QValueAxis();
  y_axis_->setTitleText("Value");
  chart()->addAxis(y_axis_, Qt::AlignLeft);

  chart()->legend()->setVisible(true);
  chart()->legend()->setAlignment(Qt::AlignBottom);
}

void ChartPanel::addSeries(PJ::TopicId topic_id, size_t col_index, const std::string& label) {
  auto* line = new QLineSeries();
  line->setName(QString::fromStdString(label));

  chart()->addSeries(line);
  line->attachAxis(x_axis_);
  line->attachAxis(y_axis_);

  series_.push_back(PlottedSeries{line, topic_id, col_index, label});
}

void ChartPanel::removeSeries(int index) {
  if (index < 0 || static_cast<size_t>(index) >= series_.size()) {
    return;
  }
  chart()->removeSeries(series_[static_cast<size_t>(index)].line);
  series_.erase(series_.begin() + index);
}

void ChartPanel::clearAllSeries() {
  for (auto& s : series_) {
    chart()->removeSeries(s.line);
  }
  series_.clear();
  first_timestamp_ = 0;
}

void ChartPanel::updateData(PJ::Timestamp t_min, PJ::Timestamp t_max) {
  if (series_.empty()) {
    return;
  }

  auto reader = engine_.createReader();

  // Detect first_timestamp for relative display
  if (first_timestamp_ == 0 && t_min > 0) {
    first_timestamp_ = t_min;
  }

  double y_min = std::numeric_limits<double>::max();
  double y_max = std::numeric_limits<double>::lowest();

  for (auto& s : series_) {
    auto cursor_result = reader.rangeQuery(PJ::QueryRange{s.topic_id, t_min, t_max});
    if (!cursor_result) {
      s.line->clear();
      continue;
    }

    QList<QPointF> points;
    cursor_result->forEachChunk([&](const PJ::ChunkRowRange& range) {
      auto row_count = range.row_end - range.row_start;
      std::vector<PJ::Timestamp> timestamps(row_count);
      std::vector<double> values(row_count);

      // Guard: chunk may have fewer columns than the current schema
      if (s.col_index >= range.chunk->columns.size()) {
        return;
      }

      range.chunk->readTimestamps(PJ::Span<PJ::Timestamp>(timestamps), range.row_start);
      range.chunk->readColumnAsDoubles(s.col_index, PJ::Span<double>(values), range.row_start);

      for (size_t i = 0; i < row_count; ++i) {
        if (range.chunk->isNull(s.col_index, range.row_start + i)) {
          continue;
        }
        double x = static_cast<double>(timestamps[i] - first_timestamp_) / 1e9;
        double y = values[i];
        points.append(QPointF(x, y));
        if (y < y_min) {
          y_min = y;
        }
        if (y > y_max) {
          y_max = y;
        }
      }
    });

    s.line->replace(points);
  }

  // Auto-scale x-axis (skipped when user has manually zoomed or panned)
  if (!user_zoom_ && !user_panned_) {
    double x_min_s = static_cast<double>(t_min - first_timestamp_) / 1e9;
    double x_max_s = static_cast<double>(t_max - first_timestamp_) / 1e9;
    x_axis_->setRange(x_min_s, x_max_s);
  }

  if (y_min < y_max) {
    double margin = (y_max - y_min) * 0.05;
    if (margin == 0.0) {
      margin = 1.0;
    }
    y_axis_->setRange(y_min - margin, y_max + margin);
  }
}

void ChartPanel::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasFormat("application/x-pj-field")) {
    event->acceptProposedAction();
  }
}

void ChartPanel::dragMoveEvent(QDragMoveEvent* event) {
  if (event->mimeData()->hasFormat("application/x-pj-field")) {
    event->acceptProposedAction();
  }
}

void ChartPanel::dropEvent(QDropEvent* event) {
  auto field_data = event->mimeData()->data("application/x-pj-field");
  QDataStream stream(field_data);

  quint32 count = 0;
  stream >> count;

  for (quint32 i = 0; i < count; ++i) {
    quint32 topic_id = 0;
    quint32 col_index = 0;
    QString label;
    stream >> topic_id >> col_index >> label;
    if (stream.status() != QDataStream::Ok) {
      break;
    }
    addSeries(topic_id, col_index, label.toStdString());
  }

  event->acceptProposedAction();

  // Trigger an immediate chart update after adding the series.
  emit seriesDropped();
}

void ChartPanel::wheelEvent(QWheelEvent* event) {
  double factor = (event->angleDelta().y() > 0) ? 0.8 : 1.25;
  QPointF scene_pos = mapToScene(event->position().toPoint());
  double mouse_x_s = chart()->mapToValue(scene_pos).x();
  double x_min = x_axis_->min();
  double x_max = x_axis_->max();
  x_axis_->setRange(mouse_x_s - (mouse_x_s - x_min) * factor, mouse_x_s + (x_max - mouse_x_s) * factor);
  user_zoom_ = true;
  event->accept();
}

void ChartPanel::mouseDoubleClickEvent(QMouseEvent* event) {
  user_zoom_ = false;
  user_panned_ = false;
  emit seriesDropped();  // triggers MainWindow to redraw with the full data range
  QChartView::mouseDoubleClickEvent(event);
}

void ChartPanel::contextMenuEvent(QContextMenuEvent* event) {
  if (series_.empty()) {
    QChartView::contextMenuEvent(event);
    return;
  }

  QMenu menu(this);
  for (size_t i = 0; i < series_.size(); ++i) {
    auto* action = menu.addAction("Remove: " + QString::fromStdString(series_[i].label));
    connect(action, &QAction::triggered, this, [this, i]() { removeSeries(static_cast<int>(i)); });
  }
  menu.exec(event->globalPos());
}

void ChartPanel::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton) {
    is_panning_ = true;
    last_pan_pos_ = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QChartView::mousePressEvent(event);
}

void ChartPanel::mouseMoveEvent(QMouseEvent* event) {
  if (is_panning_) {
    QPoint delta = event->pos() - last_pan_pos_;
    chart()->scroll(-delta.x(), delta.y());
    last_pan_pos_ = event->pos();
    user_panned_ = true;
    event->accept();
    return;
  }
  QChartView::mouseMoveEvent(event);
}

void ChartPanel::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton && is_panning_) {
    is_panning_ = false;
    setCursor(Qt::ArrowCursor);
    event->accept();
    return;
  }
  QChartView::mouseReleaseEvent(event);
}

}  // namespace proto
