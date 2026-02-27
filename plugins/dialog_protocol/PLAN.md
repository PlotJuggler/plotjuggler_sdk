# Plugin Dialog Protocol — Implementation Plan

## Context

The PoC (`plugins/poc/` in the `feature/dialog-poc` worktree) validated that host-rendered plugin dialogs work via .ui files + JSON. Now we harden it into an ABI-stable C header + C++ SDK for plugin authors.

**Resolved design questions:**
- Error reporting: `get_last_error()` on vtable (returns NULL if no error)
- File pickers: declarative in widget data (`"action": "file_picker"`)
- Headless mode: implicit (`load_config()` + `on_accepted()`, no special flag)

## Deliverables

### File structure

```
plugins/dialog_protocol/
├── include/pj/
│   ├── dialog_protocol.h           # [1] Pure C ABI header (zero dependencies)
│   └── sdk/
│       ├── dialog_plugin_base.hpp  # [2] Level 1: C++ base class (vtable + string buffers)
│       ├── widget_data.hpp         # [3] Level 2: setters — build get_widget_data() JSON
│       ├── widget_event.hpp        # [4] Level 2: getters — parse on_widget_event() JSON
│       └── dialog_plugin_typed.hpp # [5] Level 3: typed event dispatch
├── examples/
│   └── mock_streamer.cpp           # [6] Complete example plugin using the SDK
├── tests/
│   ├── widget_data_test.cpp        # [7] Test WidgetData builder
│   ├── widget_event_test.cpp       # [8] Test WidgetEvent parser
│   └── plugin_lifecycle_test.cpp   # [9] Test full vtable lifecycle via mock
└── CMakeLists.txt                  # [10] Build: interface lib + shared mock + tests
```

### Dependency graph (what each file knows about)

```
dialog_protocol.h          → nothing (pure C, zero deps)
dialog_plugin_base.hpp     → dialog_protocol.h, <string>
widget_data.hpp            → nlohmann/json (standalone, no base class dep)
widget_event.hpp           → nlohmann/json (standalone, no base class dep)
dialog_plugin_typed.hpp    → dialog_plugin_base.hpp, widget_event.hpp
```

WidgetData and WidgetEvent are independent helpers — usable without the base class.
The typed dispatch class extends the base and uses WidgetEvent for parsing.

---

## [1] `dialog_protocol.h` — C ABI header

Pure C. The stable binary contract between host and plugin.

```c
#ifndef PJ_DIALOG_PROTOCOL_H
#define PJ_DIALOG_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_DIALOG_PROTOCOL_VERSION 1

/* Export macro for plugin shared libraries */
#if defined(_WIN32)
#  define PJ_DIALOG_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define PJ_DIALOG_EXPORT __attribute__((visibility("default")))
#else
#  define PJ_DIALOG_EXPORT
#endif

/*
 * String ownership convention:
 *
 * - Strings returned by plugin functions are OWNED BY THE PLUGIN.
 * - Pointer is valid until the next call to the SAME function on the SAME context.
 * - Host must copy if it needs to retain the string.
 * - Host-provided strings (event_json, config_json, final_state_json) are valid
 *   only for the duration of the call.
 */

typedef struct {
  uint32_t protocol_version;  /* Must equal PJ_DIALOG_PROTOCOL_VERSION */

  /* Lifecycle */
  void*       (*create)(void);
  void        (*destroy)(void* ctx);

  /* Plugin-owned, stable pointer (does not change between calls) */
  const char* (*get_manifest)(void* ctx);    /* JSON */
  const char* (*get_ui_content)(void* ctx);  /* Qt Designer XML */

  /* Plugin-owned, valid until next call to same function on same ctx */
  const char* (*get_widget_data)(void* ctx); /* JSON */

  /* Returns true if host should re-read get_widget_data() */
  bool        (*on_widget_event)(void* ctx, const char* widget_name,
                                 const char* event_json);
  bool        (*on_tick)(void* ctx);

  /* Dialog result */
  void        (*on_accepted)(void* ctx, const char* final_state_json);
  void        (*on_rejected)(void* ctx);

  /* Config persistence — same ownership as get_widget_data */
  const char* (*save_config)(void* ctx);
  bool        (*load_config)(void* ctx, const char* config_json);

  /* Error reporting — NULL if no error. Plugin-owned, valid until next call. */
  const char* (*get_last_error)(void* ctx);
} pj_dialog_vtable_t;

/*
 * Every dialog plugin exports this symbol.
 * Returns a pointer to a static vtable. The pointer is valid for the process lifetime.
 *
 * Usage: const pj_dialog_vtable_t* vt = pj_get_dialog_vtable();
 */
typedef const pj_dialog_vtable_t* (*pj_get_dialog_vtable_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* PJ_DIALOG_PROTOCOL_H */
```

---

## [2] `dialog_plugin_base.hpp` — Level 1: C++ base class

Depends on: `dialog_protocol.h`, `<string>`. No JSON library.

