#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <nlohmann/json.hpp>
#include <optional>
#include <pj_plugins/sdk/widget_data.hpp>  // TimelineMark, TreeCheckState
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

  /// QStackedWidget: current page index. Hosts normally emit this together
  /// with stackedPage(); each accessor remains independently defensive.
  /// @since 0.21.0
  std::optional<int> stackedIndex() const {
    return getInt("stacked_index");
  }

  /// QStackedWidget: current page's stable Qt objectName. Hosts normally emit
  /// this together with stackedIndex(); each accessor remains independently
  /// defensive.
  /// @since 0.21.0
  std::optional<std::string> stackedPage() const {
    return getString("stacked_page");
  }

  /// QTreeWidget: the complete logical selected-ID set after a user change,
  /// including selected items currently hidden by an ID visibility filter.
  /// Empty is a valid cleared selection. Every ID must be a non-empty string.
  /// @since 0.21.0
  std::optional<std::vector<std::string>> treeSelectionChanged() const {
    auto it = data_.find("tree_selection_changed");
    if (it == data_.end() || !it->is_array()) {
      return std::nullopt;
    }
    std::vector<std::string> ids;
    ids.reserve(it->size());
    for (const auto& encoded_id : *it) {
      if (!encoded_id.is_string()) {
        return std::nullopt;
      }
      auto id = encoded_id.get<std::string>();
      if (id.empty()) {
        return std::nullopt;
      }
      ids.push_back(std::move(id));
    }
    return ids;
  }

  struct TreeItemActivation {
    std::string id;
    int column = 0;
  };

  /// QTreeWidget: stable item ID and activated column. Activation covers both
  /// keyboard and double-click activation.
  /// @since 0.21.0
  std::optional<TreeItemActivation> treeItemActivated() const {
    auto it = data_.find("tree_item_activated");
    if (it == data_.end() || !it->is_object()) {
      return std::nullopt;
    }
    auto id = it->find("id");
    auto column = it->find("column");
    if (id == it->end() || !id->is_string() || id->get_ref<const std::string&>().empty() || column == it->end() ||
        !column->is_number_integer() || column->get<int>() < 0) {
      return std::nullopt;
    }
    return TreeItemActivation{id->get<std::string>(), column->get<int>()};
  }

  struct TreeExpansionChange {
    std::string id;
    bool expanded = false;
  };

  /// QTreeWidget: stable item ID and its new expanded state.
  /// @since 0.21.0
  std::optional<TreeExpansionChange> treeExpansionChanged() const {
    auto it = data_.find("tree_expansion_changed");
    if (it == data_.end() || !it->is_object()) {
      return std::nullopt;
    }
    auto id = it->find("id");
    auto expanded = it->find("expanded");
    if (id == it->end() || !id->is_string() || id->get_ref<const std::string&>().empty() || expanded == it->end() ||
        !expanded->is_boolean()) {
      return std::nullopt;
    }
    return TreeExpansionChange{id->get<std::string>(), expanded->get<bool>()};
  }

  struct TreeCheckStateChange {
    std::string id;
    TreeCheckState state = TreeCheckState::None;
  };

  /// QTreeWidget: stable item ID and the new canonical column-0 check state.
  /// @since 0.21.0
  std::optional<TreeCheckStateChange> treeCheckStateChanged() const {
    auto it = data_.find("tree_check_state_changed");
    if (it == data_.end() || !it->is_object()) {
      return std::nullopt;
    }
    auto id = it->find("id");
    auto state = it->find("state");
    if (id == it->end() || !id->is_string() || id->get_ref<const std::string&>().empty() || state == it->end() ||
        !state->is_string()) {
      return std::nullopt;
    }
    auto decoded_state = treeCheckStateFromWireValue(state->get_ref<const std::string&>());
    if (!decoded_state.has_value()) {
      return std::nullopt;
    }
    return TreeCheckStateChange{id->get<std::string>(), *decoded_state};
  }

  /// QListWidget: item double-clicked (returns row index)
  std::optional<int> itemDoubleClickedIndex() const {
    return getInt("item_double_clicked_index");
  }
  std::optional<int> itemDeleteRequestedIndex() const {
    return getInt("item_delete_index");
  }

  /// QTableWidget: horizontal-header section clicked (returns column index)
  std::optional<int> headerSection() const {
    return getInt("header_section");
  }

  /// QTableWidget: a radio button in the radio column was selected (row index)
  std::optional<int> tableRadioRow() const {
    return getInt("table_radio_row");
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

  /// QDateTimeEdit: edited datetime as an ISO-8601 string.
  std::optional<std::string> dateTimeChanged() const {
    return getString("datetime_iso");
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

  /// MarkerTimeline: the full mark set after a user edit (drag / resize / delete).
  std::optional<std::vector<TimelineMark>> markerTimelineChanged() const {
    auto it = data_.find("marker_timeline_marks");
    if (it == data_.end() || !it->is_array()) {
      return std::nullopt;
    }
    return timelineMarksFromJson(*it);
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
