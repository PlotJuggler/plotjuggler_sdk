#include <pj_plugins/host_qt/chart_preview_widget.hpp>

#include <QPointF>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <limits>

namespace PJ {

ChartPreviewWidget::ChartPreviewWidget(QWidget* parent) : QChartView(new QChart(), parent) {
  setRenderHint(QPainter::Antialiasing);

  x_axis_ = new QValueAxis();
  y_axis_ = new QValueAxis();
  chart()->addAxis(x_axis_, Qt::AlignBottom);
  chart()->addAxis(y_axis_, Qt::AlignLeft);

  chart()->legend()->setVisible(true);
  chart()->legend()->setAlignment(Qt::AlignBottom);
  chart()->setMargins(QMargins(4, 4, 4, 4));
}

void ChartPreviewWidget::setSeries(const std::vector<Series>& series) {
  chart()->removeAllSeries();

  double x_min = std::numeric_limits<double>::max();
  double x_max = std::numeric_limits<double>::lowest();
  double y_min = std::numeric_limits<double>::max();
  double y_max = std::numeric_limits<double>::lowest();

  for (const auto& s : series) {
    auto* line = new QLineSeries();
    line->setName(QString::fromStdString(s.label));

    QList<QPointF> points;
    points.reserve(static_cast<int>(s.points.size()));
    for (const auto& [x, y] : s.points) {
      points.append(QPointF(x, y));
      if (x < x_min) x_min = x;
      if (x > x_max) x_max = x;
      if (y < y_min) y_min = y;
      if (y > y_max) y_max = y;
    }
    line->replace(points);

    chart()->addSeries(line);
    line->attachAxis(x_axis_);
    line->attachAxis(y_axis_);
  }

  if (x_min < x_max) {
    x_axis_->setRange(x_min, x_max);
  }
  if (y_min < y_max) {
    double margin = (y_max - y_min) * 0.05;
    if (margin == 0.0) {
      margin = 1.0;
    }
    y_axis_->setRange(y_min - margin, y_max + margin);
  }
}

void ChartPreviewWidget::clearSeries() {
  chart()->removeAllSeries();
}

}  // namespace PJ
