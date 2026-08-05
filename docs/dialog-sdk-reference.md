# Dialog SDK Reference

Quick reference for `PJ::WidgetData` setters and `PJ::DialogPluginTyped` event handlers.

For the full tutorial, see [dialog-plugin-guide.md](../pj_plugins/docs/dialog-plugin-guide.md).

---

## WidgetData Setters

### QLineEdit

| Method | Description |
|--------|-------------|
| `setText(name, text)` | Set current text |
| `setPlaceholder(name, text)` | Set placeholder text |
| `setReadOnly(name, bool)` | Make read-only |

### QComboBox

| Method | Description |
|--------|-------------|
| `setItems(name, vector<string>)` | Set dropdown items |
| `setCurrentIndex(name, int)` | Set selected index |

### QCheckBox / QRadioButton

| Method | Description |
|--------|-------------|
| `setChecked(name, bool)` | Set checked state |

### QSpinBox

| Method | Description |
|--------|-------------|
| `setValue(name, int)` | Set integer value |
| `setRange(name, min, max)` | Set min/max range |

### QDoubleSpinBox

| Method | Description |
|--------|-------------|
| `setValue(name, double)` | Set double value |

### QLabel

| Method | Description |
|--------|-------------|
| `setLabel(name, text)` | Set label text |

### QPushButton

| Method | Description |
|--------|-------------|
| `setButtonText(name, text)` | Set button label |
| `setButtonIcon(name, svg_data)` | Set an inline SVG icon (custom/one-off) |
| `setButtonIconNamed(name, icon_id)` | Set a button icon by id, resolved from the host's themed icon set (consistent tinting; unknown id → no icon) |
| `setShortcut(name, key_sequence)` | Assign keyboard shortcut (e.g. `"Ctrl+A"`) |
| `setFilePicker(name, text, filter, title)` | Turn into an **open** file picker (existing file) |
| `setSaveFilePicker(name, text, filter, title, default_suffix="")` | Turn into a **save-as** file picker (navigate + type a new filename); reports via `onFileSelected`. `default_suffix` is appended when the typed name has none |
| `setFolderPicker(name, text, title)` | Turn into folder picker |

### QListWidget

| Method | Description |
|--------|-------------|
| `setListItems(name, vector<string>)` | Set list items |
| `setSelectedItems(name, vector<string>)` | Set selected items by text |
| `setListItemsDeletable(name, bool)` | Draw a trailing trash button on every row; a click fires `onItemDeleteRequested`. |
| `setListPlaceholder(name, text)` | Centered empty-state hint over the list while it has no items; hidden once items appear. |
| `setListItemsDisabled(name, vector<string>)` | Grey out (disable + make unselectable) the items whose text is in the set, keeping them visible; matched by item text. An empty/absent set re-enables every item. |
| `setListSelectionMode(name, bool multi)` | Switch a list between single (`false`, default) and multi (`true`, extended) selection. |

### QTableWidget

| Method | Description |
|--------|-------------|
| `setTableHeaders(name, vector<string>)` | Set column headers |
| `setTableRows(name, vector<vector<string>>)` | Set row data as plain text. Every column sorts **lexicographically** — `"9"` after `"10"`. |
| `setTableRows(name, vector<vector<TableItem>>)` | Set row data with a per-cell sort key, so numeric columns sort numerically. `TableItem{text, optional<NumericValue>}`: `TableItem("x")` sorts by text, `TableItem(v)` renders and sorts on `v`, `TableItem(v, "display")` sorts on `v` but shows `display` (lossy or decorated rendering, or a hidden key such as a date over `int64` ns). Pass the native type — `NumericValue` keeps `int64`/`uint64` exact past 2⁵³. |
| `setTableSortIndicator(name, column, ascending)` | Draw the header arrow **without** enabling Qt's sorting — for tables that sort themselves via `onHeaderClicked` (Qt paints an arrow only for its own sorting). Cosmetic; never reorders rows. |
| `setSelectedRows(name, vector<int>)` | Set selected row indices |
| `setDisabledRows(name, vector<int>)` | Grey out rows (non-selectable) |
| `setTableRadioColumn(name, column, checked_row)` | Render `column` as an exclusive radio group; `checked_row` is selected (-1 = none). Fires `onTableRadioSelected`. |
| `appendTableRows(name, seq, rows)` | Delta: append rows without resending `rows` — string or `TableItem` rows (typed rows keep their sort keys via a sparse `append_values` map). See guide → "Table deltas". |
| `updateTableCells(name, seq, vector<TableCellUpdate>)` | Delta: rewrite individual cells (`{row, col, TableItem}`, plugin row space). Replaces the whole cell — a keyless item clears the sort key. |
| `removeTableRows(name, seq, vector<int>)` | Delta: remove plugin-space row indexes |

