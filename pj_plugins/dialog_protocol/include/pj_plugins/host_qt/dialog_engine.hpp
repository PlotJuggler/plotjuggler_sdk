#pragma once

#include <QWidget>
#include <pj_plugins/host/dialog_handle.hpp>
#include <string>

namespace PJ {

/// Result of showing a dialog.
enum class DialogResult { kAccepted, kRejected };

/// Configuration for DialogEngine.
struct DialogEngineConfig {
  int tick_interval_ms = 50;
  bool enable_diff = true;         // Only apply changed widgets on tick
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
  explicit DialogEngine(PJ::DialogHandle handle, DialogEngineConfig config = {});

  /// Show the plugin's dialog modally. Returns the result.
  [[nodiscard]] DialogResult showDialog(QWidget* parent = nullptr);

  /// Run plugin headlessly (no UI): pump N ticks, return final widget_data JSON.
  [[nodiscard]] std::string runHeadless(int max_ticks);

  /// Return the plugin's current saved config.
  [[nodiscard]] std::string savedConfig() const;

  /// Stats from the last showDialog() call.
  struct Stats {
    int tick_count = 0;
    int event_count = 0;
    int diff_apply_count = 0;
    bool has_parser_slot = false;
  };
  [[nodiscard]] Stats lastStats() const {
    return stats_;
  }

 private:
  PJ::DialogHandle handle_;
  DialogEngineConfig config_;
  Stats stats_;
};

}  // namespace PJ
