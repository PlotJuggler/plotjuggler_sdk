#include "main_window.hpp"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>

#include "pj_marketplace/extension_manager.hpp"
#include "pj_marketplace/marketplace_window.hpp"


#include "pj_datastore/reader.hpp"
#include "pj_plugins/host_qt/dialog_engine.hpp"

namespace proto {

namespace {

/// Creates a callback that shows a QMessageBox.
/// Assumes caller is on GUI thread (protoapp calls plugin methods from GUI thread).
ShowMessageBoxCallback makeMessageBoxCallback(QWidget* parent) {
  return [parent](PJ_message_box_type_t type, std::string_view title, std::string_view message, int buttons) -> int {
    QString q_title = QString::fromUtf8(title.data(), static_cast<qsizetype>(title.size()));
    QString q_message = QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size()));

    // Use manual QMessageBox to support custom button text (e.g., "Continue")
    QMessageBox msg_box(parent);
    msg_box.setWindowTitle(q_title);
    msg_box.setText(q_message);

    // Set icon based on type
    switch (type) {
      case PJ_MESSAGE_BOX_WARNING:
        msg_box.setIcon(QMessageBox::Warning);
        break;
      case PJ_MESSAGE_BOX_ERROR:
        msg_box.setIcon(QMessageBox::Critical);
        break;
      case PJ_MESSAGE_BOX_QUESTION:
        msg_box.setIcon(QMessageBox::Question);
        break;
      case PJ_MESSAGE_BOX_INFO:
      default:
        msg_box.setIcon(QMessageBox::Information);
        break;
    }

    // Map to track custom buttons -> PJ constants
    std::vector<std::pair<QPushButton*, int>> button_map;

    auto add_button = [&](int pj_btn, const QString& text, QMessageBox::ButtonRole role) {
      if (buttons & pj_btn) {
        auto* btn = msg_box.addButton(text, role);
        button_map.emplace_back(btn, pj_btn);
      }
    };

    if (buttons == 0) {
      auto* btn = msg_box.addButton(QMessageBox::Ok);
      button_map.emplace_back(btn, PJ_MSG_BTN_OK);
    } else {
      // Add buttons with proper labels
      add_button(PJ_MSG_BTN_OK, QStringLiteral("OK"), QMessageBox::AcceptRole);
      add_button(PJ_MSG_BTN_CANCEL, QStringLiteral("Cancel"), QMessageBox::RejectRole);
      add_button(PJ_MSG_BTN_YES, QStringLiteral("Yes"), QMessageBox::YesRole);
      add_button(PJ_MSG_BTN_NO, QStringLiteral("No"), QMessageBox::NoRole);
      add_button(PJ_MSG_BTN_ABORT, QStringLiteral("Abort"), QMessageBox::RejectRole);
      add_button(PJ_MSG_BTN_RETRY, QStringLiteral("Retry"), QMessageBox::AcceptRole);
      add_button(PJ_MSG_BTN_IGNORE, QStringLiteral("Ignore"), QMessageBox::AcceptRole);
      add_button(PJ_MSG_BTN_CONTINUE, QStringLiteral("Continue"), QMessageBox::AcceptRole);
    }

    msg_box.exec();

    // Find which button was clicked
    QAbstractButton* clicked = msg_box.clickedButton();
    for (const auto& [btn, pj_val] : button_map) {
      if (btn == clicked) {
        return pj_val;
      }
    }

    return PJ_MSG_BTN_OK;  // fallback
  };
}

}  // namespace