> A table must not combine `sortingEnabled=true` in its `.ui` with
> `onHeaderClicked` — Qt would sort the view while the plugin reorders the model,
> and the plugin's order loses. See `pj_plugins/docs/dialog-plugin-guide.md` →
> "Sortable Tables".

### QTreeWidget (since 0.21.0)

Tree items use stable, plugin-supplied string IDs. Never use display text, row
numbers, or paths through visible labels as identity. `TreeItem` snapshots are a
flat array linked by `id` / `parent_id` (`""` means top level); filtering that
array to one `parent_id` preserves the plugin's unsorted sibling order. Ragged
cell arrays are valid, and missing trailing columns render empty.

```cpp
struct TreeCell {
  std::string text;
  std::optional<NumericValue> sort_value;
  std::string tooltip;
  std::string icon;  // host semantic name, not a path
};

struct TreeItem {
  std::string id;
  std::string parent_id;
  std::vector<TreeCell> cells;
  bool enabled = true;
  bool selectable = true;
  TreeCheckState check_state = TreeCheckState::None;
  bool may_have_children = false;
};
```

`TreeCheckState` uses exactly `None`, `Unchecked`, `PartiallyChecked`, and
`Checked`, encoded as `"none"`, `"unchecked"`, `"partially_checked"`, and
`"checked"`. In v1 it applies only to column 0. The initial semantic tree icon
names are `folder`, `topic`, `schema`, `info`, `warning`, and `error`; an empty
or unknown name displays no icon.

| Method | Description |
|--------|-------------|
| `setTreeHeaders(name, headers)` | Set column headers |
| `setTreeItems(name, items)` | Publish a complete keyed snapshot; empty clears the tree |
| `setTreeSelectedIds(name, ids)` | Replace the logical selected-ID set; empty clears selection |
| `setTreeExpandedIds(name, ids)` | Replace the expanded-ID set; empty collapses all public items |
| `setTreeVisibleIds(name, ids)` | Replace the ID filter; empty is an active zero-match filter |
| `clearTreeVisibleIds(name)` | Emit JSON null to remove the filter and restore visible-by-default behavior |
| `setTreeSelectionMode(name, multi)` | Select single (`false`) or extended multi-selection (`true`) |

These are independent additive channels: any may appear without `tree_items`,
and absence means unchanged. `WidgetDataView::treeVisibilityUpdate()` preserves
visibility's three states: `nullopt` = absent, `Filter` = array (including an
empty array), and `Reset` = JSON null. The host adds every listed item's ancestor
chain so matches remain reachable. Selected/expanded/visible IDs not in the
current snapshot are exposed unchanged by the view and pruned with a host
diagnostic. Invalid `tree_items` data is rejected atomically;
`treeItems(name, &validation_error)` supplies the reason.

All four string-array view channels (headers, selected IDs, expanded IDs, and a
visibility filter) decode strictly: if any array member is not a string, that
whole channel returns `nullopt` and remains unchanged. A malformed array is
never interpreted as an empty destructive update. Activation columns likewise
must be integers in the inclusive range `0..INT_MAX`.

`may_have_children=true` permits a host-only expansion placeholder until the
plugin publishes a later complete snapshot with real children. V1 always sends
full keyed snapshots; deltas are deferred. Any future tree delta must reuse the
table delta protocol's fresh sequence gate and all-or-nothing application rules.

### QFrame Chart Container

| Method | Description |
|--------|-------------|
| `setChartSeries(name, vector<ChartSeries>)` | Create/update chart series inside a QFrame |
| `setChartMarkers(name, vector<ChartMarker>)` | Overlay markers (events/regions/value-bands) on top of the series |
| `clearChart(name)` | Remove chart series and markers |
| `setChartZoomEnabled(name, bool)` | Enable chart zoom/pan events |

### QPlainTextEdit

