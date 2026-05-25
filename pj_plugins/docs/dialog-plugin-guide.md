# Writing a Dialog Plugin

> **Tracks the v5 plugin ABI** (`PJ_ABI_VERSION == 5`). Every dialog
> vtable slot is `PJ_NOEXCEPT` — the SDK trampolines in
> `DialogPluginBase` catch exceptions automatically, but your overrides
> must assume no exception ever crosses the ABI boundary. All dialog
> calls happen on the main (GUI) thread; see `ARCHITECTURE.md` for the
> full thread-class contract.

> **Vocabulary used throughout this guide**:
> - `PJ::WidgetData` — JSON builder for outbound widget state (host reads).
> - `PJ::WidgetDataView` — read-only parsed view the host applies to widgets.
> - Object name — the `objectName` attribute on a Qt widget. This is the key
>   that links code to UI and is required on every interactive widget.

## Required UI Conventions

The host renders your `.ui` XML through `QUiLoader`. Three conventions are
non-negotiable; misnaming will silently break the dialog at runtime with no
compile-time error.

| Requirement | Why it matters |
|---|---|
| The `QDialogButtonBox` MUST be named exactly `buttonBox` (camelCase) | The host calls `findChild<QDialogButtonBox*>("buttonBox")` to wire OK/Cancel. Other names yield a dialog with no buttons. |
| The `QDialogButtonBox` MUST set the `standardButtons` property in the XML | Without it, the box instantiates with no buttons even when found by name. |
| Every interactive widget MUST have a unique `objectName` | All `WidgetData` setters and event handlers address widgets by name. |

## What is a Dialog Plugin?

A dialog plugin is a shared library (`.so` / `.dylib` / `.dll`) that drives a
Qt Designer `.ui` form through a stable C ABI. The plugin describes its UI as
XML, exchanges widget state as JSON, and handles user interactions via typed
event callbacks — all without linking Qt itself. The host loads the `.ui`,
renders the widgets, and relays events to the plugin over the C vtable.

## Quick Start

1. Subclass `PJ::DialogPluginTyped`.
2. Provide inline Qt Designer `.ui` XML via `ui_content()`.
3. Override `manifest()` with a JSON string (`id`, name, version, etc.).
4. Implement `widget_data()` to push state to the UI.
5. Override the typed event handlers you need (`onTextChanged`,
   `onIndexChanged`, `onToggled`, etc.) — return `true` when state changes.
6. Export with `PJ_DIALOG_PLUGIN(YourClass, kManifestJson)`.
7. Build as a shared library linking `pj_dialog_sdk`.

A complete example lives at
`pj_plugins/dialog_protocol/examples/mock_dialog.cpp`.

## Plugin Contract

Follow these rules. Some are enforced by manifest validation or host UI wiring;
others prevent runtime failures that are silent at compile time.

