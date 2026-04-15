# Toolbox Porting — SDK Gap Analysis

This document provides an exhaustive comparison of PlotJuggler 3.x toolbox plugin
features against the current PJ 4.x SDK capabilities. It identifies what Dialog SDK
extensions are required to port the existing toolboxes with full feature parity.

**Scope:** `plotjuggler_core` (Dialog SDK, `ToolboxPluginBase`) + `pj-official-plugins`
(Quaternion to RPY port as the reference implementation).

**Summary:** The SDK is sufficient for simple, headless processing toolboxes (Quaternion
reaches ~80% feature parity). FFT drops to ~60%. The Lua Reactive Script Editor cannot
be ported without major SDK extensions. Six gaps are blocking and require new
infrastructure: embedded chart widget, zoom event, drag-drop on chart, editable code
editor, reactive time-tick, and ScatterXY output type.

---

## Index

1. [Overview](#1-overview)
2. [PJ 3.x Toolbox Reference](#2-pj-3x-toolbox-reference)
3. [Architectural Differences](#3-architectural-differences)
4. [Gap Block 1 — Embedded Chart Widget](#4-gap-block-1--embedded-chart-widget)
5. [Gap Block 2 — Drag-and-Drop](#5-gap-block-2--drag-and-drop)
6. [Gap Block 3 — Code Editor](#6-gap-block-3--code-editor)
7. [Gap Block 4 — Additional Gaps](#7-gap-block-4--additional-gaps)
8. [Summary Table](#8-summary-table)
9. [Concrete SDK Extensions Required](#9-concrete-sdk-extensions-required)

---

## 1. Overview

### What works

The SDK reports served their purpose: they document with verifiable references how PJ 3.x works internally. The Quaternion port is functionally correct — the math is identical to the original, it compiles, the tests pass with known values, and the mock host validates the complete flow (catalog, read, transform, write, notify).

The toolbox loader is minimal and correct: it follows the exact pattern of `DataSource` and `MessageParser`, adding nothing speculative.

### What falls short

The UX of the ported plugin is far from the original. In PJ 3.x:

- Drag-and-drop of curves with intelligent auto-fill (drop a curve ending in `x` and it fills all four X/Y/Z/W fields automatically)
- Live preview inside the panel before committing
- The toolbox lived as a FIXED panel embedded in the main window

In the current port:

- Static combo boxes (better than typing by hand, but not drag-and-drop)
- No preview — the transform runs directly on OK
- Modal dialog that opens and closes

This is not a failure of the port — it is a limitation of the current SDK: `DialogPluginTyped` does not support preview widgets (charts) or drag-and-drop. Those features require Dialog SDK extensions.

The end-to-end wiring is not yet confirmed at 100%. The plugin loads, the dialog opens with combo boxes, but we have not verified that the transform produces data visible in the plots. There may be a mismatch in catalog field names.

---

## 2. PJ 3.x Toolbox Reference

Three toolbox plugins exist in `plotjuggler/plotjuggler_plugins/`:

| Toolbox | Key Feature | Key UI |
|---------|------------|--------|
| **ToolboxQuaternion** | Convert (x,y,z,w) → roll/pitch/yaw; preview before save | 1× embedded `PlotWidgetBase`; 4× `QLineEdit` (drag-drop target); `QCheckBox` (unwrap); `QRadioButton` (deg/rad) |
| **ToolboxFFT** | Frequency analysis; zoom-aware range selection; constant-dT assumed | 2× embedded `PlotWidgetBase` (input + FFT output); `QRadioButton` (all/zoomed); `QCheckBox` (remove DC); editable suffix |
| **ToolboxLuaEditor** | Reactive Lua script editor; re-executes on every slider move; library management | 3× `QCodeEditor` (global/function/library); `QListWidget` (active + recent functions); green/red validation indicator |

---

## 3. Architectural Differences

| Aspect | PJ 3.x | New SDK |
|--------|--------|---------|
| Plugin owns its UI | Yes — full `QWidget` with arbitrary children | No — `.ui` file + `DialogEngine` |
| Data access | Direct reference to `PlotDataMapRef` | Via handles: `catalogSnapshot`, `readSeries`, `appendRecord` |
| Communication | Qt signals/slots | C ABI vtables + JSON config |
| Qt dependency | Required | Optional (`pj_base` has no Qt) |
| Embedded chart preview | Integrated (`PlotWidgetBase`) | Not available in the SDK |
| Drag-and-drop | Via `eventFilter` in the plugin | Not supported by `DialogEngine` |
| Output type | `PlotData` (time series) **or** `PlotDataXY` (scatter) | Time-indexed series only |
| Transform registry | Yes — re-applied on layout reload | No — outputs are static data |
| Reactive execution | `ReactiveLuaFunction` re-runs on every slider tick | `onTick()` exists but cannot write to datastore or access current timestamp |

---

## 4. Gap Block 1 — Embedded Chart Widget

### 4.1 · Curve add / remove / clear / zoom (CRITICAL — FFT + Quaternion)

PJ 3.x `PlotWidgetBase` core API used by both toolboxes:

```cpp
CurveInfo* addCurve(const std::string& name, PlotDataXY& src_data, QColor color = Qt::transparent);
void       removeCurve(const QString& title);
void       removeAllCurves();
void       resetZoom();
PJ::Range  getVisualizationRangeX() const;
PJ::Range  getVisualizationRangeY(PJ::Range range_X) const;
bool       isEmpty() const;
```

**Quaternion usage:** one preview widget; adds roll/pitch/yaw curves, calls `resetZoom()`, clears before each re-render.

**FFT usage:** two preview widgets; widget A = input time series, widget B = FFT output (X=Hz, Y=amplitude); clears both on "Clear", resets zoom after calculate.

**SDK equivalent needed:**
```cpp
WidgetData& setChartSeries(std::string_view name,
    const std::vector<ChartSeries>& series);  // {label, timestamps[], values[]}
WidgetData& clearChartSeries(std::string_view name);
```

### 4.2 · Curve visual style (MEDIUM)

`PlotWidgetBase` supports 6 curve styles, 4 line widths, matplotlib color palette, and per-curve color inheritance:

| Feature | Detail |
|---------|--------|
| **CurveStyle** | `LINES`, `DOTS`, `LINES_AND_DOTS`, `STICKS`, `STEPS`, `STEPSINV` |
| **LineWidth** | 4 levels: 1.4 / 2.1 / 2.8 / 4.2 px |
| **Color palette** | Matplotlib 8-color cyclic: `#1f77b4`, `#d62728`, `#1ac938`, `#ff7f0e`, `#f14cc1`, `#9467bd`, `#17becf`, `#bcbd22` |
| **Color inheritance** | FFT output inherits source curve's `COLOR_HINT` attribute |
| **Anti-aliasing** | Always enabled (`RenderAntialiased`) |
| **Point markers** | `QwtPlotMarker` per curve, hidden by default |
| **Performance** | `FilterPointsAggressive` decimation for large datasets |

**SDK equivalent needed:**
```cpp
WidgetData& setChartCurveStyle(std::string_view widget_name,
    int curve_index, CurveStyle style, QColor color, LineWidth width);
```

### 4.3 · Interactive legend (LOW)

`PlotWidgetBase` has a collapsible legend with click-to-toggle-visibility per curve:

| Feature | Detail |
|---------|--------|
| Position | Top-Right (configurable via `setLegendAlignment`) |
| Font size | 9 pt initial; Ctrl+wheel resize in range 7–13 pt; emits `legendSizeChanged(int)` |
| Click behavior | Left-click on curve label toggles visibility; if `autozoom_visibility` pref is set, triggers `resetZoom()` |
| Collapse button | Small circle in corner — collapses entire legend |
| Inactive curve text | Grayed out: RGB(122, 122, 122) |

This gap has low impact for toolbox porting (toolboxes don't programmatically control legend visibility), but it is the expected default behavior for any embedded chart.

**SDK equivalent needed:**
```cpp
WidgetData& setChartLegend(std::string_view name,
    bool visible, Qt::Alignment position, int font_size);
// Event (optional):
virtual bool onChartLegendToggle(std::string_view name,
    std::string_view curve_label, bool visible);
```

### 4.4 · Zoom/pan and `viewResized` event (CRITICAL — FFT)

`PlotWidgetBase` exposes zoom/pan interaction and emits a signal on every view change:

| Method / Signal | Description |
|-----------------|-------------|
| `setZoomEnabled(bool)` | Enable/disable zoom+pan |
| `setSwapZoomPan(bool)` | Swap left-click bindings for zoom/pan |
| `setKeepRatioXY(bool)` | Lock aspect ratio (XY scatter mode) |
| `signal viewResized(const QRectF&)` | Emitted on every zoom or pan event |

**Critical for FFT:** `onViewResized(const QRectF& rect)` captures the current X viewport (`rect.left()` / `rect.right()`) to implement the **"Only data in zoomed area"** mode. Without this signal, range-aware FFT processing is impossible.

```cpp
void ToolboxFFT::onViewResized(const QRectF& rect) {
    _zoom_range.min = rect.left();
    _zoom_range.max = rect.right();
}
```

**SDK equivalent needed:**
```cpp
virtual bool onChartViewChanged(std::string_view name,
    double x_min, double x_max, double y_min, double y_max);
```

---

## 5. Gap Block 2 — Drag-and-Drop

PJ 3.x uses a single MIME type for all drag-drop: `curveslist/add_curve` — a `QDataStream` of `QStringList` (curve names). The three toolboxes use it in three completely different ways.

### 5.1 · Drop on plot widget → load curve into preview (CRITICAL — FFT)

FFT calls `_plot_widget_A->setAcceptDrops(true)` and connects `dragEnterSignal` + `dropSignal` from `PlotWidgetBase`. When a curve (or multiple curves) is dropped:

1. Extract `QStringList` from MIME data
2. Call `_plot_data->getOrCreateNumeric(curve_id)` for each
3. Call `_plot_widget_A->addCurve(...)` and `resetZoom()`
4. Enable Calculate and Save buttons

Accepts **multiple simultaneous curves**.

**SDK equivalent needed:** `WidgetData::setAcceptDrops(name, true)` for chart widgets, with a new event:
```cpp
virtual bool onItemsDropped(std::string_view widget_name,
    const std::vector<std::string>& items);
```

### 5.2 · Drop on QLineEdit → fill field + smart auto-fill (HIGH — Quaternion)

Quaternion installs an `eventFilter` on all four `QLineEdit` widgets. On drop:

1. Accept only single-curve drops
2. Fill the target `QLineEdit` with the curve name
3. **Auto-fill logic:** If the dropped curve name ends with `x` (or `y`, `z`, `w`), extract the prefix and check whether all four variants (`prefix+x`, `prefix+y`, `prefix+z`, `prefix+w`) exist in the data map. If yes, fill all four fields and trigger a live preview automatically.

This is the most ergonomic feature of the Quaternion toolbox. Example: dropping `imu/quat/x` onto the X field auto-fills all four and shows the RPY preview immediately.

**SDK equivalent needed:** `WidgetData::setAcceptDrops(name, true)` for `QLineEdit` or `QComboBox`, with:
```cpp
virtual bool onItemsDropped(std::string_view widget_name,
    const std::vector<std::string>& items);
// Plugin implements auto-fill logic in this handler
```

### 5.3 · Drop on code editor → insert curve name as string literal (MEDIUM — Lua)

Lua editor installs an `eventFilter` on all three `QCodeEditor` widgets. On drop, it inserts each dropped curve name as a quoted string literal on a new line:

```lua
"imu/quaternion/x"
"imu/quaternion/y"
```

Accepts multiple curves; each becomes one line. This allows users to quickly reference series in their Lua code without typing.

**SDK equivalent needed:** Same `onItemsDropped` event, but on code editor widgets.

---

## 6. Gap Block 3 — Code Editor

### 6.1 · Editable code widget with syntax highlighting (CRITICAL — Lua)

The Lua editor uses `QCodeEditor` (external library) with:

| Feature | Detail |
|---------|--------|
| Widget | `QCodeEditor` — editable `QPlainTextEdit` subclass |
| Highlighter | `QLuaHighlighter` — Lua keyword/token coloring |
| Completer | `QLuaCompleter` — auto-completion popup |
| Instances | 3: global code, function body, library |
| Font size | Ctrl+wheel: 8–14 pt range, persisted to `QSettings` |

The current SDK has `setPlainText()` for **read-only** text display only. The dialog docs explicitly state that `QTextEdit` and `QPlainTextEdit` are **not supported** by the widget binding system. Without an editable code widget, the Lua editor cannot exist.

**SDK equivalent needed:**
```cpp
WidgetData& setCodeContent(std::string_view name, std::string_view code);
WidgetData& setCodeLanguage(std::string_view name, std::string_view language); // "lua", "python"
virtual bool onCodeChanged(std::string_view name, std::string_view code);
```

### 6.2 · Real-time validation indicator (HIGH — Lua)

The library tab has a green/red SVG circle indicator that validates Lua code in near-real-time:

1. `textChanged` signal fires
2. After a 250 ms delay (`DelayedCallback` pattern), attempt to construct a `ReactiveLuaFunction` with the current library code
3. On success: green circle, tooltip "Everything is fine :)"
4. On failure: red circle, tooltip = exception message from Lua runtime

This gives the user immediate feedback on library syntax errors before saving.

**SDK equivalent needed:**
```cpp
WidgetData& setWidgetIndicator(std::string_view name,
    IndicatorState state,  // OK, ERROR, WARNING
    std::string_view tooltip);
```

This is different from `setEnabled` — it is a visual status indicator that does not affect interactivity.

### 6.3 · Reactive execution tied to time slider (CRITICAL — Lua)

`ReactiveLuaFunction` (sol2-based) is registered in `_transforms` and re-executed automatically on every time slider movement, receiving the current `tracker_time` as parameter:

```lua
-- User-written function, called on every slider tick:
function(tracker_time)
    local val = TimeseriesRef("imu/x"):atTime(tracker_time)
    CreatedSeriesTime("my_output"):push_back(tracker_time, val * 2.0)
end
```

The sol2 Lua API exposed to scripts:

| Type | Methods |
|------|---------|
| `TimeseriesRef` | `at(i)`, `set(i,x,y)`, `atTime(t)`, `size()`, `clear()` |
| `CreatedSeriesTime` | `at(i)`, `clear()`, `push_back(x,y)`, `size()` |
| `CreatedSeriesXY` | `at(i)`, `clear()`, `push_back(x,y)`, `size()` |

The current SDK `onTick()` in `DialogPluginTyped` fires periodically while the dialog is open, but:
- Has no access to the current time slider value
- Cannot call `toolboxHost().appendRecord()` — the toolbox host is separate from the dialog plugin
- Cannot be used to implement time-reactive series generation

**SDK equivalent needed (toolbox-level):**
```cpp
// In PJ_toolbox_runtime_host_t:
void (*on_time_changed)(void* ctx, int64_t timestamp_ns);

// Plugin implementation:
void MyToolbox::onTimeChanged(int64_t ts) {
    // re-run Lua, call toolboxHost().appendRecord(...)
    runtimeHost().notifyDataChanged();
}
```

---

## 7. Gap Block 4 — Additional Gaps

### 7.1 · ScatterXY output type (CRITICAL — FFT)

FFT's output is not a time series — it is a frequency spectrum: X = Hz, Y = amplitude. In PJ 3.x this is stored as `PlotDataXY` (scatter, no time index):

```cpp
auto& curver_fft = _local_data.getOrCreateScatterXY(curve_id);
curver_fft.pushBack({ Hz, amplitude });  // {x=frequency, y=magnitude}
```

The PJ 4.x datastore only supports time-indexed series (`appendRecord` with timestamp). There is no equivalent of `PlotDataXY` (arbitrary XY pairs). To represent the FFT output faithfully, either:

- The datastore adds a ScatterXY/series type, or
- FFT uses a workaround: treat frequency as timestamp (lossy, semantically wrong), or
- FFT outputs only via the embedded preview chart without saving to the datastore

This is an independent gap from the chart widget — it affects the *save* path, not just the preview.

### 7.2 · Transform registry — re-apply on layout reload (MEDIUM)

In PJ 3.x, toolboxes register their transforms in `_transforms` map:
```cpp
// Quaternion:
_transforms->insert({ prefix + "RPY", transform });

// Lua:
(*_transforms)[name.toStdString()] = lua_function;
```

On layout reload, registered transforms are re-executed, re-generating their output series. This means:
- Quaternion outputs are always consistent with the raw quaternion data
- Lua functions regenerate series dynamically from the current dataset

In PJ 4.x, toolbox outputs are static data written once to a data source. If the user reloads the layout, the output data is still there but cannot be regenerated with different parameters.

**Workaround:** Store full transform parameters in `saveConfig()` and re-run the transform on `loadConfig()`. This is implementable today but requires deliberate design per plugin.

### 7.3 · QSettings fine-grained persistence (LOW — Lua)

The Lua editor persists several settings directly via `QSettings` outside of `saveConfig()`:

| Key | Content |
|-----|---------|
| `ToolboxLuaEditor/recent_functions` | XML: up to 10 recently saved functions (name + code) |
| `ToolboxLuaEditor/library` | Full library code string |
| `ToolboxLuaEditor/fonts_size` | Editor font point size (8–14 pt) |

`saveConfig()` / `loadConfig()` in the new SDK can hold all of this as a JSON blob — no new mechanism needed. The only loss is that the font size is UI state, not plugin state, and would need to be treated as config.

---

## 8. Summary Table

| # | Gap | Quaternion | FFT | Lua | Severity |
|---|-----|:---:|:---:|:---:|:---:|
| 1.1 | Embedded chart: add/remove/zoom curves | CRITICAL | CRITICAL | — | **CRITICAL** |
| 1.2 | Curve styles, colors, line widths | MEDIUM | MEDIUM | — | MEDIUM |
| 1.3 | Interactive legend (collapse, toggle) | LOW | LOW | — | LOW |
| 1.4 | Zoom/pan + `viewResized` event | LOW | CRITICAL | — | **CRITICAL** |
| 2.1 | Drag-drop → chart widget (load curve) | — | CRITICAL | — | **CRITICAL** |
| 2.2 | Drag-drop → input field (smart auto-fill) | HIGH | — | — | HIGH |
| 2.3 | Drag-drop → code editor (insert as string) | — | — | MEDIUM | MEDIUM |
| 3.1 | Editable code widget + syntax highlight | — | — | CRITICAL | **CRITICAL** |
| 3.2 | Real-time validation indicator | — | — | HIGH | HIGH |
| 3.3 | Reactive tick with `tracker_time` | — | — | CRITICAL | **CRITICAL** |
| 4.1 | ScatterXY output type (freq vs amplitude) | — | CRITICAL | — | **CRITICAL** |
| 4.2 | Transform registry (re-apply on reload) | MEDIUM | MEDIUM | HIGH | MEDIUM |
| 4.3 | `QSettings` fine-grained persistence | LOW | LOW | MEDIUM | LOW |

**Feature parity estimate without SDK changes:**

| Toolbox | Parity | Blocking gaps |
|---------|--------|---------------|
| Quaternion | ~80% | No chart preview; no drag-drop auto-fill |
| FFT | ~60% | No chart preview; no drag-drop; no zoom-aware range; no ScatterXY output |
| Lua Editor | ~10% | No editable code widget; no reactive execution |

---

## 9. Concrete SDK Extensions Required

To port all three toolboxes with full feature parity, the following extensions are needed in `plotjuggler_core`:

### 9.1 Embedded chart widget

```cpp
// WidgetData setters:
struct ChartSeries { std::string label; std::vector<double> x; std::vector<double> y; };
WidgetData& setChartSeries(std::string_view name, const std::vector<ChartSeries>& series);
WidgetData& setChartCurveStyle(std::string_view name, int index,
                               CurveStyle style, std::string_view color_hex, int line_width);
WidgetData& setChartLegend(std::string_view name, bool visible, int font_size);
WidgetData& setChartAcceptDrops(std::string_view name, bool accept);

// DialogPluginTyped event handlers:
virtual bool onChartViewChanged(std::string_view name,
                                double x_min, double x_max,
                                double y_min, double y_max);
virtual bool onItemsDroppedOnChart(std::string_view name,
                                   const std::vector<std::string>& items);
```

### 9.2 Drag-and-drop on standard widgets

```cpp
// WidgetData setter (for QLineEdit, QComboBox):
WidgetData& setAcceptDrops(std::string_view name, bool accept);

// DialogPluginTyped event handler:
virtual bool onItemsDropped(std::string_view widget_name,
                            const std::vector<std::string>& items);
// Plugin implements auto-fill or insertion logic in this handler
```

### 9.3 Code editor widget

```cpp
// WidgetData setters:
WidgetData& setCodeContent(std::string_view name, std::string_view code);
WidgetData& setCodeLanguage(std::string_view name, std::string_view lang); // "lua", "python"

// DialogPluginTyped event handler:
virtual bool onCodeChanged(std::string_view name, std::string_view code);

// Validation indicator:
WidgetData& setWidgetIndicator(std::string_view name,
                               IndicatorState state, std::string_view tooltip);
```

### 9.4 Time-synchronized reactive execution

```cpp
// New callback in PJ_toolbox_runtime_host_t (C ABI):
typedef struct {
    void (*on_time_changed)(void* ctx, int64_t timestamp_ns);
} PJ_toolbox_time_callbacks_t;

// C++ wrapper in ToolboxPluginBase:
virtual void onTimeChanged(int64_t timestamp_ns) {}
```

### 9.5 ScatterXY data type in the datastore

```cpp
// New topic type or field type in pj_datastore:
FieldHandle ensureScatterField(TopicHandle topic,
                               std::string_view x_name, std::string_view y_name);
void appendScatterPoint(FieldHandle field, double x, double y);
```

Or alternatively, a dedicated write path:
```cpp
void appendArrowScatterIpc(TopicHandle topic,
                           const std::string& x_col, const std::string& y_col,
                           const std::vector<double>& x, const std::vector<double>& y);
```

---

*Gaps 1.1, 1.4, 2.1, 3.1, 3.3, and 4.1 are blocking for their respective toolboxes and require new SDK infrastructure. The remaining gaps represent UX degradation that can be partially mitigated with workarounds within the current SDK.*
