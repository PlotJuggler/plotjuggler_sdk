#pragma once

#include <PJ/host/dialog_handle.hpp>

#include <QWidget>

#include <string>

namespace PJ::host_qt {

/// Result of showing a dialog.
enum class DialogResult { kAccepted, kRejected };

/// Configuration for DialogEngine.
struct DialogEngineConfig {
  int tick_interval_ms = 50;
  bool enable_diff = true;        // Only apply changed widgets on tick
  bool enable_file_picker = true;  // Show QFileDialog for file_picker actions
};

/// Orchestrates the full dialog lifecycle for a plugin:
///   1. Load .ui via QUiLoader
///   2. Wrap in QDialog, wire QDialogButtonBox
///   3. Apply initial get_widget_data()
///   4. Wire signals -> on_widget_event
///   5. Start tick timer -> on_tick -> diff apply
///   6. dialog->exec()
///   7. Call on_accepted / on_rejected
class DialogEngine {
 public:
  explicit DialogEngine(PJ::host::DialogHandle handle, DialogEngineConfig config = {});

  /// Show the plugin's dialog modally. Returns the result.
  [[nodiscard]] DialogResult show_dialog(QWidget* parent = nullptr);

  /// Run plugin headlessly (no UI): pump N ticks, return final widget_data JSON.
  [[nodiscard]] std::string run_headless(int max_ticks);

  /// Stats from the last show_dialog() call.
  struct Stats {
    int tick_count = 0;
    int event_count = 0;
    int diff_apply_count = 0;
  };
  [[nodiscard]] Stats last_stats() const { return stats_; }

 private:
  PJ::host::DialogHandle handle_;
  DialogEngineConfig config_;
  Stats stats_;
};

}  // namespace PJ::host_qt
