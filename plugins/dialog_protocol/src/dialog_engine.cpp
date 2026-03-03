#include <PJ/host_qt/dialog_engine.hpp>

#include <PJ/host/widget_data_view.hpp>
#include <PJ/host/widget_event_builder.hpp>
#include <PJ/host_qt/widget_binding.hpp>

#include <QBuffer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QTimer>
#include <QUiLoader>
#include <QVBoxLayout>

namespace PJ::host_qt {

DialogEngine::DialogEngine(PJ::host::DialogHandle handle, DialogEngineConfig config)
    : handle_(std::move(handle)), config_(config) {}

// ---------------------------------------------------------------------------
// JSON diff: compute which widget keys changed between old and new data
// ---------------------------------------------------------------------------

static nlohmann::json compute_diff(const nlohmann::json& old_data,
                                   const nlohmann::json& new_data) {
  nlohmann::json diff = nlohmann::json::object();
  for (const auto& [key, val] : new_data.items()) {
    if (!old_data.contains(key) || old_data[key] != val) {
      diff[key] = val;
    }
  }
  return diff;
}

// ---------------------------------------------------------------------------
// apply_and_diff: re-read widget data, apply (diffed or full), update prev
// ---------------------------------------------------------------------------

static void apply_and_diff(QWidget* root, PJ::host::DialogHandle& handle,
                           nlohmann::json& prev_data, bool enable_diff,
                           int& diff_apply_count) {
  std::string raw = handle.widget_data();
  nlohmann::json new_data = nlohmann::json::parse(raw, nullptr, false);
  if (new_data.is_discarded()) return;

  if (enable_diff) {
    nlohmann::json diff = compute_diff(prev_data, new_data);
    if (!diff.empty()) {
      PJ::host::WidgetDataView view(diff.dump());
      apply_widget_data(root, view);
      ++diff_apply_count;
    }
  } else {
    PJ::host::WidgetDataView view(raw);
    apply_widget_data(root, view);
  }
  prev_data = std::move(new_data);
}

// ---------------------------------------------------------------------------
// show_dialog
// ---------------------------------------------------------------------------

DialogResult DialogEngine::show_dialog(QWidget* parent) {
  stats_ = {};

  // 1. Load .ui
  std::string ui = handle_.ui_content();
  QByteArray data(ui.data(), static_cast<int>(ui.size()));
  QBuffer buffer(&data);
  buffer.open(QIODevice::ReadOnly);

  QUiLoader loader;
  QWidget* loaded = loader.load(&buffer, parent);
  if (!loaded) return DialogResult::kRejected;

  // 2. Wrap in QDialog if needed
  QDialog* dialog = qobject_cast<QDialog*>(loaded);
  if (!dialog) {
    dialog = new QDialog(parent);
    dialog->setWindowTitle(loaded->windowTitle());
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(loaded);

    auto* button_box = loaded->findChild<QDialogButtonBox*>("buttonBox");
    if (button_box) {
      QObject::connect(button_box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
      QObject::connect(button_box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    }
  }

  QWidget* binding_root = loaded;

  // 3. Apply initial widget data
  std::string initial_raw = handle_.widget_data();
  nlohmann::json prev_data = nlohmann::json::parse(initial_raw, nullptr, false);
  if (prev_data.is_discarded()) prev_data = nlohmann::json::object();
  {
    PJ::host::WidgetDataView view(initial_raw);
    apply_widget_data(binding_root, view);
  }

  // 4. Handle file picker actions
  auto maybe_show_file_picker = [&](const std::string& widget_name) {
    if (!config_.enable_file_picker) return;
    PJ::host::WidgetDataView view(handle_.widget_data());
    if (!view.is_file_picker(widget_name)) return;
    auto filter = view.file_picker_filter(widget_name).value_or("");
    auto title = view.file_picker_title(widget_name).value_or("Select File");
    QString path = QFileDialog::getOpenFileName(
        dialog, QString::fromStdString(title), QString(),
        QString::fromStdString(filter));
    if (!path.isEmpty()) {
      if (handle_.send_event(widget_name,
                             PJ::host::WidgetEventBuilder::file_selected(path.toStdString()))) {
        apply_and_diff(binding_root, handle_, prev_data, config_.enable_diff,
                       stats_.diff_apply_count);
      }
    }
  };

  // 5. Wire signals
  connect_widget_signals(binding_root, [&](const std::string& name,
                                           const std::string& event_json) {
    stats_.event_count++;
    if (handle_.send_event(name, event_json)) {
      apply_and_diff(binding_root, handle_, prev_data, config_.enable_diff,
                     stats_.diff_apply_count);
    }
    maybe_show_file_picker(name);
  });

  // 6. Start tick timer
  QTimer tick_timer;
  tick_timer.setInterval(config_.tick_interval_ms);
  QObject::connect(&tick_timer, &QTimer::timeout, [&]() {
    stats_.tick_count++;
    if (handle_.tick()) {
      apply_and_diff(binding_root, handle_, prev_data, config_.enable_diff,
                     stats_.diff_apply_count);
    }
  });
  tick_timer.start();

  // 7. Run dialog
  int result = dialog->exec();
  tick_timer.stop();

  // 8. Notify plugin and clean up
  DialogResult dr;
  if (result == QDialog::Accepted) {
    handle_.accept(handle_.save_config());
    dr = DialogResult::kAccepted;
  } else {
    handle_.reject();
    dr = DialogResult::kRejected;
  }
  delete dialog;
  return dr;
}

// ---------------------------------------------------------------------------
// run_headless
// ---------------------------------------------------------------------------

std::string DialogEngine::run_headless(int max_ticks) {
  for (int i = 0; i < max_ticks; ++i) {
    (void)handle_.tick();
  }
  return handle_.widget_data();
}

}  // namespace PJ::host_qt
