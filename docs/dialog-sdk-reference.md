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
| `setShortcut(name, key_sequence)` | Assign keyboard shortcut (e.g. `"Ctrl+A"`) **NEW** |
| `setFilePicker(name, text, filter, title)` | Turn into file picker |
| `setFolderPicker(name, text, title)` | Turn into folder picker **NEW** |

### QListWidget

| Method | Description |
|--------|-------------|
| `setListItems(name, vector<string>)` | Set list items |
| `setSelectedItems(name, vector<string>)` | Set selected items by text |

### QTableWidget

| Method | Description |
|--------|-------------|
| `setTableHeaders(name, vector<string>)` | Set column headers |
| `setTableRows(name, vector<vector<string>>)` | Set row data |
| `setSelectedRows(name, vector<int>)` | Set selected row indices |
| `setDisabledRows(name, vector<int>)` | Grey out rows (non-selectable) **NEW** |

### QPlainTextEdit

| Method | Description |
|--------|-------------|
| `setPlainText(name, text)` | Set plain text content **NEW** |

### QTabWidget

| Method | Description |
|--------|-------------|
| `setTabIndex(name, int)` | Set active tab index |

### QDialogButtonBox

| Method | Description |
|--------|-------------|
| `setOkEnabled(bool)` | Enable/disable OK button (targets `"buttonBox"`) |
| `setOkEnabled(name, bool)` | Enable/disable OK button (custom name) |

### Generic (any widget)

| Method | Description |
|--------|-------------|
| `setEnabled(name, bool)` | Enable/disable widget |
| `setVisible(name, bool)` | Show/hide widget |

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
| `onFileSelected(name, path)` | QPushButton (file picker) | Selected file path |
| `onFolderSelected(name, path)` | QPushButton (folder picker) | Selected folder path **NEW** |
| `onSelectionChanged(name, items)` | QListWidget, QTableWidget | Vector of selected item texts |
| `onItemDoubleClicked(name, index)` | QListWidget, QTableWidget | Row index of double-clicked item |
| `onTabChanged(name, index)` | QTabWidget | New tab index |

---

## Lifecycle Hooks

| Method | When Called | Return |
|--------|-------------|--------|
| `onTick()` | Periodically while dialog is open | `true` to refresh UI |
| `onAccepted(final_state_json)` | User clicked OK | void |
| `onRejected()` | User clicked Cancel | void |
| `saveConfig()` | Host persisting state | JSON string |
| `loadConfig(json)` | Host restoring state | `true` if state changed |
| `lastError()` | Host checking for errors | Error string or empty |

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

Configure `DialogEngine` with a parser dialog provider:

```cpp
DialogEngineConfig config;
config.parser_dialog_provider = [&](const std::string& encoding) -> const PJ_dialog_vtable_t* {
  return registry.queryParserDialog(encoding);
};
config.initial_parser_config = saved_parser_config;  // Optional

DialogEngine engine(dialog_handle, config);
```

### Behavior

1. When the user selects an encoding in `comboBoxProtocol`, the engine looks up the parser's dialog vtable
2. If found, the parser's UI is loaded and injected into `pj_parser_slot`
3. The parser dialog's events and `widget_data()` are handled independently
4. On accept, both configs are saved: `engine.savedConfig()` + `engine.parserConfig()`

---

## New Features (IBRobotics Contributions)

| Feature | PR | Description |
|---------|----|-----------  |
| `setShortcut()` | core #33 | Keyboard shortcuts for buttons without Qt code |
| `setFolderPicker()` / `onFolderSelected()` | core #30 | Folder picker (complements file picker) |
| `setDisabledRows()` | core #35 | Non-selectable greyed-out rows in tables |
| `setPlainText()` | core #30 | QPlainTextEdit support |
| Parser dialog injection | core #30 | Embed parser options in data source dialogs |

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
