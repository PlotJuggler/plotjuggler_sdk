#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PJ {

/// Read-only parser for the event_json string passed to onWidgetEvent().
/// Returns std::nullopt for missing fields instead of throwing.
class WidgetEvent {
 public:
  explicit WidgetEvent(std::string_view event_json) : data_(nlohmann::json::parse(event_json, nullptr, false)) {
    if (data_.is_discarded()) {
      data_ = nlohmann::json::object();
    }
  }

  /// QLineEdit: text changed
  std::optional<std::string> text() const {
    return getString("text");
  }

  /// QComboBox: current index changed
  std::optional<int> currentIndex() const {
    return getInt("current_index");
  }

  /// QComboBox: current text
  std::optional<std::string> currentText() const {
    return getString("current_text");
  }

  /// QCheckBox, QRadioButton: toggled
  std::optional<bool> checked() const {
    return getBool("checked");
  }

  /// QSpinBox: value changed
  std::optional<int> valueInt() const {
    return getInt("value");
  }

  /// QDoubleSpinBox: value changed
  std::optional<double> valueDouble() const {
    auto it = data_.find("value");
    if (it == data_.end() || !it->is_number()) {
      return std::nullopt;
    }
    return it->get<double>();
  }

  /// QListWidget: selection changed
  std::optional<std::vector<std::string>> selectedItems() const {
    auto it = data_.find("selected_items");
    if (it == data_.end() || !it->is_array()) {
      return std::nullopt;
    }
    std::vector<std::string> result;
    result.reserve(it->size());
    for (const auto& item : *it) {
      if (item.is_string()) {
        result.push_back(item.get<std::string>());
      }
    }
    return result;
  }

  /// QPushButton: clicked
  bool clicked() const {
    auto it = data_.find("clicked");
    return it != data_.end() && it->is_boolean() && it->get<bool>();
  }

  /// File picker: file selected
  std::optional<std::string> fileSelected() const {
    return getString("file_selected");
  }

  /// Folder picker: folder selected
  std::optional<std::string> folderSelected() const {
    return getString("folder_selected");
  }

  /// QTabWidget: tab changed
  std::optional<int> tabIndex() const {
    return getInt("tab_index");
  }

  /// QListWidget: item double-clicked (returns row index)
  std::optional<int> itemDoubleClickedIndex() const {
    return getInt("item_double_clicked_index");
  }

  /// QTableWidget: horizontal-header section clicked (returns column index)
  std::optional<int> headerSection() const {
    return getInt("header_section");
  }

  /// Code editor: code changed
  std::optional<std::string> codeChanged() const {
    return getString("code_changed");
  }

  /// Caret offset (bytes) accompanying a codeChanged event, when the host
  /// reported one. Absent for hosts/events that don't carry the cursor.
  std::optional<int> codeCursor() const {
    auto it = data_.find("code_cursor");
    if (it == data_.end() || !it->is_number_integer()) {
      return std::nullopt;
    }
    return it->get<int>();
  }

  /// Drag-and-drop: items dropped on a widget (curves, files, or any draggable payload).
  std::optional<std::vector<std::string>> itemsDropped() const {
    auto it = data_.find("items_dropped");
    if (it == data_.end() || !it->is_array()) {
      return std::nullopt;
    }
    std::vector<std::string> result;
    result.reserve(it->size());
    for (const auto& item : *it) {
      if (item.is_string()) {
        result.push_back(item.get<std::string>());
      }
    }
    return result;
  }

  /// DateRangePicker: date/time range filter (ISO datetime strings, empty =
  /// unbounded on that side).
  struct DateRangeFilter {
    std::string from_iso;
    std::string to_iso;
  };

  std::optional<DateRangeFilter> dateRangeChanged() const {
    auto from = data_.find("date_from_iso");
    auto to = data_.find("date_to_iso");
    if (from == data_.end() || !from->is_string() || to == data_.end() || !to->is_string()) {
      return std::nullopt;
    }
    return DateRangeFilter{from->get<std::string>(), to->get<std::string>()};
  }

  /// RangeSlider: lower/upper handle positions (slider units).
  struct RangeValues {
    int lower;
    int upper;
  };

  std::optional<RangeValues> rangeChanged() const {
    auto lo = data_.find("range_lower");
    auto hi = data_.find("range_upper");
    if (lo == data_.end() || !lo->is_number_integer() || hi == data_.end() || !hi->is_number_integer()) {
      return std::nullopt;
    }
    return RangeValues{lo->get<int>(), hi->get<int>()};
  }

  /// ChartPreviewWidget: visible range changed via zoom or pan.
  struct ChartViewState {
    double x_min;
    double x_max;
    double y_min;
    double y_max;
  };

  std::optional<ChartViewState> chartViewChanged() const {
    auto xmin = data_.find("chart_x_min");
    auto xmax = data_.find("chart_x_max");
    auto ymin = data_.find("chart_y_min");
    auto ymax = data_.find("chart_y_max");
    if (xmin == data_.end() || !xmin->is_number() || xmax == data_.end() || !xmax->is_number() || ymin == data_.end() ||
        !ymin->is_number() || ymax == data_.end() || !ymax->is_number()) {
      return std::nullopt;
    }
    return ChartViewState{xmin->get<double>(), xmax->get<double>(), ymin->get<double>(), ymax->get<double>()};
  }

  /// Check if a key exists in the event data
  bool has(std::string_view key) const {
    return data_.contains(std::string(key));
  }

  /// Raw access for custom events
  const nlohmann::json& raw() const {
    return data_;
  }

 private:
  nlohmann::json data_;

  std::optional<std::string> getString(const char* key) const {
    auto it = data_.find(key);
    if (it == data_.end() || !it->is_string()) {
      return std::nullopt;
    }
    return it->get<std::string>();
  }

  std::optional<int> getInt(const char* key) const {
    auto it = data_.find(key);
    if (it == data_.end() || !it->is_number_integer()) {
      return std::nullopt;
    }
    return it->get<int>();
  }

  std::optional<bool> getBool(const char* key) const {
    auto it = data_.find(key);
    if (it == data_.end() || !it->is_boolean()) {
      return std::nullopt;
    }
    return it->get<bool>();
  }
};

}  // namespace PJ
