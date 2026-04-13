#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PJ {

/// A single point in a chart series (used by setChartSeries).
struct ChartPoint {
  double x;
  double y;
};

/// A named series of XY points for chart display (used by setChartSeries).
struct ChartSeries {
  std::string label;
  std::vector<ChartPoint> points;
};

/// Builder for the JSON string returned by get_widget_data().
/// Each method targets an existing widget in the .ui file by its objectName.
class WidgetData {
 public:
  // --- QLineEdit ---
  WidgetData& setText(std::string_view name, std::string_view text) {
    entry(name)["text"] = text;
    return *this;
  }

  WidgetData& setPlaceholder(std::string_view name, std::string_view text) {
    entry(name)["placeholder"] = text;
    return *this;
  }

  WidgetData& setReadOnly(std::string_view name, bool read_only) {
    entry(name)["read_only"] = read_only;
    return *this;
  }

  // --- QComboBox ---
  WidgetData& setCurrentIndex(std::string_view name, int index) {
    entry(name)["current_index"] = index;
    return *this;
  }

  WidgetData& setItems(std::string_view name, const std::vector<std::string>& items) {
    entry(name)["items"] = items;
    return *this;
  }

  // --- QCheckBox, QRadioButton ---
  WidgetData& setChecked(std::string_view name, bool checked) {
    entry(name)["checked"] = checked;
    return *this;
  }

  // --- QSpinBox ---
  WidgetData& setValue(std::string_view name, int value) {
    entry(name)["value"] = value;
    return *this;
  }

  // --- QDoubleSpinBox ---
  WidgetData& setValue(std::string_view name, double value) {
    entry(name)["value"] = value;
    return *this;
  }

  WidgetData& setRange(std::string_view name, int min, int max) {
    auto& e = entry(name);
    e["min"] = min;
    e["max"] = max;
    return *this;
  }

  // --- QListWidget ---
  WidgetData& setListItems(std::string_view name, const std::vector<std::string>& items) {
    entry(name)["list_items"] = items;
    return *this;
  }

  WidgetData& setSelectedItems(std::string_view name, const std::vector<std::string>& selected) {
    entry(name)["selected_items"] = selected;
    return *this;
  }

  // --- QTableWidget ---
  WidgetData& setTableHeaders(std::string_view name, const std::vector<std::string>& headers) {
    entry(name)["headers"] = headers;
    return *this;
  }

  WidgetData& setTableRows(std::string_view name, const std::vector<std::vector<std::string>>& rows) {
    entry(name)["rows"] = rows;
    return *this;
  }

  WidgetData& setSelectedRows(std::string_view name, const std::vector<int>& rows) {
    entry(name)["selected_rows"] = rows;
    return *this;
  }

  WidgetData& setDisabledRows(std::string_view name, const std::vector<int>& rows) {
    entry(name)["disabled_rows"] = rows;
    return *this;
  }

  // --- Chart (QFrame used as chart container) ---

