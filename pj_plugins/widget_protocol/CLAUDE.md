# pj_plugins/widget_protocol — Widget plugin C ABI, C++ SDK, and host loader

Self-contained nested module: the Widget C ABI (`widget_protocol.h`), the
toolkit-free C++ authoring base (`sdk/widget_plugin_base.hpp`), the canonical Qt
module list (`widget_qt_modules.h`) and the host loader (`host/WidgetLibrary`,
`WidgetHandle`, `WidgetHostBridge`). Author guide: `../docs/widget-guide.md`.

A Widget plugin is the one plugin that links Qt; nothing in this module does.
The widget crosses the ABI as `PJ_qwidget_t` (`void*`) and is never
dereferenced here. Do not add a Qt include to any header or source in this
module; the author's `.cpp` does the `static_cast`.

Local traps not visible from the headers:
- `PJ_WIDGET_PLUGIN` reads `QT_VERSION_MAJOR/MINOR/PATCH/STR` where it expands,
  to fill `build_info`. The translation unit using it must have included a Qt
  header. The Qt-free mock defines the four macros itself.
- Every slot is main-thread. `inputs()` returns an array the base class keeps
  until the next `inputs()`, `load_config()` or destroy; the host copies it.
  `inputs()` never changes in the middle of `on_update()`.
- `destroy` is synchronous and deletes the widget object if still alive. The
  host calls `setParent(nullptr)` first. A Widget library is never unloaded:
  the loader itself guarantees it (`load` uses a no-op deleter, `loadFromHandle`
  pins the handle), the host needs no keep-alive store.
- `PJ::widgetCompilerFamily()` / `PJ::widgetBuiltAsDebug()` live in `widget_types.hpp`
  (shared by sdk and host); hosts compare `build_info` against them, never against
  a hand-written string.
- Slot names that look like other families' (`report_message`, `history`) are
  deliberately not reused: `tools/feature_floors/feature_floor_check.py`
  matches the vtable member name as a bare token in plugin sources, so a
  generic name would raise floors of unrelated plugins. The widget host slots
  are `query_history`, `notify_inputs_changed`, `report_widget_message`.
- The Qt module list is generated into `share/plotjuggler_sdk/widget_qt_modules.json`
  at CMake configure time from `widget_qt_modules.h` (one `X(Name, "since")` per
  line, nothing else on the line). Edit only the header.
- `PluginDescriptor::widgets[].type` must not contain `/`: the panel id is
  `plugin_id/type`.
- ABI layout pins live in `tests/widget_abi_layout_sentinels_test.cpp` (not in
  `pj_base`: the family is a `pj_plugins` module).

See ../CLAUDE.md for module context.
