#pragma once

#include <QAction>
#include <QDateTime>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QTreeView>
#include <memory>
#include <vector>

#include "chart_panel.hpp"
#include "data_source_session.hpp"
#include "pj_datastore/colormap_registry.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_marketplace/qt_diagnostic_bridge.hpp"
#include "plugin_registry.hpp"
#include "series_tree_model.hpp"
#include "toolbox_session.hpp"

#include "pj_marketplace/extension_manager.hpp"

namespace proto {

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(const std::string& plugin_dir, QWidget* parent = nullptr);

  /// Load a file programmatically (used by --load CLI option).
  void loadFile(const QString& file_path);

  /// Plot the first N fields from the loaded data (used by --plot CLI option).
  void plotFirstFields(int count);

  /// Start the "Dummy Streamer" plugin programmatically (used by --dummy-stream CLI option).
  void startDummyStream();

 private slots:
  void onLoadFile();
  void onStartStream();
  void onOpenMarketplace();
  void onClearData();
  void onClearPlots();
  void onRefreshTimer();
  void onTreeContextMenu(const QPoint& pos);
  void onDiagnosticReported(int level, QString source, QString id, QString message);
  void onShowDiagnosticsDialog();

 private:
  void setupToolboxPanels(QMenu* tools_menu);

  /// Compute the current visible time range based on data and streaming state.
  std::pair<PJ::Timestamp, PJ::Timestamp> computeVisibleRange() const;

  /// Find the session owning @p dataset_id, or nullptr.
  DataSourceSession* findSessionForDataset(PJ::DatasetId dataset_id);

  void removeSession(DataSourceSession* session);
  void restartSession(DataSourceSession* session);

  PJ::DataEngine engine_;
  PJ::ColorMapRegistry colormap_registry_;
  PJ::TimeDomainId default_td_id_ = 0;
  PJ::QtDiagnosticBridge* diag_bridge_ = nullptr;
  std::unique_ptr<PluginRegistry> registry_;
  std::vector<std::unique_ptr<DataSourceSession>> sessions_;
  SeriesTreeModel tree_model_;

  // Recent diagnostics, capped, shown in the Diagnostics dialog.
  struct UiDiagnostic {
    int level;
    QString source;
    QString id;
    QString message;
    QDateTime timestamp;
  };
  QList<UiDiagnostic> diagnostics_;
  QAction* diagnostics_action_ = nullptr;

  std::unique_ptr<PJ::ExtensionManager> ext_mgr_;

  QTreeView* tree_view_ = nullptr;
  ChartPanel* chart_panel_ = nullptr;
  QSpinBox* buffer_spinbox_ = nullptr;
  QPushButton* btn_marketplace_ = nullptr;
  QMenu* tools_menu_ = nullptr;
  QTimer refresh_timer_;
  int refresh_tick_ = 0;
  bool streaming_active_ = false;
  int open_toolbox_dialogs_ = 0;

  std::vector<std::unique_ptr<ToolboxSession>> toolbox_sessions_;
};

}  // namespace proto