MainWindow::MainWindow(const std::string& plugin_dir, QWidget* parent)
    : QMainWindow(parent), registry_(plugin_dir), tree_model_(engine_) {
  auto td_result = engine_.createTimeDomain("default");
  if (td_result) {
    default_td_id_ = *td_result;
  }

  ext_mgr_ = std::make_unique<PJ::ExtensionManager>();
  ext_mgr_->applyPendingUninstalls();
  registry_.scanDirectory();
  ext_mgr_ = std::make_unique<PJ::ExtensionManager>();

  // --- Toolbar ---
  auto* toolbar = addToolBar("Main");
  toolbar->setMovable(false);

  auto* btn_load = new QPushButton("Load File");
  auto* btn_stream = new QPushButton("Start Stream");
  auto* btn_marketplace = new QPushButton("Marketplace");
  auto* btn_clear_data = new QPushButton("Clear Data");
  auto* btn_clear_plots = new QPushButton("Clear Plots");

  toolbar->addWidget(btn_load);
  toolbar->addWidget(btn_stream);
  toolbar->addWidget(btn_marketplace);
  toolbar->addSeparator();
  toolbar->addWidget(btn_clear_data);
  toolbar->addWidget(btn_clear_plots);
  toolbar->addSeparator();

  // Streaming buffer size control
  toolbar->addWidget(new QLabel("  Buffer (s): "));
  buffer_spinbox_ = new QSpinBox();
  buffer_spinbox_->setRange(5, 3600);
  buffer_spinbox_->setValue(60);
  buffer_spinbox_->setToolTip("Streaming buffer size in seconds. Old data is evicted.");
  toolbar->addWidget(buffer_spinbox_);

  connect(btn_load, &QPushButton::clicked, this, &MainWindow::onLoadFile);
  connect(btn_stream, &QPushButton::clicked, this, &MainWindow::onStartStream);
  connect(btn_marketplace, &QPushButton::clicked, this, &MainWindow::onOpenMarketplace);
  connect(btn_clear_data, &QPushButton::clicked, this, &MainWindow::onClearData);
  connect(btn_clear_plots, &QPushButton::clicked, this, &MainWindow::onClearPlots);

  // --- Layout ---
  tree_view_ = new QTreeView();
  tree_view_->setModel(&tree_model_);
  tree_view_->setDragEnabled(true);
  tree_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree_view_->setHeaderHidden(true);
  tree_view_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tree_view_, &QTreeView::customContextMenuRequested, this, &MainWindow::onTreeContextMenu);

  chart_panel_ = new ChartPanel(engine_);

  auto* splitter = new QSplitter(Qt::Horizontal);
  splitter->addWidget(tree_view_);
  splitter->addWidget(chart_panel_);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({250, 950});

  auto* central = new QWidget();
  auto* layout = new QVBoxLayout(central);
  layout->addWidget(splitter, 1);
  setCentralWidget(central);

  // --- Signals ---
  connect(chart_panel_, &ChartPanel::seriesDropped, this, [this]() {
    auto [begin, end] = computeVisibleRange();
    chart_panel_->updateData(begin, end);
  });
  connect(&refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);

  setWindowTitle("PlotJuggler Proto");
}

void MainWindow::loadFile(const QString& file_path) {
  auto ext = QFileInfo(file_path).suffix();
  if (!ext.isEmpty()) {
    ext = "." + ext;
  }
  auto sources = registry_.findSourcesForExtension(ext.toStdString());

  if (sources.empty()) {
    qWarning("No DataSource plugin handles %s files", qPrintable(ext));
    return;
  }

  LoadedDataSource* source = sources[0];
  std::string config = R"({"filepath":")" + file_path.toStdString() + R"("})";

  auto display_name = QFileInfo(file_path).fileName().toStdString();

  auto session =
      std::make_unique<DataSourceSession>(engine_, source->library, default_td_id_, display_name, &registry_, this);
  session->setMessageBoxCallback(makeMessageBoxCallback(this));
  if (!session->startFileImport(config)) {
    qWarning("Import failed for '%s': %s", display_name.c_str(), session->lastError().c_str());
  }
  sessions_.push_back(std::move(session));

  tree_model_.rebuild();
  tree_view_->expandAll();
  auto [begin, end] = computeVisibleRange();
  chart_panel_->updateData(begin, end);
}

void MainWindow::plotFirstFields(int count) {
  int added = 0;
  auto reader = engine_.createReader();
  for (auto ds_id : reader.listDatasets()) {
    for (auto topic_id : reader.listTopics(ds_id)) {
      auto* storage = engine_.getTopicStorage(topic_id);
      if (!storage) {
        continue;
      }
      const auto& col_descs = storage->columnDescriptors();
      for (size_t i = 0; i < col_descs.size() && added < count; ++i) {
        chart_panel_->addSeries(topic_id, i, col_descs[i].field_path);
        added++;
      }
    }
  }
  if (added > 0) {
    auto [begin, end] = computeVisibleRange();
    chart_panel_->updateData(begin, end);
  }
}

