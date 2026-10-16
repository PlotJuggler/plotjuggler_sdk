# Writing a Widget Plugin

A Widget plugin is a shared library that contributes dockable panels. Each
panel shows a widget the plugin creates with Qt. The host owns the data and
the layout; the plugin owns the pixels.

Widget is the only family whose plugins link Qt. The SDK does not: the widget
object crosses the C ABI as an opaque `void*` (`PJ_qwidget_t`), and the plugin
casts it in its own source. A plugin of any other family must not link Qt; if a
product needs both, ship the widgets as a separate library.

A complete Qt-free example is `pj_plugins/widget_protocol/examples/mock_widget.cpp`.

## Quick start

1. Subclass `PJ::WidgetPluginBase` and override `inputs()`, `createWidget()` and
   `onUpdate()`.
2. Write `manifest.json` (see [Manifest](#manifest)) and embed it with
   `pj_configure_plugin(... MANIFEST_HEADER ...)`.
3. Export with `PJ_WIDGET_PLUGIN(YourClass, kManifest)`. For a namespaced class in
   a static build use `PJ_WIDGET_PLUGIN_NAMED(ns::YourClass, YourClass, kManifest)`.
4. Build a shared library with `FAMILIES widget`, linking
   `plotjuggler_sdk::plugin_sdk` and your Qt modules.

```cpp
#include <QPointer>
#include <QtWidgets/QLabel>
#include <pj_plugins/sdk/widget_plugin_base.hpp>

class Battery : public PJ::WidgetPluginBase {
 public:
  std::vector<PJ::WidgetInput> inputs() const override {
    return {PJ::WidgetInput("voltage", "Voltage", "V")};
  }
  void* createWidget(void* parent) override {
    label_ = new QLabel(static_cast<QWidget*>(parent));
    return label_.data();
  }
  void onUpdate(PJ::Span<const PJ_widget_value_t> values, double time) override {
    for (const PJ_widget_value_t& v : values) {  // one entry per wire: dispatch on v.input
      switch (v.input) {
        case 0:  // "voltage"
          if (label_) {
            label_->setText(v.kind == PJ_WIDGET_VALUE_NUMBER ? QString::number(v.number, 'f', 1) : QString("-"));
          }
          break;
      }
    }
  }

 private:
  QPointer<QLabel> label_;  // the destructor deletes it if it is still alive
};

PJ_WIDGET_PLUGIN(Battery, kManifest)
```

## Lifecycle

```
create(type) -> bind -> [load_config] -> inputs() -> create_qwidget(parent)
   -> { on_update | load_config | inputs() }* -> save_config -> destroy
```

Every call is on the GUI thread, except the constant `build_info` and
`get_plugin_extension`, which are thread-safe. One library can offer several widget types:
give the class a constructor taking `std::string_view type`, which
`PJ_WIDGET_PLUGIN` passes through (throw `std::invalid_argument` for an unknown
type, which makes `create` return null). A class without that constructor is
default-constructed. Each open panel is its own instance.

## Manifest

```json
{
  "id": "basic-widgets", "name": "Basic Widgets", "version": "1.0.0",
  "min_sdk_required": "0.37.0",
  "qt_modules": ["Core", "Gui", "Widgets", "Svg"],
  "widgets": [
    { "type": "gauge", "name": "Gauge", "icon": "<svg ...>...</svg>",
      "settings": [
        { "key": "min", "type": "number", "default": 0, "label": "Minimum" },
        { "key": "mode", "type": "enum", "options": ["arc", "bar"], "default": "arc", "label": "Mode" }
      ] }
  ]
}
```

- `widgets` is required and non-empty; a widget manifest without it is invalid.
  Each entry needs `type` and `name`; `icon` and `settings` are optional.
  `type` is unique within the library, non-empty, contains no `/`, and is
  permanent: it is stored in layouts as `plugin_id/type`. `name` and `icon` may
  change between versions.
- `icon` is inline SVG. The Marketplace package carries only the binary and the
  manifest, so there is no separate icon file.
- `qt_modules` lists the Qt modules the library links. Use only modules from
  `pj_plugins/widget_qt_modules.h`, installed as
  `share/plotjuggler_sdk/widget_qt_modules.json` for CI. A module outside the
  list may work on a developer machine and fail on a user's, because the
  official packages do not ship it.
- `settings` is covered below.

The manifest is embedded in the library; the host reads it without creating an
instance.

## Inputs, wires and values

The widget declares named inputs. It never names topics or datasets: the user
wires series to inputs in the host, the host stores the wiring in the layout and
delivers values.

`inputs()` returns `PJ::WidgetInput{name, label, unit, kind, multiple}`:

- `name` is the identity. It must be non-empty, unique and stable; the host
  refuses a list with an empty or duplicate name. It matches wirings across
  `load_config` and `inputs_changed` by name, never by position. A name that
  survives keeps its wire, one that disappears loses it, a new one starts empty.
- `kind` is `kNumber` (delivered as `number`), `kText` (`text`) or `kObject`
  (`bytes` + `type_name`, to decode with the codecs in `pj_base`). A kind the
  host does not know makes it refuse the list.
- `multiple = false` takes one series (dropping another replaces it).
  `multiple = true` takes any number, one slot each. Use it only where all
  series are treated identically (a table of values, a list of indicators), never
  for inputs with their own meaning (battery voltage, a needle). Over the C ABI
  it is the `PJ_WIDGET_INPUT_MULTIPLE` bit of `PJ_widget_input_t::flags`; readers
  ignore bits they do not know.

The list may depend on settings. The host asks again after `create`, after
`load_config` and after `notify_inputs_changed()`; the list never changes in the
middle of `onUpdate`. To report a failure, throw from `inputs()`: the C slot
returns false with the message, and the host keeps the inputs (and wires) it
had instead of reading the failure as "no inputs". The array returned over the C
ABI stays valid until the next successful `inputs()`, the next `load_config()` or
destroy.

`onUpdate(values, time)` is called on the host's tracker tick when a value
changed. The host also calls it, even when no sample changed, after
`createWidget`, after every accepted `loadConfig` and after any wiring change,
so the widget never paints stale state.

`values` holds one `PJ_widget_value_t` per wire, not per input: an unwired input
has no entry, a `multiple` input has one per wired series (`slot`). Entries are
ordered by `input` index, then `slot`, but dispatch on `v.input` (and `v.slot`),
never on the position in `values`. `kind == PJ_WIDGET_VALUE_NONE` where the wire
has no sample at or before `time`. `time` is on the host's display axis, in seconds.
`label` is the host's display name of the series behind the wire (for example
`topic/field`), set for every kind so a widget that lists its wires, like a
table of values, can name each row; it may be empty. Every pointer in a value
(`text`, `bytes`, `type_name`, `label`) is valid only during the call: copy what
you keep. Repaint after `onUpdate`; do not read data inside
`paintEvent`.

An exception thrown from `onUpdate` does not reach the host. The base reports it
once through `reportWidgetMessage` (level error) and stays silent on the
following failing ticks until a call succeeds again.

## History

History is requested, not pushed:

```cpp
auto total = widgetHost().queryHistory(input, slot, from, to, /*max_samples=*/500,
                                       [&](const PJ_widget_value_t& v) { points_.push_back({v.time, v.number}); });
if (total && *total > points_.size()) {
  // the host decimated the range: points_ is a reduced history
}
```

The callback is called once per sample, in ascending time, before `queryHistory`
returns; the value is valid only inside the callback. It returns `void` (take
every sample) or `bool` (`false` stops the delivery; the call still succeeds).
`max_samples == 0` lets the host pick a cap. On success `queryHistory` returns
the number of samples the range held before the host's cap or decimation. It
fails when the wire does not exist, the host does not provide history, or the
callback threw (the delivery stops at the throw and the message is returned).

Call it from any plugin method (`onUpdate`, `loadConfig`, ...) or from the
widget's own Qt slots, on the main thread, never from `paintEvent`, and keep your
own copy. Off the main thread and inside a paint handler are the only cases the
host may refuse.

`widgetHost()` also offers `notifyInputsChanged()` and
`reportWidgetMessage(level, text)`. Read them after `bind` succeeds
(`widgetHostBound()`); the default `bind` requires `pj.widget_host.v1`.

## Settings and configuration

Settings are what the user changes without programming: a range, a threshold, a
color. The plugin has no UI or storage for them. It declares them in the
manifest, per widget type:

| Field | Meaning |
|---|---|
| `key` | JSON key in the configuration object |
| `type` | `number`, `integer`, `boolean`, `text`, `color`, `enum` |
| `min`, `max` | optional limits for `number` and `integer` |
| `options` | values for `enum` |
| `default` | initial value |
| `label` | form label |

`integer` is a whole number: the host shows it without decimals and stores a JSON
integer (use it for counts such as `decimals`); `number` is a double.

The host draws the form, stores the complete JSON in the layout and calls
`loadConfig(json)` on every change. Restoring a layout is the same call, so
there is one path in. `loadConfig` rereads the keys it knows, ignores the rest,
and repaints; return an error `Status` for a value it cannot accept.
`saveConfig()` returns extra internal state to persist (default `"{}"`); a
widget that only has settings does not override it.

What `loadConfig` receives is a JSON object holding every key declared in the
widget's `settings` (the host fills in the manifest `default` for a key the user
never touched), with whatever `saveConfig` last returned laid over it. A
`saveConfig` key that collides with a setting key wins, so do not reuse setting
names for internal state. A widget may keep fallbacks in code, but the manifest
is the single source of defaults.