| Method | Description |
|--------|-------------|
| `setPlainText(name, text)` | Set plain text content |
| `setCodeContent(name, code)` | Set editable code content |
| `setCodeLanguage(name, lang)` | Set syntax highlighting language such as `"lua"` or `"python"` |
| `setCodeCursor(name, cursor)` | Move the caret to byte offset `cursor` (e.g. after inserting a completion) |
| `setCodeCaretTracking(name, enabled=true)` | Opt into caret tracking: report the caret on cursor moves too, not just edits |

### QTabWidget

| Method | Description |
|--------|-------------|
| `setTabIndex(name, int)` | Set active tab index |

### QStackedWidget

Use the direct child page's Qt `objectName` as its stable identity. A page name
survives page reordering; an index describes only the current layout. When both
keys are emitted, the page name wins. Empty/unknown names and negative or
out-of-range indexes are invalid: writers preserve them on the wire so the host
can warn and leave the current page unchanged.

| Method | Description |
|--------|-------------|
| `setStackedPage(name, page_object_name)` | Select a page by stable Qt `objectName` (preferred) |
| `setStackedIndex(name, int)` | Select a page by its current index |

### QDateTimeEdit

Also binds the `QDateEdit` / `QTimeEdit` subclasses. Datetimes are wall-clock
local time exchanged verbatim; events always carry a full ISO datetime, with
fractional seconds only when the editor's display format includes them.

| Method | Description |
|--------|-------------|
| `setDateTime(name, iso8601)` | Set the displayed datetime (ISO-8601, e.g. `"2026-05-21T13:45:00"`); empty/unparsable strings are ignored |
| `setDateTimeRange(name, min_iso, max_iso)` | Set the allowed [min, max] datetime range |

### QDialogButtonBox

| Method | Description |
|--------|-------------|
| `setOkEnabled(bool)` | Enable/disable OK button (targets `"buttonBox"`) |
| `setOkEnabled(name, bool)` | Enable/disable OK button (custom name) |

### MarkerTimeline (custom widget, class name `MarkerTimeline`)

Editable multi-marker strip: any number of resizable Region spans + single-point Event marks, each draggable.

| Method | Description |
|--------|-------------|
| `setMarkerTimelineBounds(name, min, max)` | Integer step domain marks live in (set before the marks) |
| `setMarkerTimelineMarks(name, marks)` | Replace the whole `std::vector<TimelineMark>` set (last-writer-wins; empty clears) |
| `setMarkerTimelineTimeSpan(name, min_ns, max_ns)` | Map the step domain onto `[min_ns, max_ns]` for hover labels (`0,0` → raw steps) |

`TimelineMark{int id; bool region; int start; int end;}` — `region=false` is a point Event (`end` ignored).

### Generic (any widget)

| Method | Description |
|--------|-------------|
| `setEnabled(name, bool)` | Enable/disable widget |
| `setVisible(name, bool)` | Show/hide widget |
| `setDropTarget(name, bool)` | Accept dropped item labels and emit `onItemsDropped` |
| `setFieldValid(name, ok, tooltip)` | Inline valid/invalid indicator the plugin drives (optional tooltip) |

### Dialog-level Commands

| Method | Description |
|--------|-------------|
| `requestAccept()` | Close dialog with OK (one-shot) |
| `requestSubDialog(ui_xml)` | Open nested modal sub-dialog |

---

## Event Handlers

Override these in your `DialogPluginTyped` subclass. Return `true` when state changes to trigger `widget_data()` refresh.

