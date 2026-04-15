#pragma once

#include <QtCharts/QChartView>
#include <string>
#include <utility>
#include <vector>

QT_BEGIN_NAMESPACE
class QValueAxis;
class QWheelEvent;
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

  /// Enable or disable interactive zoom (rubber band + mouse wheel).
  /// When enabled, viewChanged() is emitted whenever the user zooms or pans.
  void setZoomEnabled(bool enabled);

 signals:
  /// Emitted when the visible axes range changes due to user zoom or pan.
  /// Only emitted when zoom is enabled via setZoomEnabled(true).
  void viewChanged(double x_min, double x_max, double y_min, double y_max);

 protected:
  void wheelEvent(QWheelEvent* event) override;

 private:
  QValueAxis* x_axis_ = nullptr;
  QValueAxis* y_axis_ = nullptr;
  bool zoom_enabled_ = false;

  void emitViewChanged();
};

}  // namespace PJ