Provides:
- Virtual methods returning `std::string` instead of `const char*`
- Internal string buffers (`widget_data_buf_`, `config_buf_`, `error_buf_`) for lifetime management
- Static trampoline functions that cast `void* ctx` back to `DialogPluginBase*`
- `static const pj_dialog_vtable_t* vtable()` that returns the auto-wired vtable
- `PJ_DIALOG_PLUGIN(ClassName)` macro that generates the exported `pj_get_dialog_vtable` function

Plugin author's interface:
```cpp
class MyPlugin : public pj::sdk::DialogPluginBase {
  std::string manifest() const override;        // return JSON string
  std::string ui_content() const override;      // return .ui XML string
  std::string widget_data() override;           // return JSON string
  bool on_widget_event(std::string_view widget_name,
                       std::string_view event_json) override;
  bool on_tick() override;
  void on_accepted(std::string_view final_state_json) override;
  void on_rejected() override;
  std::string save_config() const override;
  bool load_config(std::string_view config_json) override;
  // Optional: override to report errors
  std::string last_error() const override;      // return "" for no error
};
PJ_DIALOG_PLUGIN(MyPlugin)
```

---

## [3] `widget_data.hpp` — Level 2: Setters

Depends on: `nlohmann/json`. Standalone — no base class dependency.

A builder for the JSON string returned by `get_widget_data()`. Each method targets an existing widget in the .ui file by its `objectName`.

```cpp
namespace pj::sdk {

class WidgetData {
 public:
  // Property setters for widgets defined in the .ui file.
  // Each method takes the widget's objectName and the value(s) to set.

  // QLineEdit
  WidgetData& set_text(std::string_view name, std::string_view text);
  WidgetData& set_placeholder(std::string_view name, std::string_view text);
  WidgetData& set_read_only(std::string_view name, bool read_only);

  // QComboBox
  WidgetData& set_current_index(std::string_view name, int index);
  WidgetData& set_items(std::string_view name, std::span<const std::string> items);

  // QCheckBox, QRadioButton
  WidgetData& set_checked(std::string_view name, bool checked);

  // QSpinBox, QDoubleSpinBox
  WidgetData& set_value(std::string_view name, int value);
  WidgetData& set_value(std::string_view name, double value);
  WidgetData& set_range(std::string_view name, int min, int max);

  // QListWidget
  WidgetData& set_list_items(std::string_view name, std::span<const std::string> items);
  WidgetData& set_selected_items(std::string_view name,
                                 std::span<const std::string> selected);

  // QTableWidget
  WidgetData& set_table_headers(std::string_view name,
                                std::span<const std::string> headers);
  WidgetData& set_table_rows(std::string_view name,
                             std::span<const std::vector<std::string>> rows);

  // QLabel, QPushButton — display text
  WidgetData& set_label(std::string_view name, std::string_view text);
  WidgetData& set_button_text(std::string_view name, std::string_view text);

  // QPushButton with file picker action
  WidgetData& set_file_picker(std::string_view name, std::string_view button_text,
                              std::string_view filter, std::string_view title);

  // QDialogButtonBox
  WidgetData& set_ok_enabled(std::string_view name, bool enabled);

  // QTabWidget
  WidgetData& set_tab_index(std::string_view name, int index);

  // Generic properties (any widget)
  WidgetData& set_enabled(std::string_view name, bool enabled);
  WidgetData& set_visible(std::string_view name, bool visible);

  // Serialize to JSON string
  std::string to_json() const;

  // Reset for reuse
  void clear();

 private:
  nlohmann::json data_ = nlohmann::json::object();

  // Returns reference to the JSON object for `name`, creating if needed
  nlohmann::json& entry(std::string_view name);
};

}  // namespace pj::sdk
```

---

## [4] `widget_event.hpp` — Level 2: Getters

Depends on: `nlohmann/json`. Standalone — no base class dependency.

Parses the `event_json` string passed to `on_widget_event()`. Read-only accessors.

```cpp
namespace pj::sdk {

class WidgetEvent {
 public:
  explicit WidgetEvent(std::string_view event_json);

  // QLineEdit: text changed
  std::optional<std::string> text() const;

  // QComboBox: index changed
  std::optional<int> current_index() const;
  std::optional<std::string> current_text() const;

  // QCheckBox, QRadioButton: toggled
  std::optional<bool> checked() const;

  // QSpinBox: value changed
  std::optional<int> value_int() const;

  // QDoubleSpinBox: value changed
  std::optional<double> value_double() const;

  // QListWidget: selection changed
  std::optional<std::vector<std::string>> selected_items() const;

  // QPushButton: clicked
  bool clicked() const;

  // File picker: file selected
  std::optional<std::string> file_selected() const;

  // QTabWidget: tab changed
  std::optional<int> tab_index() const;

  // Raw access for custom events
  bool has(std::string_view key) const;
  const nlohmann::json& raw() const;

 private:
  nlohmann::json data_;
};

}  // namespace pj::sdk
```

---

## [5] `dialog_plugin_typed.hpp` — Level 3: Typed event dispatch

Depends on: `dialog_plugin_base.hpp`, `widget_event.hpp`.