| Handler | Widget Types | Payload |
|---------|--------------|---------|
| `onTextChanged(name, text)` | QLineEdit | New text content |
| `onIndexChanged(name, index)` | QComboBox | Selected index |
| `onToggled(name, checked)` | QCheckBox, QRadioButton | New checked state |
| `onValueChanged(name, int)` | QSpinBox | New integer value |
| `onValueChanged(name, double)` | QDoubleSpinBox | New double value |
| `onClicked(name)` | QPushButton | (no payload) |
| `onFileSelected(name, path)` | QPushButton (file picker or save-file picker) | Selected file path |
| `onFolderSelected(name, path)` | QPushButton (folder picker) | Selected folder path |
| `onSelectionChanged(name, items)` | QListWidget, QTableWidget | Vector of selected item texts (table: column-0 text) |
| `onItemDoubleClicked(name, index)` | QListWidget, QTableWidget | Row index of double-clicked item |
| `onItemDeleteRequested(name, index)` | QListWidget (deletable) | Row whose trash button was clicked (see `setListItemsDeletable`) |
| `onTableRadioSelected(name, row)` | QTableWidget radio column | Row whose radio was clicked (see `setTableRadioColumn`) |
| `onCodeChanged(name, code)` | QPlainTextEdit code editor | Edited code |
| `onCodeChangedWithCursor(name, code, cursor)` | QPlainTextEdit code editor | Edited code + caret offset (`cursor < 0` when no opt-in / not reported); defaults to `onCodeChanged` |
| `onItemsDropped(name, items)` | Any widget with `setDropTarget` | Dropped item labels |
| `onChartViewChanged(name, x_min, x_max, y_min, y_max)` | QFrame chart container | Visible chart range |
| `onMarkerTimelineChanged(name, marks)` | MarkerTimeline | Full `std::vector<TimelineMark>` set after a drag/resize/delete |
| `onTabChanged(name, index)` | QTabWidget | New tab index |
| `onStackedPageChanged(name, index, page_object_name)` | QStackedWidget | New page index and stable Qt `objectName` |
| `onTreeSelectionChanged(name, ids)` | QTreeWidget | Complete logical selected-ID set, including filtered-out selections |
| `onTreeItemActivated(name, id, column)` | QTreeWidget | Stable item ID and activated column (keyboard or double-click) |
| `onTreeExpansionChanged(name, id, expanded)` | QTreeWidget | Stable item ID and new expansion state |
| `onTreeCheckStateChanged(name, id, state)` | QTreeWidget | Stable item ID and new column-0 `TreeCheckState` |
| `onDateTimeChanged(name, iso8601)` | QDateTimeEdit (incl. QDateEdit/QTimeEdit) | Edited datetime as ISO-8601 string (local wall-clock) |

---

## Lifecycle Hooks

| Method | When Called | Return |
|--------|-------------|--------|
| `onTick()` | Periodically while dialog is open | `true` to refresh UI |
| `onAccepted(final_state_json)` | User clicked OK | void |
| `onRejected()` | User clicked Cancel | void |
| `saveConfig()` | Host persisting state | JSON string |
| `loadConfig(json)` | Host restoring state | `true` if state changed |

---

## Parser Dialog Injection

Data source dialogs can embed parser-specific options using the `pj_parser_slot` pattern.

### UI Setup

Add a placeholder widget named `pj_parser_slot` in your `.ui`:

```xml
<widget class="QWidget" name="pj_parser_slot">
  <property name="minimumSize">
    <size><width>0</width><height>100</height></size>
  </property>
</widget>
```

### Host Configuration

Configure the host dialog runtime with a parser dialog provider:

```cpp
HostDialogRuntimeConfig config;
config.parser_dialog_provider = [&](const std::string& encoding) -> const PJ_dialog_vtable_t* {
  return registry.queryParserDialog(encoding);
};
config.initial_parser_config = saved_parser_config;  // Optional

auto result = runHostDialog(dialog_handle, config);
```

### Behavior

1. When the user selects an encoding in `comboBoxProtocol`, the host dialog runtime looks up the parser's dialog vtable
2. If found, the parser's UI is loaded and injected into `pj_parser_slot`
3. The parser dialog's events and `widget_data()` are handled independently
4. On accept, both configs are returned to the host: the source config and the parser config


---

## Quick Example

```cpp
#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>

class MyDialog : public PJ::DialogPluginTyped {
  std::string host_ = "localhost";
  int port_ = 9870;
  bool connected_ = false;

public:
  std::string widget_data() override {
    PJ::WidgetData wd;
    wd.setText("hostInput", host_)
      .setValue("portInput", port_)
      .setRange("portInput", 1, 65535)
      .setButtonText("connectBtn", connected_ ? "Disconnect" : "Connect")
      .setShortcut("connectBtn", "Ctrl+Return")  // NEW: keyboard shortcut
      .setOkEnabled(connected_);
    return wd.toJson();
  }

  bool onTextChanged(std::string_view name, std::string_view text) override {
    if (name == "hostInput") { host_ = text; return true; }
    return false;
  }

  bool onValueChanged(std::string_view name, int value) override {
    if (name == "portInput") { port_ = value; return true; }
    return false;
  }

  bool onClicked(std::string_view name) override {
    if (name == "connectBtn") {
      connected_ = !connected_;
      return true;
    }
    return false;
  }
};
```
