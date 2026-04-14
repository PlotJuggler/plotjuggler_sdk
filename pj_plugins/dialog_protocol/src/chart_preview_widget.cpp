#include <pj_plugins/host_qt/chart_preview_widget.hpp>

#include <QColor>
#include <QPen>
#include <QPointF>
#include <QtCharts/QChart>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <limits>

namespace PJ {

namespace {
/// Default matplotlib "tab10" palette — 10 distinct colors.
const std::vector<QColor>& kDefaultPalette() {
  static const std::vector<QColor> kPalette = {
      QColor(0x1f, 0x77, 0xb4), QColor(0xff, 0x7f, 0x0e), QColor(0x2c, 0xa0, 0x2c),
      QColor(0xd6, 0x27, 0x28), QColor(0x94, 0x67, 0xbd), QColor(0x8c, 0x56, 0x4b),
      QColor(0xe3, 0x77, 0xc2), QColor(0x7f, 0x7f, 0x7f), QColor(0xbc, 0xbd, 0x22),
      QColor(0x17, 0xbe, 0xcf),
  };
  return kPalette;
}
}  // namespace

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

  const auto& palette = kDefaultPalette();

  for (size_t i = 0; i < series.size(); ++i) {
    const auto& s = series[i];
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
    // Color precedence:
    //  1. If the series carries an explicit hex color, apply it (after addSeries
    //     so Qt's theme assignment doesn't win).
    //  2. Otherwise leave whatever Qt Charts chose from the active theme.
    if (!s.color.empty()) {
      QColor c(QString::fromStdString(s.color));
      if (c.isValid()) {
        QPen pen(c);
        pen.setWidthF(1.4);
        line->setPen(pen);
      }
    }
  }
  (void)palette;  // palette reserved for future per-series override API

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

  // Interactive legend: click a marker to toggle its series visibility.
  const auto markers = chart()->legend()->markers();
  for (auto* marker : markers) {
    QObject::disconnect(marker, nullptr, this, nullptr);
    QObject::connect(marker, &QLegendMarker::clicked, this, [marker]() {
      auto* s = marker->series();
      if (s) {
        s->setVisible(!s->isVisible());
        // Fade the legend label when series is hidden.
        QColor color = marker->labelBrush().color();
        color.setAlphaF(s->isVisible() ? 1.0F : 0.4F);
        marker->setLabelBrush(QBrush(color));
      }
    });
  }
}

void ChartPreviewWidget::clearSeries() {
  chart()->removeAllSeries();
}

}  // namespace PJ