void MainWindow::onLoadFile() {
  QSettings settings;
  auto last_dir = settings.value("ProtoApp/lastLoadDir", "").toString();
  auto filter = registry_.buildFileFilter();
  auto file_path = QFileDialog::getOpenFileName(this, "Load File", last_dir, QString::fromStdString(filter));
  if (file_path.isEmpty()) {
    return;
  }
  settings.setValue("ProtoApp/lastLoadDir", QFileInfo(file_path).absolutePath());

  auto ext = QFileInfo(file_path).suffix();
  if (!ext.isEmpty()) {
    ext = "." + ext;
  }
  auto sources = registry_.findSourcesForExtension(ext.toStdString());

  if (sources.empty()) {
    QMessageBox::warning(this, "No Plugin", "No DataSource plugin handles " + ext + " files.");
    return;
  }

  LoadedDataSource* source = sources[0];
  if (sources.size() > 1) {
    QStringList names;
    for (auto* s : sources) {
      names << QString::fromStdString(s->name);
    }
    bool ok = false;
    auto picked = QInputDialog::getItem(this, "Select DataSource", "Multiple plugins match:", names, 0, false, &ok);
    if (!ok) {
      return;
    }
    auto idx = static_cast<size_t>(names.indexOf(picked));
    source = sources[idx];
  }

  // Restore last-used config for this plugin (preserves user choices across sessions)
  auto settings_key = QString("PluginConfig/%1").arg(QString::fromStdString(source->name));
  std::string saved_config = settings.value(settings_key, "").toString().toStdString();

  // Merge the new filepath into the saved config (or create a fresh one)
  std::string config;
  {
    auto base = saved_config.empty() ? nlohmann::json::object() : nlohmann::json::parse(saved_config, nullptr, false);
    if (base.is_discarded()) {
      base = nlohmann::json::object();
    }
    base["filepath"] = file_path.toStdString();
    config = base.dump();
  }

  auto display_name = QFileInfo(file_path).fileName().toStdString();

  // Create session early so the dialog can call listAvailableEncodings()
  auto session =
      std::make_unique<DataSourceSession>(engine_, source->library, default_td_id_, display_name, &registry_, this);
  session->setMessageBoxCallback(makeMessageBoxCallback(this));

  // Bind runtime host early so the dialog can call listAvailableEncodings()
  session->bindRuntimeHostForDialog();

  // Load merged config (filepath + last-used settings) so the dialog is pre-populated
  (void)session->handle().loadConfig(config);

  // Dialog flow — use the session's own handle
  if ((source->capabilities & PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG) != 0) {
    auto vt_result = source->library.resolveDialogVtable();
    if (vt_result) {
      auto* dialog_ctx = session->handle().dialogContext();
      if (dialog_ctx != nullptr) {
        auto dialog_handle = PJ::DialogHandle::borrowed(*vt_result, dialog_ctx);
        PJ::DialogEngine dialog_engine(std::move(dialog_handle));
        if (dialog_engine.showDialog(this) == PJ::DialogResult::kRejected) {
          return;
        }
        config = dialog_engine.savedConfig();
      }
    }
  }

  session->startFileImport(config);
  sessions_.push_back(std::move(session));

  // Persist the config so next time the dialog opens with the same settings
  settings.setValue(settings_key, QString::fromStdString(config));

  tree_model_.rebuild();
  tree_view_->expandAll();
  auto [begin, end] = computeVisibleRange();
  chart_panel_->updateData(begin, end);
}

void MainWindow::onStartStream() {
  auto sources = registry_.streamSources();
  if (sources.empty()) {
    QMessageBox::warning(this, "No Plugin", "No streaming DataSource plugins found.");
    return;
  }

  LoadedDataSource* source = sources[0];
  if (sources.size() > 1) {
    QStringList names;
    for (auto* s : sources) {
      names << QString::fromStdString(s->name);
    }
    bool ok = false;
    auto picked = QInputDialog::getItem(this, "Select DataSource", "Choose stream source:", names, 0, false, &ok);
    if (!ok) {
      return;
    }
    auto idx = static_cast<size_t>(names.indexOf(picked));
    source = sources[idx];
  }

  QSettings settings;
  auto settings_key = QString("PluginConfig/%1").arg(QString::fromStdString(source->name));
  std::string saved_config = settings.value(settings_key, "").toString().toStdString();

  // Create session first so the dialog runs on the SAME handle that will stream.
  // This matches the original plugin architecture: one object, one socket.
  auto session =
      std::make_unique<DataSourceSession>(engine_, source->library, default_td_id_, source->name, &registry_, this);
  session->setMessageBoxCallback(makeMessageBoxCallback(this));

  // Bind runtime host early so the dialog can call listAvailableEncodings()
  session->bindRuntimeHostForDialog();

  // Restore saved config so the dialog remembers previous choices
  if (!saved_config.empty()) {
    (void)session->handle().loadConfig(saved_config);
  }

  // Dialog flow — use the session's own handle, not a temp handle
  std::string config = saved_config;
  if ((source->capabilities & PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG) != 0) {
    auto vt_result = source->library.resolveDialogVtable();
    if (vt_result) {
      auto* dialog_ctx = session->handle().dialogContext();
      if (dialog_ctx != nullptr) {
        auto dialog_handle = PJ::DialogHandle::borrowed(*vt_result, dialog_ctx);
        PJ::DialogEngine dialog_engine(std::move(dialog_handle));
        if (dialog_engine.showDialog(this) == PJ::DialogResult::kRejected) {
          return;
        }
        config = dialog_engine.savedConfig();
      }
    }
  }

  if (!session->startStream(config)) {
    QMessageBox::warning(this, "Stream Failed", QString::fromStdString(source->name + ": " + session->lastError()));
  }
  sessions_.push_back(std::move(session));

  // Persist the config
  settings.setValue(settings_key, QString::fromStdString(config));

  streaming_active_ = true;
  if (!refresh_timer_.isActive()) {
    refresh_timer_.start(33);  // ~30 Hz
  }
}