Because the SDK stores `settings` as raw JSON, the manifest decoder
(`PluginDescriptor::widgets[].settings_json`) checks only that it is an array.
The host interprets the entries.

The form is declarative on purpose. A widget that outgrows it is not stuck: a
later tail slot (`create_settings_qwidget`, a settings editor the plugin builds)
or a dynamic `settings_schema` can be added without touching protocol v1.

## Qt rules

- **Same Qt as the host.** The plugin records the Qt version it compiled against
  (`build_info`, filled by `PJ_WIDGET_PLUGIN` from `QT_VERSION_*`, so the
  translation unit that expands it must include a Qt header). The host requires
  the same major.minor and refuses to create the widget otherwise. Build with
  the official Qt matching the host's, not a distribution package of another
  minor.
- **No Qt private headers and no QRhi.** They tie the plugin to the exact minor.
- **Modules from the canonical list only** (`widget_qt_modules.h`).
- **Linux: build on Ubuntu 22.04.** The host needs `GLIBCXX_3.4.30`; a library
  built with a newer libstdc++ fails in `dlopen`. **Windows: MSVC**, the
  compiler of the official builds.
- **Release and debug runtimes do not mix on MSVC.** `build_info` carries a debug
  flag and the compiler family; a mismatch is refused. Hosts compute their own
  side with `PJ::widgetCompilerFamily()` / `PJ::widgetBuiltAsDebug()`
  (`pj_plugins/widget_types.hpp`, Qt-free), the same functions the SDK uses to
  fill `build_info`, so both sides agree by construction.