**MUST**
- Honour the [Required UI Conventions](#required-ui-conventions) above
  (`buttonBox` naming, `standardButtons`, every interactive widget has an
  `objectName`).
- Return `true` from an event handler iff plugin-internal state changed; the
  host re-reads `widget_data()` only on `true`. Returning `true` always wastes
  re-renders; returning `false` after a real change leaves the UI stale.
- Validate every `manifest()` JSON string at build time — the host rejects
  manifests missing `id`, `name`, or `version`.
- When overriding either `onValueChanged` overload (int or double), add
  `using PJ::DialogPluginTyped::onValueChanged;` in the class body. C++
  name hiding will otherwise drop the un-overridden overload silently.

**MUST NOT**
- Throw exceptions across virtual overrides. The SDK trampolines catch them,
  but the event is reported to the host as a generic failure.
- Block the GUI thread inside `widget_data()` or event handlers (no I/O,
  no sleeps). Long work belongs in `onTick()` or a host-thread-friendly
  background pattern.
- Use `QTextEdit` or model-based `QTableView` — the widget binding system
  does not support them. Use `QPlainTextEdit` for plain text/code editing,
  or `QLabel`, `QListWidget`, and `QTableWidget` for display/table cases.
- Retain the JSON string returned by `widget_data()` on the host side past
  the next `widget_data()` call on the same dialog.

## Step by Step

### 1. Declare your class

Subclass `DialogPluginTyped` and override `manifest()`, `ui_content()`,
`widget_data()`, and the typed event handlers your dialog needs:

```cpp
#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>

constexpr const char* kManifestJson = R"({
  "id": "my-dialog",
  "name": "My Dialog",
  "version": "1.0.0",
  "description": "Example dialog plugin"
})";

class MyDialog : public PJ::DialogPluginTyped {
 public:
  std::string manifest() const override {
    return kManifestJson;
  }

  std::string ui_content() const override {
    return R"(<?xml version="1.0" encoding="UTF-8"?>
      <ui version="4.0">
        <!-- Qt Designer XML here -->
      </ui>
    )";
  }

  std::string widget_data() override;

  bool onTextChanged(std::string_view widget_name,
                     std::string_view text) override;
};
```

If you override one of the `onValueChanged` overloads (int or double) but not
the other, add a `using` declaration to prevent the base-class overload from
being hidden:

```cpp
class MyDialog : public PJ::DialogPluginTyped {
  using PJ::DialogPluginTyped::onValueChanged;
  // now override only the int version — the double version remains visible
  bool onValueChanged(std::string_view name, int value) override;
};
```

### 2. Define the UI

Provide the Qt Designer `.ui` XML as a string literal returned by
`ui_content()`. Every widget you want to interact with must have a unique
`objectName` — this is the key that links widgets to code in `widget_data()`
and the event handlers.

```cpp
const char* kUiContent = R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>MyDialog</class>
 <widget class="QWidget" name="MyDialog">
  <layout class="QVBoxLayout">
   <item>
    <widget class="QLineEdit" name="host_input"/>
   </item>
   <item>
    <widget class="QSpinBox" name="port_input">
     <property name="minimum"><number>1</number></property>
     <property name="maximum"><number>65535</number></property>
    </widget>
   </item>
   <item>
    <widget class="QDialogButtonBox" name="buttonBox">
     <property name="standardButtons">
      <set>QDialogButtonBox::Cancel|QDialogButtonBox::Ok</set>
     </property>
    </widget>
   </item>
  </layout>
 </widget>
</ui>
)";
```

> **Important:** The `QDialogButtonBox` must be named `"buttonBox"` (camelCase) and
> must have the `standardButtons` property set in the XML. The host dialog runtime
> searches for `findChild<QDialogButtonBox*>("buttonBox")` to wire the
> accept/reject signals. Without `standardButtons`, no OK/Cancel buttons appear.

> **Tip:** You can design the `.ui` in Qt Designer, then paste the XML into a
> raw string literal. Just make sure every interactive widget has a descriptive
> `objectName`.

#### EmbedUi — external `.ui` files

For larger dialogs, keeping the XML inline becomes unwieldy. The `EmbedUi`
CMake helper converts a `.ui` file into a generated header with the XML as a
`constexpr` string:

```cmake
include(cmake/EmbedUi.cmake)
pj_embed_ui(my_plugin
  UI_FILE  ${CMAKE_CURRENT_SOURCE_DIR}/ui/my_dialog.ui
  HEADER   ${CMAKE_CURRENT_BINARY_DIR}/generated/my_dialog_ui.hpp
  VAR_NAME kMyDialogUi
)
```

Then in your plugin:

```cpp
#include "my_dialog_ui.hpp"  // generated

std::string ui_content() const override { return kMyDialogUi; }
```

The `.ui` file is tracked as a CMake configure dependency — editing it
triggers header regeneration.

### 3. Build the widget state

Override `widget_data()` to return a JSON string describing the current state of
every widget. Use the `PJ::WidgetData` builder — each method targets a widget by
its `objectName` and returns `*this` for chaining:

```cpp
std::string widget_data() override {
  PJ::WidgetData wd;

  wd.setText("host_input", host_)
    .setPlaceholder("host_input", "e.g. localhost")
    .setValue("port_input", port_)
    .setRange("port_input", 1, 65535)
    .setOkEnabled(!host_.empty());

  return wd.toJson();
}
```

The host calls `widget_data()` whenever the plugin signals a state change (by
returning `true` from an event handler), then applies the result to the live
UI.

### 4. Handle events

Override the typed event handlers for the widget types in your dialog. Each
handler receives the `objectName` of the widget that fired and the event
payload. Return `true` if the event changed your internal state — this tells
the host to re-read `widget_data()` and update the UI.

```cpp
bool onTextChanged(std::string_view name, std::string_view text) override {
  if (name == "host_input") {
    host_ = std::string(text);
    return true;  // state changed — host will re-read widget_data()
  }
  return false;  // unknown widget — no update needed
}
```

**Typed handler reference:**

| Handler | Widget types | Payload |
|---|---|---|
| `onTextChanged(name, text)` | QLineEdit | new text content |
| `onIndexChanged(name, index)` | QComboBox | selected index |
| `onToggled(name, checked)` | QCheckBox, QRadioButton | new checked state |
| `onValueChanged(name, int)` | QSpinBox | new integer value |
| `onValueChanged(name, double)` | QDoubleSpinBox | new double value |
| `onClicked(name)` | QPushButton | (no payload) |
| `onFileSelected(name, path)` | QPushButton (file picker) | selected file path |
| `onSelectionChanged(name, items)` | QListWidget | vector of selected item texts |
| `onItemDoubleClicked(name, index)` | QListWidget | row index of double-clicked item |
| `onHeaderClicked(name, section)` | QTableWidget | clicked column index (for plugin-owned sorting) |
| `onTabChanged(name, index)` | QTabWidget | new tab index |

All handlers default to returning `false`. Override only the ones you need.

> **Column sorting.** The host does not enable `QTableWidget`'s built-in sort
> (it would reorder items and desync the index-based `setVisibleRows` /
> `setSelectedRows` model). Instead, header clicks arrive as
> `onHeaderClicked(name, section)`; a plugin that wants sortable columns
> re-orders its own row model (numeric for numeric columns) and re-emits the
> rows. The host shows a sort indicator on the clicked column.

### 5. Export the plugin

At file scope, after the class definition:

```cpp
PJ_DIALOG_PLUGIN(MyDialog, kManifestJson)
```

This generates the `extern "C"` entry point (`PJ_get_dialog_vtable`) that the
host resolves via dlsym/GetProcAddress. Passing the manifest literal lets the
catalog read metadata without instantiating the dialog. The legacy
`PJ_DIALOG_PLUGIN(MyDialog)` form remains supported for existing source, but
catalog discovery must instantiate those dialogs to call `manifest()`.

### 6. Build

```cmake
add_library(my_dialog_plugin SHARED my_dialog.cpp)
target_link_libraries(my_dialog_plugin PRIVATE pj_dialog_sdk)
```

No Qt dependency is needed in the plugin — only the host links Qt.

## Manifest Schema

`manifest()` returns the same JSON string supplied to `PJ_DIALOG_PLUGIN`.
New dialogs should pass a static manifest literal to the macro so catalog
discovery can inspect metadata without constructing the dialog. Legacy dialogs
that use `PJ_DIALOG_PLUGIN(MyDialog)` without a manifest still load, but the
catalog must instantiate them to call `manifest()`. The same required keys
apply in both forms.

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `id` | string | yes | Stable plugin identifier used by the host catalog. Must be unique per plugin. |
| `name` | string | yes | Human-readable plugin name. |
| `version` | string | yes | Semver version string. |
| `description` | string | no | Short description of the dialog. |

The host validates these keys when it inspects the dialog vtable; manifests
missing a required string are rejected.

## The Reactive Loop

The dialog protocol follows a simple reactive cycle:

```
        host                         plugin
          |                            |
          |--- (1) get_widget_data --->|  host reads initial state
          |<-- JSON ---                |
          |                            |
          |   [applies state to UI]    |
          |                            |
          |   [user interacts]         |
          |                            |
          |--- (2) on_widget_event --->|  host relays the interaction
          |<-- true ---                |  plugin returns true: "I changed"
          |                            |
          |--- (3) get_widget_data --->|  host re-reads state
          |<-- JSON ---                |
          |                            |
          |   [applies updated state]  |
          |                            |
          :   ... cycle repeats ...    :
```

1. The host calls `widget_data()` and applies the JSON to the live `.ui` form.
2. When the user interacts with a widget, the host calls the plugin's event
   handler with the widget name and event payload.
3. If the handler returns `true`, the host calls `widget_data()` again and
   re-applies the result. This is how a change to one widget can update
   others (e.g., checking "Use TLS" makes a certificate picker visible).

Additionally, `onTick()` is called periodically by the host. If it returns
`true`, the host re-reads `widget_data()` — this enables async background
work like polling a server for available topics.

## Widget Reference Table

| Qt widget | WidgetData setters | Event handler |
|---|---|---|
| QLineEdit | `setText`, `setPlaceholder`, `setReadOnly` | `onTextChanged(name, text)` |
| QComboBox | `setItems`, `setCurrentIndex` | `onIndexChanged(name, index)` |
| QCheckBox | `setChecked` | `onToggled(name, checked)` |
| QRadioButton | `setChecked` | `onToggled(name, checked)` |
| QSpinBox | `setValue(int)`, `setRange` | `onValueChanged(name, int)` |
| QDoubleSpinBox | `setValue(double)` | `onValueChanged(name, double)` |
| QPushButton | `setButtonText` | `onClicked(name)` |
| QPushButton (file picker) | `setFilePicker` | `onFileSelected(name, path)` |
| QPushButton (folder picker) | `setFolderPicker` | `onFolderSelected(name, path)` |
| QLabel | `setLabel` | (none — display only) |
| QListWidget | `setListItems`, `setSelectedItems` | `onSelectionChanged(name, items)`, `onItemDoubleClicked(name, index)` |
| QTableWidget | `setTableHeaders`, `setTableRows`, `setSelectedRows`, `setVisibleRows`, `setRowColor`, `setCellTooltip` | `onSelectionChanged(name, items)`, `onHeaderClicked(name, section)` |
| QPlainTextEdit | `setPlainText`, `setCodeContent`, `setCodeLanguage` | `onCodeChanged(name, code)` for code editors |
| QFrame (chart container) | `setChartSeries`, `clearChart`, `setChartZoomEnabled` | `onChartViewChanged(name, x_min, x_max, y_min, y_max)` |
| QDateTimeEdit | `setDateTime`, `setDateTimeRange` | (none — input only) |
| RangeSlider (two-handle) | `setRangeSliderBounds`, `setRangeSliderValues`, `setRangeSliderTimeSpan` | `onRangeChanged(name, lower, upper)` |
| SequencePicker (date range) | `setDatePickerEarliest` | `onDateRangeChanged(name, from_iso, to_iso, every_day)` |
| MetadataQueryBar | `setQueryKeys`, `setQueryOperators`, `setQueryValues`, `setQueryCompletions`, `setQuerySchema`, `setQueryFeedback` | `onQuerySelector(name, role, value)` |
| QTabWidget | `setTabIndex` | `onTabChanged(name, index)` |
| QDialogButtonBox | `setOkEnabled` | (none — host handles OK/Cancel) |

All widgets also support `setEnabled(name, bool)`, `setVisible(name, bool)`,
and `setDropTarget(name, bool)`. Drop targets receive
`onItemsDropped(name, items)`.

`onHeaderClicked(name, section)` reports the clicked column index for plugins
that drive their own sorting. The `QTableWidget` styling setters layer over the
row data: `setVisibleRows` live-filters by index (an empty set hides every row;
to re-show all rows pass the full index list — clearing the field makes *no*
change), `setRowColor` tints a row (`"#rrggbb"`, or `""` to clear), and
`setCellTooltip` annotates a single cell.

For `onQuerySelector`, `role` is one of `"key"`, `"op"`, or `"value"`. For
`SequencePicker`, the `from_iso` / `to_iso` strings are ISO-8601 datetimes and
are empty when that side of the range is unbounded.

> **Note:** `QTextEdit` and `QTableView` (model-based) are not supported by the
> widget binding system. Use `QPlainTextEdit` for plain text or code editing,
> and `QTableWidget` for tabular data such as topic lists and preview tables.
>
> **Custom widgets:** `RangeSlider`, `SequencePicker`, and `MetadataQueryBar`
> are PlotJuggler-provided widget classes, not stock Qt. Use them as *promoted*
> widgets in your `.ui` (promote a placeholder `QWidget` to the class name); the
> host binds them by object name exactly like the stock widgets above.

## Optional Features

### onTick — periodic background work

Override `onTick()` for async work such as polling a server or running a
discovery process. The host calls it periodically while the dialog is open.
Return `true` when your state changes and the UI needs an update:

```cpp
bool onTick() override {
  if (discovery_complete_) {
    return false;  // nothing new
  }
  if (checkDiscoveryResult()) {
    topics_ = fetchDiscoveredTopics();
    discovery_complete_ = true;
    return true;  // UI needs update
  }
  return false;
}
```

### Config persistence

Override `saveConfig()` and `loadConfig()` so the host can persist dialog state
across sessions:

```cpp
std::string saveConfig() const override {
  nlohmann::json cfg;
  cfg["host"] = host_;
  cfg["port"] = port_;
  return cfg.dump();
}

bool loadConfig(std::string_view config_json) override {
  auto cfg = nlohmann::json::parse(config_json, nullptr, false);
  if (cfg.is_discarded()) return false;
  if (auto it = cfg.find("host"); it != cfg.end() && it->is_string())
    host_ = it->get<std::string>();
  if (auto it = cfg.find("port"); it != cfg.end() && it->is_number_integer())
    port_ = it->get<int>();
  return true;  // state changed
}
```

### File picker

Turn any QPushButton into a file picker with `setFilePicker()`. The host shows
a native file dialog when the button is clicked and delivers the result via
`onFileSelected()`:

```cpp
// In widget_data():
wd.setFilePicker("cert_btn", "Select Certificate...",
                  "*.pem *.crt", "Select TLS Certificate");

// Handler:
bool onFileSelected(std::string_view name, std::string_view path) override {
  if (name == "cert_btn") {
    cert_path_ = std::string(path);
    return true;
  }
  return false;
}
```

### Dynamic visibility and enabled state

Control widget visibility and enabled state from `widget_data()` to build
conditional UIs:

```cpp
// Show certificate picker only when TLS is enabled
wd.setVisible("cert_btn", use_tls_);

// Disable Connect until host is filled in
wd.setEnabled("connect_btn", !host_.empty());
```

### OK button gating

Control whether the dialog's OK button is clickable:

```cpp
wd.setOkEnabled(is_connected_ && has_selection_);
```

### Double-click to accept — `onItemDoubleClicked` + `requestAccept`

Use `onItemDoubleClicked` to handle double-click on list items. Combined with
`requestAccept()`, this implements the common pattern of double-clicking an item
to select it and immediately close the dialog:

```cpp
bool onItemDoubleClicked(std::string_view name, int index) override {
  if (name == "column_list" && index >= 0) {
    selected_index_ = index;
    accept_requested_ = true;
    return true;  // triggers widget_data() refresh
  }
  return false;
}

std::string widget_data() override {
  PJ::WidgetData wd;
  // ... set widget states ...

  if (accept_requested_) {
    accept_requested_ = false;
    wd.requestAccept();  // tells the host to close the dialog with OK
  }

  return wd.toJson();
}
```

`requestAccept()` sets a `__request_accept` flag in the widget data JSON. After
applying widget state, the host dialog runtime checks this flag and calls
`dialog->accept()` if set. This is a one-shot — the flag is consumed on the
next `widget_data()` call.

### onAccepted / onRejected — dialog completion

Override these to react when the user closes the dialog:

```cpp
void onAccepted(std::string_view final_state_json) override {
  // User clicked OK — start streaming, open file, etc.
}

void onRejected() override {
  // User clicked Cancel — clean up connections
  connected_ = false;
}
```

### Panel close — `requestClose`

`requestClose(reason)` asks the host to tear down the panel that hosts the
plugin. It is a **panel-only** command: the `PanelEngine` observes the
`__request_close` flag on every tick and closes the panel after invoking its
`onCloseRequested` callback with the reason string. The modal `DialogEngine`
**ignores** it — classic dialogs close through `requestAccept()` and the
buttonBox instead, so the same plugin code is safe under either host.

`reason` is a free-form, plugin-defined string (for example `"import_complete"`,
`"user_back"`, or `"error"`) forwarded verbatim to the host.

```cpp
std::string widget_data() override {
  PJ::WidgetData wd;
  // ... set widget states ...
  if (import_finished_) {
    wd.requestClose("import_complete");
  }
  return wd.toJson();
}
```

### Error reporting

Fallible dialog callbacks return `bool` through the C ABI and receive a
`PJ_error_t*` out-param. In the C++ SDK, throw only for exceptional failures or
return `false` from event handlers that do not handle an event. The SDK
trampolines catch exceptions and populate `PJ_error_t` for the host.

### Dialog geometry persistence

Dialog geometry (size and position) is **automatically persisted** by the host
via `QSettings`. When a dialog is opened, the host restores the geometry from
the previous session. When the dialog closes, the host saves the current
geometry. The key is derived from the plugin's manifest `"name"` field.

**No plugin code is needed.** Every dialog gets this behavior for free.

### Sub-dialog — `requestSubDialog`

Open a read-only helper dialog (e.g. a reference table or help window) from
within your main dialog. The sub-dialog is shown as a nested modal — the main
dialog is blocked until the user closes the sub-dialog.

```cpp
// Store the sub-dialog UI XML (typically embedded via pj_embed_ui)
#include "help_dialog_ui.hpp"

bool onClicked(std::string_view name) override {
  if (name == "helpButton") {
    show_help_ = true;
    return true;  // triggers widget_data() refresh
  }
  return false;
}

std::string widget_data() override {
  PJ::WidgetData wd;
  // ... set widget states ...

  if (show_help_) {
    show_help_ = false;
    wd.requestSubDialog(kHelpDialogUi);  // pass UI XML string
  }

  return wd.toJson();
}
```

`requestSubDialog()` sets a `__request_sub_dialog` command in the widget data
JSON. The host loads the UI XML via `QUiLoader`, wraps it in a child `QDialog`,
and runs it modally. The sub-dialog supports standard `QDialogButtonBox`
accept/reject wiring. This is a one-shot command — the request is consumed
after opening the sub-dialog.

## Choosing a Base Class

| Class | Use when |
|---|---|
| `DialogPluginTyped` | **Recommended.** Typed dispatch — override `onTextChanged`, `onValueChanged`, etc. No JSON parsing needed. |
| `DialogPluginBase` | You need raw access to `onWidgetEvent(name, event_json)` with manual JSON parsing. |
| C vtable (`dialog_protocol.h`) | You're writing a plugin in C, Rust, or another language that can produce a C-compatible shared library. |

`DialogPluginTyped` inherits from `DialogPluginBase`, which implements the C
vtable trampolines and exception-safe ABI boundary. All three levels share the
same wire protocol — the host doesn't know or care which one you used.

## Examples

### Standalone dialog — `mock_dialog.cpp`

`pj_plugins/dialog_protocol/examples/mock_dialog.cpp` is a minimal standalone
dialog for testing. It demonstrates the core pattern with three widgets:

- **QLineEdit** for name input (`onTextChanged`, `setText`, `setPlaceholder`)
- **QSpinBox** for count (`onValueChanged(int)`, `setValue`, `setRange`)
- **QCheckBox** for verbose toggle (`onToggled`, `setChecked`)
- **QDialogButtonBox** for OK gating (`setOkEnabled`)
- **Config persistence** — `saveConfig()` / `loadConfig()` round-trip

Use this as a starting point for simple dialogs.

### Combined DataSource + Dialog — `mock_source_with_dialog.cpp`

`pj_plugins/examples/mock_source_with_dialog.cpp` is the reference
implementation for the DataSource-owned dialog pattern. It demonstrates a
streaming data source with a full configuration dialog in a single `.so`:

- **QTabWidget** with "Connection" and "Advanced" tabs
- **QLineEdit** for host input (`onTextChanged`, `setText`, `setPlaceholder`)
- **QSpinBox** for port (`onValueChanged(int)`, `setValue`, `setRange`)
- **QDoubleSpinBox** for timeout (`onValueChanged(double)`, `setValue`)
- **QComboBox** for protocol selection (`onIndexChanged`, `setItems`)
- **QCheckBox** for TLS toggle and auto-reconnect (`onToggled`, `setChecked`)
- **QPushButton** for connect/disconnect (`onClicked`, `setButtonText`)
- **QLabel** for dynamic status text (`setLabel`)
- **QListWidget** for topic selection (`onSelectionChanged`, `setListItems`)
- **QDialogButtonBox** for OK gating (`setOkEnabled`)
- **Conditional visibility** — topic list shown only when connected
- **onTick** — simulated async topic discovery
- **Config persistence** — `saveConfig()` / `loadConfig()` for all fields
- **Dialog result** — `onAccepted()` / `onRejected()`
- **Read-only accessors** — `host()`, `port()`, `useTls()`, etc. for the
  DataSource to consume
- **Shared state** — the DataSource owns the dialog as a member and reads
  its state directly (no JSON serialization at runtime)

### DataSource-owned dialog pattern

When a dialog is part of a DataSource plugin, the dialog class is a member of
the source class. The source overrides `getDialog()` returning a typed
`PJ_borrowed_dialog_t` fat pointer via `PJ::borrowDialog(dialog_)` — no
`extern "C"` forward declaration required:

```cpp
class MySource : public PJ::StreamSourceBase {
 public:
  PJ_borrowed_dialog_t getDialog() override {
    return PJ::borrowDialog(dialog_);
  }
 private:
  MyDialog dialog_;
};

PJ_DATA_SOURCE_PLUGIN(MySource, R"({"id":"my-source","name":"My Source","version":"1.0.0"})")
PJ_DIALOG_PLUGIN(MyDialog, kManifestJson)  // also specialises PJ::dialogVtableFor<MyDialog>()
                                          // so PJ::borrowDialog picks up the right vtable.
```

The host resolves both vtables, creates a borrowed `DialogHandle` from the
source's dialog context, and drives the dialog through its dialog runtime. After
the dialog completes, the source reads its dialog member's state directly.
The source handle owns the dialog object storage and keeps the plugin DSO
loaded, so any borrowed dialog handle must be scoped inside the source handle's
lifetime.

See `pj_plugins/docs/data-source-guide.md` for the full DataSource-side
documentation of this pattern.

## Common Mistakes

| Symptom | Cause | Fix |
|---|---|---|
| Dialog window has no OK / Cancel buttons | `QDialogButtonBox` not named `buttonBox`, or `standardButtons` property missing in XML | Rename to `buttonBox`, add `<set>QDialogButtonBox::Cancel\|QDialogButtonBox::Ok</set>` |
| Overriding `onValueChanged(int)` silently disables the `double` version (or vice versa) | C++ name hiding | Add `using PJ::DialogPluginTyped::onValueChanged;` in the class body |
| UI does not update after an event | Event handler returned `false`, so the host did not re-read `widget_data()` | Return `true` whenever internal state changed |
| `setText`/`setValue`/etc. has no visible effect | Wrong `objectName`, or widget type not in the [Widget Reference Table](#widget-reference-table) | Match XML `objectName` exactly; replace `QTextEdit`/`QTableView` with supported widgets |
| File picker button does nothing | `setFilePicker(...)` not called in `widget_data()` for this `objectName` | Call it once per `widget_data()` so the host wires the click |
| Sub-dialog opens repeatedly on every refresh | `requestSubDialog()` left set in `widget_data()` after the request fires | Set a one-shot flag; clear it before calling `requestSubDialog()` |
| Dialog state lost on layout reload | `loadConfig()` never restored fields, or `saveConfig()` returned `"{}"` | Round-trip every field through `saveConfig()` / `loadConfig()` |
| Manifest missing `id`/`name`/`version` causes load failure | Host rejects manifests missing required string keys | Validate the manifest in unit tests; the SDK does not assert this for dialogs at build time |
