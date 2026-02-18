# PlotJuggler SISO and MIMO Function Analysis (`../PlotJuggler`)

## Scope

This analysis covers how PlotJuggler implements transform functions in:

- `plotjuggler_base`
- `plotjuggler_app`
- `plotjuggler_plugins` (especially Lua and quaternion toolboxes)

The core finding is that PlotJuggler has:

1. A generic MIMO-capable transform interface.
2. A built-in SISO pipeline used for per-curve filters.
3. A Lua-based custom function pipeline that is primarily MISO (multi-input, single-output).
4. A reactive Lua scripting path that can behave as practical MIMO (many inputs, many outputs).

---

## 1. Core Transform Contract

`TransformFunction` is the central abstraction and is explicitly documented as multi-input/multi-output (`plotjuggler_base/include/PlotJuggler/transform_function.h:18`).

Key contract points:

- `numInputs()` and `numOutputs()` define arity (`plotjuggler_base/include/PlotJuggler/transform_function.h:45`, `plotjuggler_base/include/PlotJuggler/transform_function.h:50`).
- `setData(...)` validates source/destination vector sizes when arity is fixed (`plotjuggler_base/src/transform_function.cpp:25`, `plotjuggler_base/src/transform_function.cpp:30`).
- Functions are assigned a construction-time execution `order` used by MainWindow scheduling (`plotjuggler_base/src/transform_function.cpp:13`, `plotjuggler_app/mainwindow.cpp:2336`).

Implication: the framework itself is MIMO-capable; specific behavior depends on the concrete subclass.

---

## 2. SISO Path (Built-in Curve Transforms)

### 2.1 How SISO transforms are defined and registered

- Built-in transforms inherit `TransformFunction_SISO` (`plotjuggler_base/include/PlotJuggler/transform_function.h:88`).
- They are registered at startup through `TransformFactory::registerTransform<...>()` (`plotjuggler_app/main.cpp:188` to `plotjuggler_app/main.cpp:198`).
- Examples: derivative, moving average, integral, variance.

### 2.2 Where they run

SISO transforms are attached per displayed curve via `TransformedTimeseries`, not via the global transform map:

- Wrapper class: `TransformedTimeseries` (`plotjuggler_base/src/timeseries_qwt.h:74`).
- Transform binding: `setTransform(...)` creates plugin instance from factory (`plotjuggler_base/src/timeseries_qwt.cpp:92`).
- Runtime update: `PlotWidget::updateCurves(...) -> series->updateCache(...)` (`plotjuggler_app/plotwidget.cpp:1199`, `plotjuggler_app/plotwidget.cpp:1204`).

> **Design implication for new engine:** SISO transforms in PlotJuggler are **display-lazy** — they execute inside the render path, triggered when a curve is drawn, not when data arrives. Because they live in the display layer (`TransformedTimeseries`), the same underlying data series can appear with a different SISO transform in each plot widget simultaneously.
>
> The new engine's `DerivedEngine` is **data-eager**: transforms run at commit time, write permanent derived series into `TopicStorage`, and are visible to all readers. This is a deliberate architectural break. The consequence is that a derived series is a first-class data series (queryable, evictable, benchmarkable), not a view-layer decoration.

### 2.3 SISO execution semantics

`TransformFunction_SISO::calculate()`:

- Uses one input and one output (`plotjuggler_base/src/transform_function.cpp:46`, `plotjuggler_base/src/transform_function.cpp:47`).
- Iterates source points and calls `calculateNextPoint(index)` (`plotjuggler_base/src/transform_function.cpp:67`).
- Supports incremental behavior using `_last_timestamp` (`plotjuggler_base/include/PlotJuggler/transform_function.h:115`).

Concrete transforms store state in class fields and reset it in `reset()`.
Example: moving average ring buffer and reset (`plotjuggler_app/transforms/moving_average_filter.cpp:25` to `plotjuggler_app/transforms/moving_average_filter.cpp:29`).

### 2.4 Configuration and persistence

- UI options are provided through `optionsWidget()` and serialized with `xmlSaveState/xmlLoadState`.
- Transform state is saved per curve inside PlotWidget XML (`plotjuggler_app/plotwidget.cpp:708` to `plotjuggler_app/plotwidget.cpp:712`).
- On layout load, transform is recreated and options reloaded (`plotjuggler_app/plotwidget.cpp:838`, `plotjuggler_app/plotwidget.cpp:847`).

---

## 3. Lua Custom Function Editor: MISO Pipeline

This is the path users most often refer to for "Lua transforms".

### 3.1 Data model and arity