void MainWindow::startDummyStream() {
  auto sources = registry_.streamSources();
  LoadedDataSource* dummy = nullptr;
  for (auto* s : sources) {
    if (s->name == "Dummy Streamer") {
      dummy = s;
      break;
    }
  }
  if (dummy == nullptr) {
    qWarning("Dummy Streamer plugin not found in plugin directory");
    return;
  }

  auto session =
      std::make_unique<DataSourceSession>(engine_, dummy->library, default_td_id_, dummy->name, &registry_, this);
  session->setMessageBoxCallback(makeMessageBoxCallback(this));
  session->startStream("{}");
  sessions_.push_back(std::move(session));

  streaming_active_ = true;
  if (!refresh_timer_.isActive()) {
    refresh_timer_.start(33);  // ~30 Hz
  }

  tree_model_.rebuild();
  tree_view_->expandAll();
}

void MainWindow::onClearData() {
  // Stop all streaming sessions
  for (auto& session : sessions_) {
    session->requestStop();
    session->stopStream();
  }
  sessions_.clear();

  refresh_timer_.stop();
  streaming_active_ = false;

  // Reconstruct engine (no clear() API)
  engine_ = PJ::DataEngine();
  auto td_result = engine_.createTimeDomain("default");
  if (td_result) {
    default_td_id_ = *td_result;
  }

  tree_model_.rebuild();
  chart_panel_->clearAllSeries();
}

void MainWindow::onClearPlots() {
  chart_panel_->clearAllSeries();
}

void MainWindow::onRefreshTimer() {
  for (auto& session : sessions_) {
    session->poll();
  }

  // Enforce retention window for streaming: evict old data
  if (streaming_active_) {
    auto retention_seconds = static_cast<int64_t>(buffer_spinbox_->value());
    auto retention_ns = retention_seconds * 1'000'000'000LL;
    engine_.enforceRetention(retention_ns);
  }

  auto [begin, end] = computeVisibleRange();
  chart_panel_->updateData(begin, end);

  // Update state indicators for streaming sessions (every tick, cheap)
  for (auto& session : sessions_) {
    if (session->isStream()) {
      tree_model_.setDatasetState(session->datasetId(), session->currentState());
    }
  }

  refresh_tick_++;
  if (refresh_tick_ >= 30) {
    refresh_tick_ = 0;
    tree_model_.rebuildIfChanged();
  }
}

void MainWindow::onTreeContextMenu(const QPoint& pos) {
  auto index = tree_view_->indexAt(pos);
  if (!tree_model_.isDatasetNode(index)) {
    return;
  }

  auto dataset_id = tree_model_.datasetIdAt(index);
  auto* session = findSessionForDataset(dataset_id);
  if (session == nullptr) {
    return;
  }

  auto state = session->currentState();
  bool is_stream = session->isStream();
  bool supports_pause = session->supportsPause();

  QMenu menu;

  // All lambdas capture dataset_id (value) and re-lookup session at invocation time.
  // This is safe because QMenu::exec() runs a nested event loop during which
  // sessions could theoretically be modified.

  if (is_stream && state == PJ_DATA_SOURCE_STATE_RUNNING) {
    if (supports_pause) {
      menu.addAction("Pause", [this, dataset_id]() {
        auto* s = findSessionForDataset(dataset_id);
        if (!s) {
          return;
        }
        s->pauseStream();
        tree_model_.setDatasetState(dataset_id, s->currentState());
      });
    }
    menu.addAction("Stop", [this, dataset_id]() {
      auto* s = findSessionForDataset(dataset_id);
      if (!s) {
        return;
      }
      s->requestStop();
      s->stopStream();
      tree_model_.setDatasetState(dataset_id, s->currentState());
    });
  }

  if (is_stream && state == PJ_DATA_SOURCE_STATE_PAUSED) {
    menu.addAction("Resume", [this, dataset_id]() {
      auto* s = findSessionForDataset(dataset_id);
      if (!s) {
        return;
      }
      s->resumeStream();
      tree_model_.setDatasetState(dataset_id, s->currentState());
    });
    menu.addAction("Stop", [this, dataset_id]() {
      auto* s = findSessionForDataset(dataset_id);
      if (!s) {
        return;
      }
      s->requestStop();
      s->stopStream();
      tree_model_.setDatasetState(dataset_id, s->currentState());
    });
  }

  if (is_stream && (state == PJ_DATA_SOURCE_STATE_STOPPED || state == PJ_DATA_SOURCE_STATE_FAILED)) {
    menu.addAction("Restart", [this, dataset_id]() {
      auto* s = findSessionForDataset(dataset_id);
      if (!s) {
        return;
      }
      restartSession(s);
    });
  }

  menu.addSeparator();
  menu.addAction("Remove", [this, dataset_id]() {
    auto* s = findSessionForDataset(dataset_id);
    if (!s) {
      return;
    }
    removeSession(s);
  });

  if (!menu.actions().isEmpty()) {
    menu.exec(tree_view_->viewport()->mapToGlobal(pos));
  }
}