  /// Set chart series data on a QFrame widget. The host will create or update
  /// a chart view inside the frame, displaying one QLineSeries per entry.
  WidgetData& setChartSeries(std::string_view name, const std::vector<ChartSeries>& series) {
    auto& e = entry(name);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : series) {
      nlohmann::json pts = nlohmann::json::array();
      for (const auto& p : s.points) {
        pts.push_back({p.x, p.y});
      }
      arr.push_back({{"label", s.label}, {"points", std::move(pts)}});
    }
    e["chart_series"] = std::move(arr);
    return *this;
  }

  /// Remove all series from the chart inside the named QFrame.
  WidgetData& clearChart(std::string_view name) {
    entry(name)["chart_series"] = nlohmann::json::array();
    return *this;
  }

  // --- QPlainTextEdit ---
  WidgetData& setPlainText(std::string_view name, std::string_view text) {
    entry(name)["plain_text"] = text;
    return *this;
  }

  // --- Code editor (QPlainTextEdit with syntax highlighting) ---

  /// Set the code content of a QPlainTextEdit used as a code editor.
  /// Unlike setPlainText (read-only), this enables editing and wires onCodeChanged events.
  WidgetData& setCodeContent(std::string_view name, std::string_view code) {
    entry(name)["code_content"] = code;
    return *this;
  }

  /// Set the language for syntax highlighting (e.g. "lua", "python").
  WidgetData& setCodeLanguage(std::string_view name, std::string_view lang) {
    entry(name)["code_language"] = lang;
    return *this;
  }

  // --- QLabel ---
  WidgetData& setLabel(std::string_view name, std::string_view text) {
    entry(name)["label"] = text;
    return *this;
  }

  // --- QPushButton ---
  WidgetData& setButtonText(std::string_view name, std::string_view text) {
    entry(name)["button_text"] = text;
    return *this;
  }

  /// Assign a keyboard shortcut to a QPushButton (e.g. "Ctrl+A", "Ctrl+Shift+A").
  /// The host creates a QShortcut that triggers click() on the button.
  WidgetData& setShortcut(std::string_view name, std::string_view key_sequence) {
    entry(name)["shortcut"] = key_sequence;
    return *this;
  }

  WidgetData& setFilePicker(
      std::string_view name, std::string_view button_text, std::string_view filter, std::string_view title) {
    auto& e = entry(name);
    e["button_text"] = button_text;
    e["action"] = "file_picker";
    e["filter"] = filter;
    e["title"] = title;
    return *this;
  }

  WidgetData& setFolderPicker(std::string_view name, std::string_view button_text, std::string_view title) {
    auto& e = entry(name);
    e["button_text"] = button_text;
    e["action"] = "folder_picker";
    e["title"] = title;
    return *this;
  }

  // --- QDialogButtonBox ---
  /// Set OK button enabled state. The widget name defaults to "buttonBox"
  /// which is the required name for the DialogEngine to wire accept/reject.
  WidgetData& setOkEnabled(bool enabled) {
    entry("buttonBox")["ok_enabled"] = enabled;
    return *this;
  }
  WidgetData& setOkEnabled(std::string_view name, bool enabled) {
    entry(name)["ok_enabled"] = enabled;
    return *this;
  }

  // --- QTabWidget ---
  WidgetData& setTabIndex(std::string_view name, int index) {
    entry(name)["tab_index"] = index;
    return *this;
  }

  // --- Generic (any widget) ---
  WidgetData& setEnabled(std::string_view name, bool enabled) {
    entry(name)["enabled"] = enabled;
    return *this;
  }

  WidgetData& setVisible(std::string_view name, bool visible) {
    entry(name)["visible"] = visible;
    return *this;
  }

  // --- Drop target ---

  /// Mark a widget as a drag-and-drop target for field curves.
  /// The DialogEngine reads this on init and installs a DropEventFilter for it.
  WidgetData& setDropTarget(std::string_view name, bool is_target = true) {
    entry(name)["drop_target"] = is_target;
    return *this;
  }

  // --- Dialog-level commands ---

  /// Request that the dialog accept (close with OK) after applying this widget data.
  /// Typically used to implement double-click-to-accept on list items.
  WidgetData& requestAccept() {
    data_["__request_accept"] = true;
    return *this;
  }

  /// Request that the host open a sub-dialog with the given UI XML.
  /// The sub-dialog is shown as a nested modal. When the user closes it,
  /// the main dialog resumes. The sub-dialog is read-only (no plugin events).
  WidgetData& requestSubDialog(std::string_view ui_xml) {
    data_["__request_sub_dialog"] = nlohmann::json{{"ui", ui_xml}};
    return *this;
  }

  /// Serialize to JSON string
  std::string toJson() const {
    return data_.dump();
  }

  /// Reset for reuse
  void clear() {
    data_ = nlohmann::json::object();
  }

 private:
  nlohmann::json data_ = nlohmann::json::object();

  nlohmann::json& entry(std::string_view name) {
    std::string key(name);
    if (!data_.contains(key)) {
      data_[key] = nlohmann::json::object();
    }
    return data_[key];
  }
};

}  // namespace PJ
