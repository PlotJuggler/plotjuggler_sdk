#pragma once

#include <QWidget>
#include <pj_plugins/host/dialog_handle.hpp>
#include <functional>
#include <string>

namespace PJ {

/// Result of showing a dialog.
enum class DialogResult { kAccepted, kRejected };

/// Callback to resolve a parser's dialog vtable by encoding name.
/// Returns nullptr if no dialog is available for that encoding.
/// Used by DialogEngine to inject parser-specific options UI into data source dialogs.
using QueryParserDialogFn = std::function<const PJ_dialog_vtable_t*(const std::string& encoding)>;

/// Configuration for DialogEngine.
struct DialogEngineConfig {
  int tick_interval_ms = 50;
  bool enable_diff = true;         // Only apply changed widgets on tick
  bool enable_file_picker = true;  // Show QFileDialog for file_picker actions

  /// Optional callback to resolve parser dialog vtables.
  /// When set and the loaded UI contains a widget named "pj_parser_slot",
  /// the engine will inject the parser's dialog widget into that slot
  /// whenever the encoding combo (comboBoxProtocol) changes.
  QueryParserDialogFn parser_dialog_provider;

  /// Initial parser config to restore when injecting the parser dialog.
  /// If non-empty, the parser dialog's loadConfig() is called with this.
  std::string initial_parser_config;

  /// If true, the dialog is shown non-modally (Qt::NonModal) so the parent
  /// window remains interactive. Required for drag-and-drop from the host UI
  /// into the dialog. Defaults to false (modal).
  bool non_modal = false;
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

  /// Return the parser dialog's saved config (empty if no parser dialog was shown).
  /// Only valid after showDialog() returns kAccepted.
  [[nodiscard]] std::string parserConfig() const;

  /// Stats from the last showDialog() call.
  struct Stats {
    int tick_count = 0;
    int event_count = 0;
    int diff_apply_count = 0;
    bool has_parser_slot = false;
    bool parser_dialog_injected = false;  // True if a parser dialog was actually injected
  };
  [[nodiscard]] Stats lastStats() const {
    return stats_;
  }

 private:
  PJ::DialogHandle handle_;
  DialogEngineConfig config_;
  Stats stats_;
  std::string parser_config_;  // Saved parser config (populated on accept)
};

}  // namespace PJ