- **Export only the plugin entry points.** `pj_configure_plugin(... FAMILIES widget)`
  applies the version script that keeps `PJ_get_widget_vtable` and
  `pj_plugin_abi_version` exported and hides everything else.

## Ownership, threads, unloading

- The plugin creates and owns the widget. The host removes it from its parent
  (`setParent(nullptr)`) and then destroys the instance. Destruction is
  synchronous: your destructor deletes the widget if it is still alive. Hold it
  in a guarded pointer (`QPointer`), because it may already be gone. Never call
  `deleteLater` on objects the plugin owns.
- Every call, in both directions, is on the main thread (the constant
  `build_info` and `get_plugin_extension` are also safe from other threads). Do
  not start threads that touch the widget or the host service.
- A Widget library is never unloaded: `WidgetLibrary` itself keeps the DSO
  mapped for the process lifetime (Qt calls into the widget at times the host
  does not control), so the host needs no keep-alive store and vtable and
  `buildInfo()` pointers stay valid. Install adds a library in place; update
  and removal take effect at the next start.
- A widget only consumes. It creates no series and no datasets.

## Host side

Hosts use `PJ::WidgetLibrary::load(path)` (or `loadStatic(vtable)` for a
statically linked plugin), read `buildInfo()` before any instance, compare it
with the running Qt, then `createHandle(type)` per panel. The loader validates
`build_info` (non-null, at least the v1 size, sane string views) together with
the vtable, and pins a library only once it passed. `WidgetHostBridge` turns
three `std::function` callbacks (`history`, `inputs_changed`, `report_message`)
into the `pj.widget_host.v1` service (`PJ_WIDGET_HOST_SERVICE_V1`) to register
before `bind`; `history` returns the range's total (`Expected<uint32_t>`) and
stops when its sink returns `false`. `WidgetHandle` exposes `inputs()`
(`Expected`, failing on a plugin error or on an invalid list, so a failure never
looks like "no inputs"), `createQWidget(parent)`, `onUpdate`, `saveConfig` and
`loadConfig`; its destructor runs the synchronous `destroy`.
The catalog (`scanPluginDsos`, `inspectPluginDso`) reports
`PluginFamily::kWidget` with `widgets` and `qt_modules` filled.

## Compatibility

`PJ_widget_vtable_t`, `PJ_widget_host_vtable_t` and `PJ_widget_build_info_t`
are ABI-appendable: new members go at the tail and are read behind
`struct_size`; `PJ_WIDGET_MIN_VTABLE_SIZE` never grows. `PJ_widget_input_t` and
`PJ_widget_value_t` are ABI-frozen: they travel in arrays, and a reader with a
different `sizeof` would walk them at the wrong stride, so they never change and
growth goes through a new tail slot (an `inputs_v2`, say). Inside them, an
unknown input `kind` is an error on the host side; unknown `flags` bits are
ignored. A manifest
`min_sdk_required` of `0.37.0` is the floor for a Widget plugin, since the
family and `pj.widget_host.v1` first shipped in 0.37.0.
