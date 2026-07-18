# Dialog plugin

A Dialog is a **toolkit-neutral** configuration UI: your plugin links **no Qt**. It
describes the UI as Qt Designer `.ui` XML and exchanges state as JSON; the host
renders the widgets and relays events back over the vtable. A dialog can be
**standalone**, or **embedded** in a DataSource/Toolbox.

Full reference: `pj_plugins/docs/dialog-plugin-guide.md`.
Setter + event-handler cheat sheet: `docs/dialog-sdk-reference.md`.

## The reactive loop (the mental model)

```
host reads widget_data() (JSON)  →  renders widgets
      → user interacts → host calls your onXxx() event handler
            → you mutate state, return TRUE iff state changed
                  → if TRUE, host re-reads widget_data()  → repeat
```

You never hold widgets. You describe *desired state* in `widget_data()` and react
to events. Return `true` **iff a re-rendered `widget_data()` would differ** from
what the host is already showing. That's subtler than "state changed": when the
user edits a line-edit and you only store the text, the widget already shows it —
return `false`. Return `true` when the event changes *other* widgets (enabling OK,
updating a preview, recomputing items). `true` always = wasted re-renders; `false`
after a dependent change = stale UI.

## Headers + skeleton

```cpp
#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>

namespace {
constexpr const char* kManifest =
    R"({"id":"my-dialog","name":"My Dialog","version":"1.0.0"})";

// buttonBox MUST be named exactly "buttonBox" AND set standardButtons, or the
// dialog renders with no OK/Cancel and no error.
constexpr const char* kUi = R"(<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0"><class>MyDialog</class>
 <widget class="QWidget" name="MyDialog"><layout class="QVBoxLayout">
  <item><widget class="QLineEdit" name="name_input"/></item>
  <item><widget class="QSpinBox" name="count_input"/></item>
  <item><widget class="QDialogButtonBox" name="buttonBox">
    <property name="standardButtons">
      <set>QDialogButtonBox::Cancel|QDialogButtonBox::Ok</set></property>
  </widget></item>
 </layout></widget></ui>)";
}  // namespace

class MyDialog : public PJ::DialogPluginTyped {
  using PJ::DialogPluginTyped::onValueChanged;   // avoid overload name-hiding
 public:
  std::string manifest()   const override { return kManifest; }
  std::string ui_content() const override { return kUi; }

  std::string widget_data() override {           // describe current state
    PJ::WidgetData wd;
    wd.setText("name_input", name_);
    wd.setValue("count_input", count_);
    wd.setRange("count_input", 0, 1000);
    wd.setOkEnabled(!name_.empty());
    return wd.toJson();
  }

  bool onTextChanged(std::string_view w, std::string_view t) override {
    if (w == "name_input") { name_ = std::string(t); return true; }
    return false;
  }
  bool onValueChanged(std::string_view w, int v) override {
    if (w == "count_input") { count_ = v; return true; }
    return false;
  }

  std::string saveConfig() const override {          // complete round-trip: every UI field
    nlohmann::json cfg{{"name", name_}, {"count", count_}};
    return cfg.dump();
  }
  bool loadConfig(std::string_view j) override {
    auto cfg = nlohmann::json::parse(j, nullptr, false);
    if (cfg.is_discarded()) return false;
    const auto old_name = name_;
    const auto old_count = count_;
    name_  = cfg.value("name", name_);               // tolerate missing keys
    count_ = cfg.value("count", count_);
    return name_ != old_name || count_ != old_count; // true iff state changed
  }

 private:
  std::string name_;
  int count_ = 10;
};

PJ_DIALOG_PLUGIN(MyDialog, kManifest)
```

Downstream CMake links `plotjuggler_sdk::plugin_sdk` (dialog SDK is part of it);
in-tree, link `pj_dialog_sdk`.

## Traps specific to Dialog

- **In a modal/standalone dialog, the `QDialogButtonBox` must be named exactly
  `buttonBox` and set `standardButtons` in the XML.** Otherwise it renders with no
  OK/Cancel — and there is **no compile or runtime error**. This is the #1 dialog
  bug. (Non-modal toolbox *panels* and parser-slot option panes legitimately have
  no buttonBox at all — the rule applies where OK/Cancel semantics exist.)