`CustomFunction`:

- `numInputs() = -1` (dynamic input list) (`plotjuggler_app/transforms/custom_function.h:49`).
- `numOutputs() = 1` (`plotjuggler_app/transforms/custom_function.h:54`).

So this pipeline is multi-input/single-output by design, not true multi-output MIMO.

### 3.2 Source binding model

Each snippet stores:

- one `linked_source` (main timeline),
- optional `additional_sources` (`v1`, `v2`, ...),
- Lua global block,
- Lua function body (`plotjuggler_app/transforms/custom_function.h:19` to `plotjuggler_app/transforms/custom_function.h:26`).

At calculate time:

- main source and additional channels are resolved from `PlotDataMapRef` (`plotjuggler_app/transforms/custom_function.cpp:73` to `plotjuggler_app/transforms/custom_function.cpp:91`).
- iteration runs on the main source index (`plotjuggler_app/transforms/custom_function.cpp:105`).

### 3.3 Lua call signature and limits

`LuaCustomFunction` wraps user code into:

`function calc(time, value, v1, v2, ...)` (`plotjuggler_app/transforms/lua_custom_function.cpp:39` to `plotjuggler_app/transforms/lua_custom_function.cpp:45`).

Hard cap: at most 8 additional sources (9 total value arguments including main) via explicit dispatch switch (`plotjuggler_app/transforms/lua_custom_function.cpp:83` to `plotjuggler_app/transforms/lua_custom_function.cpp:114`).

### 3.4 Time alignment strategy

For each main-source timestamp, each additional source value is sampled by nearest timestamp using `getIndexFromX` (`plotjuggler_app/transforms/lua_custom_function.cpp:68`, `plotjuggler_base/include/PlotJuggler/timeseries.h:146`).

This is nearest-neighbor lookup, not interpolation.

### 3.5 Allowed Lua returns

`LuaCustomFunction` accepts:

1. One number -> `(old_time, y)`.
2. Two numbers -> `(x, y)`.
3. One table of `{x,y}` pairs -> multiple points appended to same output series.

Implemented at `plotjuggler_app/transforms/lua_custom_function.cpp:123` to `plotjuggler_app/transforms/lua_custom_function.cpp:157`.

Important nuance: "multiple points" here still means one destination series, not multiple output series.

> **Clarification:** The table return form is an **output density** mechanism, not a multi-output mechanism. It lets a single Lua call emit several `(time, value)` samples into the one destination series (e.g. upsampling or event burst expansion), but it cannot write to a second series. A user who wants to generate `roll`, `pitch`, and `yaw` from one Lua function cannot do so with `CustomFunction`/`LuaCustomFunction` — they must use `ReactiveLuaFunction` (section 4) instead.

### 3.6 Editor behavior

In `FunctionEditorWidget`:

- Tab 1 creates one Lua custom output (`plotjuggler_app/transforms/function_editor.cpp:635` to `plotjuggler_app/transforms/function_editor.cpp:644`).
- Tab 2 ("batch") creates many independent outputs by applying one script to many selected sources (`plotjuggler_app/transforms/function_editor.cpp:646` to `plotjuggler_app/transforms/function_editor.cpp:663`).

Batch mode gives 1->N authoring convenience, but each output is still a separate single-output transform instance.

---

## 4. Reactive Lua Scripts: Practical MIMO

The `ToolboxLuaEditor` path is the closest built-in Lua mechanism to true MIMO.

### 4.1 Runtime model

`ReactiveLuaFunction`:

- `numInputs() = -1`, `numOutputs() = -1` (`plotjuggler_base/include/PlotJuggler/reactive_function.h:77`, `plotjuggler_base/include/PlotJuggler/reactive_function.h:82`).
- Executes `calc(tracker_time)` on:
  - tracker changes (`plotjuggler_app/mainwindow.cpp:459` to `plotjuggler_app/mainwindow.cpp:469`),
  - data update cycle (`plotjuggler_app/mainwindow.cpp:2340`),
  - playback loop (`plotjuggler_app/mainwindow.cpp:2676`).

> **Execution model distinction:** `ReactiveLuaFunction` is **event-driven and display-synchronous** — `calc(tracker_time)` fires once per UI event (tracker move, data refresh, playback tick), not once per data row. The Lua script receives the current playback cursor position and is expected to read series and write outputs using the full-history Lua API (`TimeseriesView.find`, `Timeseries.new`).
>
> This makes it unsuitable as a direct model for the new engine's incremental scheduler, which must process only *new* chunks since the last commit. A `ReactiveLuaFunction` has no concept of "rows I haven't seen yet" — it always has access to the entire series history and re-reads it each call. The new engine's `IMIMOTransform::calculate()` is per-row and incremental by design.