DataSourceSession* MainWindow::findSessionForDataset(PJ::DatasetId dataset_id) {
  for (auto& s : sessions_) {
    if (s->datasetId() == dataset_id) {
      return s.get();
    }
  }
  return nullptr;
}

void MainWindow::removeSession(DataSourceSession* session) {
  auto dataset_id = session->datasetId();

  // Stop if still running
  auto state = session->currentState();
  if (state == PJ_DATA_SOURCE_STATE_RUNNING || state == PJ_DATA_SOURCE_STATE_PAUSED) {
    session->requestStop();
    session->stopStream();
  }

  // Remove from sessions vector
  auto it = std::find_if(sessions_.begin(), sessions_.end(), [session](const auto& s) { return s.get() == session; });
  if (it != sessions_.end()) {
    sessions_.erase(it);
  }

  // Hide from tree (data stays in engine but becomes invisible)
  tree_model_.hideDataset(dataset_id);
  tree_model_.rebuild();

  // If no streaming sessions remain, stop the refresh timer
  bool any_streaming = std::any_of(sessions_.begin(), sessions_.end(), [](const auto& s) { return s->isStream(); });
  if (!any_streaming) {
    refresh_timer_.stop();
    streaming_active_ = false;
  }
}

void MainWindow::restartSession(DataSourceSession* session) {
  auto config = session->handle().saveConfig();
  auto& library = session->library();
  auto name = session->sourceName();
  auto dataset_id = session->datasetId();

  // Stop and remove the old session
  session->requestStop();
  session->stopStream();
  auto it = std::find_if(sessions_.begin(), sessions_.end(), [session](const auto& s) { return s.get() == session; });
  if (it != sessions_.end()) {
    sessions_.erase(it);
  }
  tree_model_.hideDataset(dataset_id);

  // Create and start a new session with the same config
  auto new_session = std::make_unique<DataSourceSession>(engine_, library, default_td_id_, name, &registry_, this);
  new_session->setMessageBoxCallback(makeMessageBoxCallback(this));
  new_session->startStream(config);
  sessions_.push_back(std::move(new_session));

  streaming_active_ = true;
  if (!refresh_timer_.isActive()) {
    refresh_timer_.start(33);
  }

  tree_model_.rebuild();
  tree_view_->expandAll();
}

std::pair<PJ::Timestamp, PJ::Timestamp> MainWindow::computeVisibleRange() const {
  auto reader = engine_.createReader();
  PJ::Timestamp global_min = std::numeric_limits<PJ::Timestamp>::max();
  PJ::Timestamp global_max = std::numeric_limits<PJ::Timestamp>::lowest();

  for (auto ds_id : reader.listDatasets()) {
    for (auto topic_id : reader.listTopics(ds_id)) {
      auto meta = reader.getMetadata(topic_id);
      if (meta && meta->total_row_count > 0) {
        global_min = std::min(global_min, meta->time_range_min);
        global_max = std::max(global_max, meta->time_range_max);
      }
    }
  }

  if (global_min >= global_max) {
    return {0, 1};
  }

  return {global_min, global_max};
}

void MainWindow::onOpenMarketplace() {
  const QUrl registry_url(
      "https://raw.githubusercontent.com/PlotJuggler/pj-plugin-registry/refs/heads/development/registry.json");
  PJ::MarketplaceWindow window(ext_mgr_.get(), registry_url, this);
  window.resize(700, 500);
  window.exec();
  if (window.installationsChanged()) {
    registry_.reload();
  }
}

}  // namespace proto