- **Every interactive widget needs a unique `objectName`**, and your setter/handler
  strings must match it exactly. A mismatch is silent — the state just doesn't apply.
- **Use `QPlainTextEdit` and `QTableWidget`**, not `QTextEdit` or a model-based
  `QTableView` — the latter two are not supported by the widget binding.
- **Overload name-hiding:** `onValueChanged` has an `int` and a `double` overload.
  Overriding one hides the other unless you add
  `using PJ::DialogPluginTyped::onValueChanged;` in the class body. Same caution for
  any other overloaded handler.
- **Return `true` iff state changed** (see the reactive loop above).
- **Do no I/O or blocking work in `widget_data()` or event handlers** — they run on
  the GUI thread and freeze the UI. `onTick()` runs on the GUI thread too: use it
  only for non-blocking checks or to drain results a worker thread produced
  (return `true` to trigger a refresh) — the actual I/O belongs on the worker.
- **Do not retain the `const char*`/string returned by `widget_data()`** past the
  next call (SKILL.md rule 5).
- **Config round-trip:** every UI-relevant field must survive
  `saveConfig()`→`loadConfig()`, and `loadConfig()` returns `true` only when it
  actually changed state.
- **`manifest()` must return the same JSON literal you pass to
  `PJ_DIALOG_PLUGIN`** (as the skeleton does) — hosts on the legacy discovery
  path read it from an instance via `get_manifest`.

## `widget_data()` setters (common)

`setText`, `setPlaceholder`, `setValue`, `setRange`, `setChecked`, `setItems`,
`setCurrentIndex`, `setLabel`, `setEnabled`, `setVisible`, `setOkEnabled`,
`setFilePicker`. Full list + which widget class each targets:
`docs/dialog-sdk-reference.md`.

## Event handlers (common)

`onTextChanged`, `onValueChanged(int)`, `onValueChanged(double)`, `onIndexChanged`,
`onToggled`, `onClicked`, `onFileSelected`, `onSelectionChanged`,
`onItemDoubleClicked`, plus lifecycle `onTick`, `onAccepted(final_state_json)`,
`onRejected`. Typed handlers **and `onTick()`** return `bool` (refresh needed);
`onAccepted`/`onRejected` return `void`.

## Embedded vs standalone

- **Standalone** — the whole `.so` is just the dialog; one `PJ_DIALOG_PLUGIN` macro.
- **Embedded** (in a DataSource/Toolbox) — the dialog is a member of the
  source/toolbox class, exposed via `getDialog()` → `PJ::borrowDialog(dialog_)`; the
  owner reads the dialog's state directly. Emit **both** family macros in the file.
  The borrowed dialog must **not outlive** its owner. Example:
  `pj_plugins/examples/mock_source_with_dialog.cpp`.

Two patterns the official plugins converge on for embedded dialogs:

- **The dialog owns the config.** The owner's `saveConfig()`/`loadConfig()` simply
  delegate to the dialog member — one config schema, no duplication, and the
  dialog is always consistent with what runs headless.
- **Owner→dialog data flows through injected callbacks.** The owner installs
  `std::function`s on the dialog (e.g. "list available topics", "apply") instead of
  the dialog reaching into the owner. Note for **non-modal toolbox panels**:
  `requestAccept()` is ignored there — a "Save/Apply" button must invoke an
  owner-installed callback instead.

For anything beyond a trivial UI, keep the `.ui` as a real Qt Designer file and
embed it at build time (`pj_embed_ui` — see `pj_plugins/docs/dialog-plugin-guide.md`)
rather than growing an inline XML string; the file stays editable in Designer and
the plugin still links no Qt.

## Static builds

For static/WASM builds with no `dlopen`, hosts consume plugins through
`DataSourceLibrary::loadStatic`, `MessageParserLibrary::loadStatic`, or
`ToolboxLibrary::loadStatic` — each accepts an optional companion dialog vtable
for the embedded dialog (there is no standalone `DialogLibrary::loadStatic`).
Relevant only if you are wiring a static host; a normal shared-library plugin
needs nothing extra.