### 4.2 Lua-side APIs exposed

From `prepareLua()`:

- `TimeseriesView.find(name)` -> access existing numeric series (`plotjuggler_base/src/reactive_function.cpp:103`).
- `Timeseries.new(name)` -> create/update timeseries output (`plotjuggler_base/src/reactive_function.cpp:122`).
- `ScatterXY.new(name)` -> create/update scatter output (`plotjuggler_base/src/reactive_function.cpp:142`).
- built-in `GetSeriesNames()` helper (`plotjuggler_base/src/reactive_function.cpp:160`).

API supports reading many inputs and creating many outputs in one script pass.

### 4.3 Integration details

- Scripts are managed by `ToolboxLuaEditor` and stored in `TransformsMap` as `ReactiveLuaFunction` (`plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp:271`, `plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp:275`).
- Main update loop excludes reactive scripts from the normal transform pass and runs them separately (`plotjuggler_app/mainwindow.cpp:2342` to `plotjuggler_app/mainwindow.cpp:2347`).
- Generated curves are tracked through `createdCurves()` and pushed into curve list/replot pipeline (`plotjuggler_app/mainwindow.cpp:1712` to `plotjuggler_app/mainwindow.cpp:1716`).

---

## 5. True Fixed-Arity MIMO Example (Non-Lua)

`ToolboxQuaternion` demonstrates direct MIMO usage of the core API:

- transform class `QuaternionToRollPitchYaw` has 4 inputs and 3 outputs (`plotjuggler_plugins/ToolboxQuaternion/quaternion_to_rpy.h:18` to `plotjuggler_plugins/ToolboxQuaternion/quaternion_to_rpy.h:26`).
- toolbox binds four source series and three destination series via `setData(...)` (`plotjuggler_plugins/ToolboxQuaternion/toolbox_quaternion.cpp:194`).
- output series are `roll/pitch/yaw` (`plotjuggler_plugins/ToolboxQuaternion/toolbox_quaternion.cpp:189` to `plotjuggler_plugins/ToolboxQuaternion/toolbox_quaternion.cpp:191`).

So the framework does support strict MIMO transforms natively; Lua custom editor is just not the path that exposes multi-output directly.

---

## 6. Scheduling, Dependency Handling, and Lifecycle

### 6.1 Scheduling

Global transform map functions are executed in construction order (`order`) each update cycle (`plotjuggler_app/mainwindow.cpp:2330` to `plotjuggler_app/mainwindow.cpp:2337`).

### 6.2 Dependency ordering on layout load

Custom Lua snippets in layout are sorted so dependencies are created first (`plotjuggler_app/mainwindow.cpp:2061` to `plotjuggler_app/mainwindow.cpp:2079`), then each function is calculated and inserted (`plotjuggler_app/mainwindow.cpp:2083` to `plotjuggler_app/mainwindow.cpp:2090`).

### 6.3 Deletion propagation

When deleting curves, PlotJuggler propagates to derived transforms by scanning `dataSources()` (`plotjuggler_app/mainwindow.cpp:1059` to `plotjuggler_app/mainwindow.cpp:1067`).

---

## 7. Persistence Model

- Per-curve SISO transform config is embedded under each plot curve node (`plotjuggler_app/plotwidget.cpp:705` to `plotjuggler_app/plotwidget.cpp:713`).
- Lua custom functions are saved in layout under `customMathEquations` plus snippet library (`plotjuggler_app/mainwindow.cpp:3066` to `plotjuggler_app/mainwindow.cpp:3078`).
- Reactive Lua scripts are persisted by `ToolboxLuaEditor` plugin XML (`plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp:127` to `plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp:163`).

---

## 8. Practical Conclusions

1. If you need per-curve filtering with strong GUI integration and parameter widgets, use built-in SISO transforms.
2. If you need Lua expression logic across several inputs to produce one new timeseries, use Custom Function Editor (MISO).
3. If you need one Lua script to generate or mutate multiple outputs (timeseries and scatter), use Reactive Script Editor (practical MIMO).
4. If you need strict fixed-arity multi-output transforms with deterministic I/O vectors, implement a `TransformFunction` subclass (as done by quaternion toolbox).

In short: PlotJuggler's core is MIMO-capable, but its two Lua paths target different use cases:

- Custom Function Editor: convenient MISO derivations.
- Reactive Script Editor: dynamic multi-series orchestration (MIMO-style behavior).