Extends the base class. Overrides `on_widget_event` to parse the event JSON and dispatch to typed virtual methods. Plugin authors override only the methods they need.

```cpp
namespace pj::sdk {

class DialogPluginTyped : public DialogPluginBase {
 public:
  // Override these instead of on_widget_event()
  virtual bool on_text_changed(std::string_view widget_name, std::string_view text);
  virtual bool on_index_changed(std::string_view widget_name, int index);
  virtual bool on_toggled(std::string_view widget_name, bool checked);
  virtual bool on_value_changed(std::string_view widget_name, int value);
  virtual bool on_value_changed(std::string_view widget_name, double value);
  virtual bool on_selection_changed(std::string_view widget_name,
                                    const std::vector<std::string>& selected);
  virtual bool on_clicked(std::string_view widget_name);
  virtual bool on_file_selected(std::string_view widget_name, std::string_view path);
  virtual bool on_tab_changed(std::string_view widget_name, int index);

 private:
  // Parses event_json, dispatches to typed virtuals above
  bool on_widget_event(std::string_view widget_name,
                       std::string_view event_json) override final;
};

}  // namespace pj::sdk
```

All typed virtuals return `false` by default (no widget refresh needed).

---

## [6] `mock_streamer.cpp` — Example plugin

A self-contained example using `DialogPluginTyped` + `WidgetData`. Demonstrates:
- Manifest with custom widget mapping
- Embedded .ui content (minimal inline XML)
- Widget data with text fields, combo boxes, buttons, lists
- Typed event handling (on_clicked for connect, on_text_changed for host/port)
- Tick-based async state (simulated connection + topic discovery)
- File picker for certificate
- Config save/restore
- Error reporting
- Export via `PJ_DIALOG_PLUGIN(MockStreamer)`

Builds as a shared library (`mock_streamer_plugin.so`).

---

## [7-9] Tests

**`widget_data_test.cpp`** — Unit tests for WidgetData builder:
- Each setter produces correct JSON structure
- `clear()` resets state
- Multiple properties on same widget merge correctly
- File picker generates action/filter/title fields

**`widget_event_test.cpp`** — Unit tests for WidgetEvent parser:
- Parse each event type (text, index, checked, clicked, file_selected, etc.)
- Missing fields return std::nullopt (not crash)
- Invalid JSON handled gracefully

**`plugin_lifecycle_test.cpp`** — Integration test using the mock plugin:
- Vtable has correct protocol_version
- All function pointers are non-null
- create/destroy lifecycle doesn't leak (run under ASAN)
- get_manifest returns valid JSON with required fields
- get_widget_data returns valid JSON
- on_widget_event with known event returns expected bool
- save_config → load_config round-trip preserves state
- get_last_error returns NULL when no error
- String ownership: pointer from get_widget_data is stable until next call

Tests link the mock plugin **statically** (no dlopen). The dlopen path can be tested later during host integration.

---

## [10] `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(pj_dialog_protocol LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# --- Header-only interface libraries ---

# Pure C ABI header (no deps)
add_library(pj_dialog_protocol INTERFACE)
target_include_directories(pj_dialog_protocol INTERFACE include)

# C++ SDK (adds nlohmann/json for widget_data/widget_event)
include(FetchContent)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(pj_dialog_sdk INTERFACE)
target_link_libraries(pj_dialog_sdk INTERFACE pj_dialog_protocol nlohmann_json::nlohmann_json)
target_include_directories(pj_dialog_sdk INTERFACE include)

# --- Mock plugin (shared library for integration testing) ---

add_library(mock_streamer_plugin SHARED examples/mock_streamer.cpp)
target_link_libraries(mock_streamer_plugin PRIVATE pj_dialog_sdk)

# --- Tests ---

find_package(GTest REQUIRED)
enable_testing()

add_executable(widget_data_test tests/widget_data_test.cpp)
target_link_libraries(widget_data_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_data_test COMMAND widget_data_test)

add_executable(widget_event_test tests/widget_event_test.cpp)
target_link_libraries(widget_event_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_event_test COMMAND widget_event_test)

add_executable(plugin_lifecycle_test tests/plugin_lifecycle_test.cpp
                                     examples/mock_streamer.cpp)
target_link_libraries(plugin_lifecycle_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME plugin_lifecycle_test COMMAND plugin_lifecycle_test)
```

---

## Implementation order

0. Write this plan as `plugins/dialog_protocol/PLAN.md` in the repo (persistent reference)
1. `dialog_protocol.h` — C ABI header (no deps, foundation for everything)
2. `dialog_plugin_base.hpp` — base class + vtable trampolines
3. `widget_data.hpp` — setters
4. `widget_event.hpp` — getters
5. `dialog_plugin_typed.hpp` — typed dispatch
6. `mock_streamer.cpp` — example plugin (validates the SDK API design)
7. `CMakeLists.txt` — build everything
8. Tests — widget_data_test, widget_event_test, plugin_lifecycle_test
9. Build and verify all tests pass

## Verification

```bash
cd plugins/dialog_protocol
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```
