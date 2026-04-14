#pragma once

#include <QtCharts/QChartView>
#include <string>
#include <utility>
#include <vector>

QT_BEGIN_NAMESPACE
class QValueAxis;
QT_END_NAMESPACE

namespace PJ {

/// Lightweight chart widget that renders named XY line series inside a QFrame.
/// Created and managed by the widget binding layer — plugin authors never touch this directly.
class ChartPreviewWidget : public QChartView {
  Q_OBJECT

 public:
  explicit ChartPreviewWidget(QWidget* parent = nullptr);

  struct Series {
    std::string label;
    std::vector<std::pair<double, double>> points;
    std::string color;  // optional hex "#rrggbb"; empty means use chart theme default
  };

  void setSeries(const std::vector<Series>& series);
  void clearSeries();

 private:
  QValueAxis* x_axis_ = nullptr;
  QValueAxis* y_axis_ = nullptr;
};

}  // namespace PJ
