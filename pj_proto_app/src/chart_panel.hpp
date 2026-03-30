#pragma once

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <string>
#include <vector>

#include "pj_base/types.hpp"
#include "pj_datastore/engine.hpp"

namespace proto {

struct PlottedSeries {
  QLineSeries* line = nullptr;
  PJ::TopicId topic_id = 0;
  size_t col_index = 0;
  std::string label;
};

class ChartPanel : public QChartView {
  Q_OBJECT

 public:
  explicit ChartPanel(const PJ::DataEngine& engine, QWidget* parent = nullptr);

  void addSeries(PJ::TopicId topic_id, size_t col_index, const std::string& label);
  void removeSeries(int index);
  void clearAllSeries();
  void updateData(PJ::Timestamp t_min, PJ::Timestamp t_max);

 signals:
  void seriesDropped();

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  const PJ::DataEngine& engine_;
  QValueAxis* x_axis_;
  QValueAxis* y_axis_;
  std::vector<PlottedSeries> series_;
  PJ::Timestamp first_timestamp_ = 0;
  bool user_zoom_ = false;
};

}  // namespace proto
