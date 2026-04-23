# PlotJuggler 4.x Application — Implementation Plan

## Context

PlotJuggler 3.x (PJ3) shipped as a monolithic Qt app where a ~3900 LOC `MainWindow` owned storage, plugins, playback, layout, transforms, and persistence. The rot was in the coupling, not the algorithms. The `plotjuggler_core` repository replaces the storage/plugin foundation with a cleanly modularized C++20 stack: `pj_base` (SDK types), `pj_datastore` (columnar engine + ObjectStore + DerivedEngine), `pj_plugins` (C-ABI plugin protocol, 4 families), `pj_media` (2D/video backed by ObjectStore), and `pj_marketplace` (assumed done).

This plan covers the **application layer** on top of that foundation. Goals:

- Reach **parity-plus with PJ3** (all PJ3 features + marketplace integration + multi-modal widgets) in v1.
- **Smaller, more maintainable** than PJ3 by dividing the app code into independent modules with narrow contracts.
- **Multi-modal widgets by design**: plot (Qwt, lifted from PJ3), 2D (pj_media QRhi, existing), 3D (deferred; door-open).
- Plugins and marketplace already ported — not in scope here.

The companion strategic document is `PJ4_PLAN.md` ("why"). This plan is the tactical "how" and "what exactly gets built".

## Architectural Decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| Widget families | **Three independent**: plot (Qwt), 2D (QRhi), 3D (TBD) | Heterogeneity is by design; each family owns its rendering world |
| Plot renderer | Qwt, unchanged from PJ3 | Proven; lift existing code wholesale |
| 2D renderer | QRhi via existing `pj_media` | Already mature (60 tests, ASAN-clean) |
| 3D renderer | Door-open; not implemented in v1 | Reserve `IDataWidget` contract compatibility |
| Qt boundary in `pj_app_core` | **Qt allowed, no QWidget/QDialog** | QObject/QTimer/QSettings/signals OK; services testable with QCoreApplication |
| Plot code reuse | **Lift wholesale** from PJ3 | Biggest LOC savings; preserves years of UX polish |
| Scripting | **`pj_scripting`** as independent module, Lua today / Python later | Decoupled from GUI and app_core's services layer |
| v1 scope | **Parity-plus** | File + streaming sources, 11 transforms, undo, derived series, Lua editor, reactive scripts, marketplace UI, multi-tab |
| Repo layout | **Monorepo under `plotjuggler_core/`** for now | Keep boundaries clean so a later split into a separate `plotjuggler_app` repo is mechanical |
| Time model | Global tracker; 2D/3D snap to frame, plots redraw marker line | Same as PJ3 |
| Layout format | **JSON** with explicit schema version + stable topic paths | Replaces PJ3's brittle XML widget-tree serialization |
| Undo | **Snapshot-based** (save workspace model, restore on undo) | Simpler than command pattern; matches PJ4_PLAN.md |
| StatePublisher | **Dropped** (Toolbox covers it) | Per PJ4_PLAN.md; not challenged |

## Module Graph

```
plotjuggler_core/  (this repo)
├── pj_base/                  existing — SDK vocabulary, Expected<T>
├── pj_datastore/             existing — columnar + ObjectStore + DerivedEngine
├── pj_plugins/               existing — C-ABI plugin protocol, 4 families
├── pj_media/                 existing — 2D/video core + pj_media_qt
├── pj_marketplace/           existing — assumed done
│
├── pj_scripting/             NEW — language-agnostic scripting
│   └── depends on: pj_base, pj_datastore
│
├── pj_app_core/              NEW — services; Qt allowed, no widgets
│   └── depends on: pj_base, pj_datastore, pj_plugins, pj_scripting, pj_media
│
├── pj_plot_widgets/          NEW — Qwt-based plot widgets (lifted from PJ3)
│   └── depends on: pj_app_core, Qt6, Qwt
│
├── pj_media_widgets_qt/      NEW — thin wrapper over existing pj_media_qt
│   └── depends on: pj_app_core, pj_media, Qt6
│
├── pj_3d_widgets/            NEW — placeholder module for future 3D
│   └── depends on: pj_app_core, Qt6 + (TBD renderer)
│
└── pj_app/                   NEW — main-window shell, Qt ADS docking, menus
    └── depends on: all pj_app_* and widget modules
```

**Rule**: widget modules never depend on each other. Cross-widget concerns (tracker, catalog, layout) flow through `pj_app_core` services. This is the contract that keeps the sibling structure intact.

## The Widget Contract (`IDataWidget`)

One small interface every widget family implements. Lives in `pj_app_core`. Kept intentionally minimal so plot/2D/3D families stay independent.

```cpp
// pj_app_core/include/pj_app_core/i_data_widget.hpp
class IDataWidget {
 public:
  virtual ~IDataWidget() = default;
  virtual void onTrackerChanged(Timestamp t) = 0;        // playback
  virtual void onDataChanged() = 0;                       // new data arrived
  virtual nlohmann::json saveState() const = 0;
  virtual void loadState(const nlohmann::json&) = 0;
  virtual std::vector<TopicRef> subscribedTopics() const = 0;
  virtual QWidget* qwidget() = 0;                         // for docking
  virtual std::string widgetType() const = 0;             // "plot", "media_2d", ...
};
```

**Semantic conventions**:

- Plot widgets: `onTrackerChanged` just moves the vertical marker (cheap redraw of one line).
- Media/3D widgets: `onTrackerChanged` fetches frame at `t` from ObjectStore via `MediaSource::setTimestamp` and requests paint.
- `onDataChanged`: coalesced by `pj_app_core`, called when new chunks arrive; plot widgets repaint, media widgets refresh keyframe index.

## Module Design

### `pj_scripting` — Scripting Engine

Language-agnostic. Lua first; Python later by adding one source file.

```
pj_scripting/
  include/pj_scripting/
    engine.hpp               // IScriptingEngine, ScriptHandle, Value
    value.hpp                // variant of scalar/vector/timestamp
    transform_adapter.hpp    // ScriptSISOTransform, ScriptMIMOTransform
                             //   implementing pj_datastore transform interfaces
  src/
    lua/lua_engine.cpp       // LuaScriptingEngine via sol2 (or port PJ3's wrapper)
  tests/
```

Interface shape:

```cpp
class IScriptingEngine {
 public:
  virtual Expected<ScriptHandle> compile(std::string_view src) = 0;
  virtual Expected<Value> eval(ScriptHandle, std::span<const Value> inputs) = 0;
  virtual void registerFunction(std::string_view name, NativeFn) = 0;
  virtual std::string lastError() const = 0;
  virtual std::string languageName() const = 0;
};
```

**Integration points**:

- `TransformRegistry` instantiates `ScriptSISOTransform` (from `transform_adapter`) and hands it to `DerivedEngine`. The engine doesn't know it's script-backed.
- Lua editor toolbox plugin (already ported) links `pj_scripting` directly.
- Reactive Lua scripts (re-run on tracker move) are implemented as a toolbox plugin using `onTimeChanged(Timestamp)` — no special app_core wiring.
- PJ3's Lua built-in function library (vector ops, filtering helpers) registered via `registerFunction` at app startup so scripts remain compatible.

### `pj_app_core` — Services Layer

Concrete service classes, no interfaces unless there's a real second implementation. Qt allowed for QObject-based signals, QTimer for playback tick, QSettings for user prefs. No QWidget/QDialog.

| Service | Responsibility |
|---|---|
| `SessionManager` | Owns the datastore + ObjectStore. Hosts DataSource plugin lifecycle. Exposes write hosts to plugins. Emits `dataChanged` when new chunks arrive. |
| `PlaybackEngine` | Global tracker time, play/pause, playback rate, follow-live flag. `QTimer`-driven 20 ms tick. Emits `trackerChanged(Timestamp)`. State machine: follow-live auto-disables on user scrub backward. |
| `CatalogModel` | `QAbstractItemModel` wrapping topics/fields for the curve-list view. Supports drag-mime encoding. Updated on `SessionManager::dataChanged`. |
| `WorkspaceManager` | JSON layout save/load. Owns dock tree state, per-widget state, open tabs. Versioned schema with migrations. |
| `WidgetRegistry` | Maps `widgetType` string → factory function. Plot/2D/3D each register their factories at app startup. Shell asks registry to create widgets during layout restore. |
| `TransformRegistry` | Wraps `DerivedEngine`. Registers 11 built-in transforms + script-backed transforms via `pj_scripting::transform_adapter`. Persists transform specs into workspace. |
| `ToolboxManager` | Toolbox plugin lifecycle, modal dialog triggers, `onTimeChanged` dispatch for reactive toolboxes. |
| `UserSettingsService` | App-level preferences (recent files, theme, playback defaults) via `QSettings`. |
| `UndoManager` | Snapshot-based undo. Serializes workspace to JSON before mutation, restores on undo. Stack capped at N entries. |
| `ExtensionCatalogService` | Thin facade over `pj_marketplace` for menu integration. |

**Threading model**: all services live on the UI thread. DataSource plugins run their own threads internally and write to the datastore through the plugin host (mutex inside datastore). `SessionManager` marshals `dataChanged` emissions via `QMetaObject::invokeMethod` to the UI thread so downstream widgets don't need thread awareness.

**Cross-widget concerns**:

- **Linked X-axis zoom** across plot widgets: `PlaybackEngine` also owns a `TimeViewRange` state (min,max) updated when any plot zooms; other plots in the same link-group subscribe. Link-group membership is per-plot-widget state saved in layout.
- **Drag-drop from catalog**: `CatalogModel` emits mime data with stable topic paths; any widget can accept.
- **Right-click actions** that mutate data: widget calls into app_core services, never mutates datastore directly.

#### Key service APIs

**`SessionManager`**

| Method | Purpose |
|--------|---------|
| `loadFile(path, config) → DatasetId` | Import a file via the matching DataSource plugin |
| `startStream(plugin_name, config) → DatasetId` | Begin a streaming session |
| `stopStream(DatasetId)` / `pauseStream()` / `resumeStream()` | Stream lifecycle |
| `deleteDataset(DatasetId)` / `clearAll()` | Data removal |
| `reloadFile(DatasetId)` | Re-import with stored config |

Tracks loaded file history for reload. Emits `datasetAdded`, `datasetRemoved`, `dataChanged`.

**Dataset visibility vs existence**: Hiding a dataset from the UI (e.g., user clicks "Remove") must not destroy the underlying data immediately. The session manager tracks hidden datasets separately. Data is truly deleted only on explicit "Delete data" or on app exit. This prevents UI operations from being destructive without explicit user intent.

**Missing extension recovery**: When loading a workspace that references extensions not currently installed, the load must still succeed. Affected sessions become degraded placeholders visible to the user. Reinstalling the extension should allow rebinding.

**`PlaybackEngine`**

| Method | Purpose |
|--------|---------|
| `setTrackerTime(Timestamp)` / `trackerTime()` | Current tracker position |
| `play(rate)` / `pause()` / `stop()` / `setLoop(bool)` | Playback control |
| `timeRange() → (Timestamp, Timestamp)` | Global min/max |
| `setTimeOffset(bool)` | Remove minimum time for display |
| `setReferenceTime(optional<Timestamp>)` | Reference line for delta comparison |
| `setFollowLive(bool)` | Auto-advance tracker to latest data during streaming |
| Signal: `trackerChanged(Timestamp)` | Widgets subscribe |

**Follow-live policy**: during streaming, the visible time window tracks the newest data. If the user pans/zooms away from the live edge, follow-live auto-disables. Re-enabling is an explicit user action. These rules live in `PlaybackEngine`, not in plot widgets.

**`TransformRegistry`**

| Method | Purpose |
|--------|---------|
| `attachTransform(source_field, transform_name, params) → NodeId` | Create a derived series |
| `detachTransform(NodeId)` | Remove a derived series (cascading) |
| `listAvailable() → vector<TransformDescriptor>` | Enumerate registered transforms |
| `attachScriptTransform(source_field, script, language) → NodeId` | Create a script-backed transform via `pj_scripting` |

Cascading delete: removing a source topic removes dependent transforms.

**`UndoManager`**

| Method | Purpose |
|--------|---------|
| `snapshot(WorkspaceDocument)` | Push state (caller debounces at ~100 ms) |
| `undo() → optional<WorkspaceDocument>` | Pop and return previous state |
| `redo() → optional<WorkspaceDocument>` | Re-apply undone state |

Max depth: 100 snapshots. Data samples themselves are not part of undo — undo covers workspace and configuration, not ingested data.

#### Built-in transforms

Each implemented as an `ISISOTransform` in `pj_app_core/src/transforms/`. Algorithms ported from PJ3:

| Transform | PJ3 source | Notes |
|-----------|------------|-------|
| `DerivativeTransform` | `first_derivative.cpp` | Already exists in `pj_datastore` |
| `IntegralTransform` | `integral_transform.cpp` | Cumulative sum |
| `AbsoluteTransform` | `absolute_transform.cpp` | `abs(value)` |
| `ScaleTransform` | `scale_transform.cpp` | Configurable scale + offset |
| `MovingAverageFilter` | `moving_average_filter.cpp` | Configurable window size |
| `MovingRmsFilter` | `moving_rms.cpp` | RMS over sliding window |
| `MovingVarianceFilter` | `moving_variance.cpp` | Variance over sliding window |
| `OutlierRemovalFilter` | `outlier_removal.cpp` | Median-based rejection |
| `SamplesCountFilter` | `samples_count.cpp` | Count of samples in window |
| `TimeSincePreviousTransform` | `time_since_previous_point.cpp` | Delta-t between consecutive points |
| `BinaryFilter` | `binary_filter.cpp` | Threshold with dead-band hysteresis |

Each is 50-150 LOC of pure math with no GUI dependency.

### `pj_plot_widgets` — Qwt Plot Widgets (Lifted Wholesale from PJ3)

The plot widget ecosystem from PlotJuggler 3.x is good code; the PJ3 rot was in `MainWindow` coupling, not in these widgets. They are lifted into this module and their data reads are rebound to the new datastore.

**Files lifted from `~/ws_plotjuggler/src/PlotJuggler/plotjuggler_app/`**:

- `plotwidget_base.{h,cpp}`, `plotwidget.{h,cpp}` — base + concrete plot widget
- `plotdocker.{h,cpp}`, `tabbedplotwidget.{h,cpp}` — dock container + tab wrapper
- Zoomers, `AxisTimeOffset`, custom tracker, legend
- Drag-drop handling, context menus, per-curve display transform UI
- `curvelist_*` — curve list panel + filter proxy

**The one seam — `DatastoreCurveAdapter`**:

```cpp
// pj_plot_widgets/include/pj_plot_widgets/datastore_curve_adapter.hpp
class DatastoreCurveAdapter : public QwtSeriesData<QPointF> {
  PJ::SessionManager* session_;
  PJ::TopicRef topic_;
  // Pulls range-queried data via session_->dataReader().rangeQuery(...)
  // Applies min/max-per-pixel downsampling based on viewport pixel width.
};
```

**Data downsampling (required, not optional)**: min/max-per-pixel materializer producing at most `2 * viewport_pixel_width` points. Plot widgets must never ship millions of points directly to Qwt. Lives in `pj_plot_widgets/src/downsampler.cpp` and is used by `DatastoreCurveAdapter`.

**Integration with app_core**:

- `PlotWidget` implements `IDataWidget` (tracker callback redraws vertical marker line only; subscribed topics enumerated for layout save)
- Registers a plot factory with `WidgetRegistry` at module init
- Drops from `CatalogModel` via the standard Qt drag-drop mime protocol
- Linked X-axis zoom groups coordinate through `PlaybackEngine::TimeViewRange` state (membership per-widget, saved in layout)

**Two kinds of transforms** — the distinction stays:

- **Display transforms** — plot-local, ephemeral. Applied at render time, do not create global topics, serialized as part of plot panel state. Examples: derivative-for-display, smoothing-for-display, scale/offset.
- **Named derived series** — persistent, global. Created via `TransformRegistry` → `DerivedEngine`, produce real topics visible in the catalog, plottable on any panel.

Display transforms are cheaper (no datastore overhead) and appropriate for visual exploration. Named derived series are appropriate when the output is a first-class data product.

### `pj_media_widgets_qt` — 2D Media Widgets

Thin wrapper around `pj_media` + `pj_media_qt`. Handles the `IDataWidget` contract, subscribes to `PlaybackEngine::trackerChanged`, calls `MediaSource::setTimestamp(t)` → widget repaints via its QRhi path.

Key types:

- `Media2DDockWidget` — wraps `pj_media_qt::MediaViewerWidget` + adds the `IDataWidget` behavior
- Factory registered with `WidgetRegistry`
- Supports drag-drop of image/video topics from `CatalogModel`

### `pj_3d_widgets` — 3D widget family

Renders robotics 3D data synchronized to the global tracker. **Architecture locked; full implementation is post-v1** (adds ~6-7 weeks on top of app v1). Scaffolding exists during Phase 0 so the `WidgetRegistry` contract is in place.

**Data types rendered (v1 target)**:

- **TF2** — coordinate frame tree, interpolated transform lookup (tree infrastructure, not a drawable)
- **URDF / mesh** — robot model; mesh formats via assimp (STL, OBJ, glTF, COLLADA, FBX, PLY)
- **Pointcloud** — PCD + ROS `PointCloud2`; `GL_POINTS` with scalar-field colormapping
- **Markers** — ROS `MarkerArray`: arrows, boxes, spheres, cylinders, line strips, text
- **Image + Pinhole** — camera frustum wireframe + optional textured near-plane (reuses `pj_media` image decode)
- **OccupancyGrid** — 2D costmap rendered as textured plane in 3D

**Storage (all via ObjectStore)**:

- **Pointcloud**: lazy-fetch (like MCAP images in `pj_media`)
- **Everything else**: in-memory copies
- DataSource plugins write via the existing `object_write_host.push(topic, encoding, bytes, t)` using encoding strings like `ros.tf2`, `ros.markerarray`, `ros.pointcloud2`, `ros.occupancygrid`, `pj.urdf`, `pj.pinhole_intrinsics`. No new write hosts.

**Technology stack (v1 locked)**:

| Piece | Choice |
|---|---|
| GPU abstraction | **QRhi** (consistent with `pj_media`) |
| Widget base | **`QRhiWidget`** subclass; hand-rolled scene (no Qt 3D) |
| Scene model | **Flat per-widget drawable list** (Foxglove-lean; RViz-style per-type config knobs can be added incrementally) |
| 3D math | **GLM** (GLSL-matching `vec3`/`vec4`/`mat4`/`quat`, `glm::slerp` for TF interpolation, header-only conan dep) |
| Mesh loading | **assimp** (conan dep) |
| URDF parsing | **`QXmlStreamReader`** (no pugixml/urdfdom) |
| Pointcloud format decoders | hand-rolled PCD + `PointCloud2` (~200 LOC each) |
| Marker primitive geometries | hand-rolled generators (box / sphere / arrow / cylinder / line strip) |
| Text rendering | **`QPainter` + `QFont` → `QImage` → QRhi texture** (no stb_truetype) |
| Image decoding | reuse `pj_media` |
| Coordinate convention | **ROS Z-up** (graphics Y-up flip applied in camera matrix) |

**Scene & data flow**:

- Each 3D widget owns a flat list of subscribed topics; each topic produces one or more drawables
- Subscribes to `PlaybackEngine::trackerChanged` → queries `ObjectStore::latestAt(topic, t)` → updates drawables
- Per-topic visibility toggle + minimal config (color override for primitives; color source for pointclouds)
- No deep hierarchy — robotics scenes are small

**Caching strategy (minimal)**:

- **Pointcloud drawables**: cache GPU buffers across tracker changes (re-uploading millions of points is expensive)
- **Everything else**: re-decode on tracker change (cheap — meshes rarely change, markers and occupancy are small, pinhole intrinsics near-static)

**TF interpretation layer** (stateful: per-edge time-indexed cache + slerp/lerp interpolation + tree traversal over ObjectStore TF samples) lives **inside `pj_3d_widgets`** for v1. To be reviewed later: if 2D widgets or `pj_app_core` transforms need TF, promote it to a sibling module `pj_tf` (depends only on `pj_base` + `pj_datastore`).

**Interaction**:

- Orbit camera: mouse drag = rotate, wheel = zoom, middle-drag = pan
- Drag-drop topic from `CatalogModel` → widget adds subscription
- Context menu per drawable: visibility, color, delete
- **Click-to-select picking** (for inspection)

**Deferred (post-v1)**:

- Multiple 3D widgets sharing decoded GPU resources (each widget caches independently for v1)
- Advanced pointcloud LOD / voxel downsampling
- Costmap 3D / octomap types
- Richer per-display config UI (RViz-style history/decay/opacity knobs)

**Scope estimate**: ~6-7 weeks focused work. Renders all 5 visual types well with orbit camera, TF-resolved poses, picking, text labels.

**Inspiration source (not a dependency)**: [threepp](https://github.com/markaren/threepp) — a mature MIT-licensed C++20 port of Three.js — is referenced as a reading source when deciding how to implement specific patterns (URDF link/joint traversal, OrbitControls math, Raycaster algorithm, material abstractions). Logic may be ported with attribution; threepp is **never linked** as a runtime dep, preserving the single-GPU-stack property (QRhi only) and zero-external-3D-library property.

### `pj_app` — Main Window Shell

The thin orchestration layer that wires everything together. Houses the shared app-global widgets that are not tied to a widget family.

**MainWindow**:

- **Owns**: `AppSession` from `pj_app_core` (which holds `SessionManager`, `TransformRegistry`, `PlaybackEngine`, `UndoManager`, etc.)
- **Layout**: Splitter — curve list (left) | tabbed Qt ADS dock manager (right, one manager per tab) | time slider (bottom)
- **Toolbar**: file load, stream start, marketplace, buffer size, grid, legend, link zoom, ratio, theme
- **Menu bar**: File (load/save layout, recent files, exit), Edit (undo/redo, preferences), Tools (toolbox plugins), Plugins → Marketplace, Help (cheatsheet, about)

**Shared app-global widgets (live here, not in a widget family)**:

- **Curve list panel** — `QTreeView` + `QSortFilterProxyModel` over `CatalogModel`; search box at top; two sections (data series + derived/custom series); tracker-value column reading `DataReader::latestAt()`; drag source for all widget families; context menu (delete, create transform, refresh).
- **Time slider** — horizontal time navigation bar. Global time range with draggable tracker position, optional reference line marker, play/pause/stop buttons, rate spinbox, loop toggle, DateTime display mode toggle. Drives `PlaybackEngine`.
- **Transform editor dialog** — list of available transforms from `TransformRegistry`; per-transform parameter widgets (auto-generated from metadata or custom `.ui`); preview plot showing original + transformed curve; Apply/Cancel.
- **Function editor (Lua)** — code editor with syntax highlighting (QCodeEditor or QScintilla); function signature template `function calc(time, value, v1, v2, ...) return ... end`; source selection (linked source + additional sources); test/preview button; alias name input. Bound to `pj_scripting::LuaScriptingEngine`.
- **Preferences dialog** — plugin directories, theme, default buffer size, time display format, swap pan/zoom. Persisted via `QSettings`.
- **Toast notifications** — lifted from PJ3 `toast_manager.cpp` / `toast_notification.cpp` (~200 LOC, self-contained).

**Signal wiring (the "glue")**:

| Signal | Reaction |
|---|---|
| Plot zoom changed | Linked zoom propagation to all other plots in same link-group |
| Tracker moved (from plot or slider) | `PlaybackEngine::setTrackerTime` → all widgets get `onTrackerChanged` |
| Data changed (from session manager) | Refresh `CatalogModel`, coalesced `onDataChanged` to all widgets |
| Transform attached/detached | `DerivedEngine` schedule + replot |
| Undo/redo | Snapshot/restore `WorkspaceDocument`, apply via `loadState` to all widgets |

## Gap Analysis: PJ3 Features vs 4.x Status

### MUST-HAVE (core app experience — day-1)

| # | Feature | PJ3 | 4.x Status | Work Required |
|---|---|---|---|---|
| 1 | Multi-plot panel system (tabs + docking + splitting) | TabbedPlotWidget → PlotDocker → PlotWidget | Lifted from PJ3 | Data-read rebind only |
| 2 | Linked horizontal zoom across plots | `onPlotZoomChanged` fan-out | None | `PlaybackEngine::TimeViewRange` + link-group membership in layout |
| 3 | Time slider + tracker crosshair + playback | RealSlider, CurveTracker, `_publish_timer` | None | `PlaybackEngine` + time slider widget + tracker in `PlotWidget` |
| 4 | Curve list with filtering and tracker values | CurveListPanel (2 CurveTreeViews, filter, drag) | None | Filter proxy, value column, two-section layout on `CatalogModel` |
| 5 | Layout save/load (JSON) | Full XML serialization of widget tree | None | `WorkspaceDocument` + save/load actions; per-widget `saveState`/`loadState` |
| 6 | Undo/redo | 100-snapshot deque of XML | None | `UndoManager` + snapshot wiring |
| 7 | Per-curve display transforms | DialogTransformEditor + 11 builtins + TransformFactory | `DerivedEngine` exists, only `DerivativeTransform` ported | Port 10 builtins, `TransformRegistry`, transform editor dialog |
| 8 | Data deletion and lifecycle | `deleteAllData`, `onDeleteMultipleCurves`, file reload | Engine reconstruction, dataset hide | Per-topic deletion, cascading cleanup, reload |
| 9 | 2D media alongside plots | Not supported in PJ3 | `pj_media` mature, no widget integration | `pj_media_widgets_qt` `Media2DDockWidget` |

### SHOULD-HAVE (important but not day-1)

| # | Feature | PJ3 | 4.x Status | Work Required |
|---|---|---|---|---|
| 10 | Custom derived series (Lua scripting) | FunctionEditorWidget + LuaCustomFunction + ReactiveLuaFunction | `DerivedEngine` supports SISO/MIMO | `pj_scripting` + `transform_adapter` + function editor in `pj_app` |
| 11 | Toolbox plugin integration | Widget stack, init with data+transforms, `importData` signal | Full C ABI + host exists, not wired in app | `ToolboxManager`, UI panel, dialog engine binding |
| 12 | XY plot mode | `addCurveXY`, `setModeXY` | None | Mode in `PlotWidget` (part of the lift), timestamp-join logic |
| 13 | Theme support (light/dark) | QSS stylesheets, icon sets, preferences | None | QSS files, icon resources, preference toggle |
| 14 | Multi-file loading (merge/replace/prefix/reload) | `loadDataFromFiles` with dialogs | Single file loading | Merge dialog, prefix, history, recent menu |
| 15 | Preferences dialog | PreferencesDialog with QSettings | None | Dialog + settings |
| 16 | Reactive Lua scripts | ReactiveLuaFunction, re-exec on tracker move | None | Toolbox plugin using `onTimeChanged` + `pj_scripting` |
| 17 | Marketplace install UI | N/A | `pj_marketplace` done | Plugins → Marketplace menu entry |

### NICE-TO-HAVE (can be deferred post-v1)

| # | Feature | Effort |
|---|---|---|
| 18 | Statistics dialog (per-curve min/max/mean/stddev) | Small |
| 19 | Toast notifications (port ~200 LOC) | Small |
| 20 | Colormap editor + plot backgrounds | Small |
| 21 | Cheatsheet/help dialog | Trivial |
| 22 | Fullscreen mode (F10) | Trivial |
| 23 | Plot save to file (PNG/SVG) + copy to clipboard | Small |
| 24 | Drag-and-drop file loading on main window | Trivial |
| 25 | Streaming notifications panel | Small |
| 26 | 3D widget family (any renderer) | Large — post-v1 |
| 27 | StatePublisher plugin protocol | Large — only if Toolbox doesn't suffice |
| 28 | PJ3 `.xml` layout import adapter | Medium — optional tool |

## Layout JSON Shape

```json
{
  "schema_version": 1,
  "datasets": [{"id": "ds1", "source_plugin": "ros2_bag", "config": "..."}],
  "tabs": [{
    "name": "Main",
    "dock_tree": "<ads-serialized-string>",
    "link_groups": [{"id": "g1", "axis": "x"}]
  }],
  "widgets": [{
    "type": "plot",
    "id": "w1",
    "tab": "Main",
    "link_group": "g1",
    "subscribed_topics": ["ds1/imu/accel/x"],
    "state": {"display_transforms": [...], "y_range": [...]}
  }],
  "transforms": [{"name": "accel_filtered", "kind": "lua_siso", "input": "ds1/imu/accel/x", "script": "..."}],
  "toolboxes": [{"type": "lua_editor", "state": {...}}],
  "playback": {"tracker_t": 0, "rate": 1.0, "follow_live": true}
}
```

Topic references are **stable paths** (`dataset/topic/field`), not session-local IDs. This is what allows reopening a layout against a different recording.

All internal logic uses `int64_t` nanoseconds (matching `pj_datastore`). Display conversion to seconds or datetime happens only at the widget layer (time slider, axis labels).

## Delivery Phases

Phases are sized for incremental verification. Each ends with build + tests + clang-tidy green.

### Phase 0 — Scaffolding (~1 week)

- Add empty module directories + CMakeLists for `pj_scripting`, `pj_app_core`, `pj_plot_widgets`, `pj_media_widgets_qt`, `pj_3d_widgets`, `pj_app`
- Wire into top-level CMake
- Update `conanfile.txt` with new deps (Qwt, sol2, QCodeEditor/QScintilla for later)
- Minimal `pj_app/main.cpp` that launches empty main window
- Tag `pj_proto_app` as deprecated in CMake
- **Verification**: top-level build succeeds, existing tests green, new empty targets build

### Phase 1 — Scripting + Core Services (~3 weeks)

| Step | Work | Location |
|------|------|----------|
| 1.1 | `IScriptingEngine`, `Value`, `LuaScriptingEngine` (sol2) + unit tests | `pj_scripting/` |
| 1.2 | `transform_adapter` (Script SISO/MIMO wrapping ScriptHandle) | `pj_scripting/` |
| 1.3 | `SessionManager` (datastore + ObjectStore ownership, DataSource lifecycle) | `pj_app_core/` |
| 1.4 | `PlaybackEngine` (internal `QTimer`, tracker, follow-live state machine) | `pj_app_core/` |
| 1.5 | `CatalogModel` (Qt model over topics/fields, drag-mime encoding) | `pj_app_core/` |
| 1.6 | `WidgetRegistry`, `UserSettingsService`, `ExtensionCatalogService`, `NotificationCenter` | `pj_app_core/` |

**Verification**: headless unit tests using `QCoreApplication`; scripting round-trips; `SessionManager` loads a file and emits `dataChanged`; `PlaybackEngine` advances correctly.

### Phase 2 — Plot Widget Lift (~3 weeks, biggest chunk)

| Step | Work | Location |
|------|------|----------|
| 2.0 | Copy PJ3 plot widget files (PlotWidgetBase, PlotWidget, PlotDocker, TabbedPlotWidget, zoomers, axis-time, custom tracker, curvelist) | `pj_plot_widgets/src/` |
| 2.1 | Replace `PlotDataMapRef` reads with `DatastoreCurveAdapter` | `pj_plot_widgets/src/datastore_curve_adapter.cpp` |
| 2.2 | Min/max-per-pixel downsampler | `pj_plot_widgets/src/downsampler.cpp` |
| 2.3 | Implement `IDataWidget` on `PlotWidget`; register factory | `pj_plot_widgets/src/plot_widget.cpp` |
| 2.4 | Adapt curve-list drag-drop to consume `CatalogModel` mime | `pj_plot_widgets/src/curve_list_*.cpp` |
| 2.5 | Per-curve display transform UI kept intact; wire to plot-local state | `pj_plot_widgets/src/display_transform_dialog.cpp` |
| 2.6 | Stand up Qt ADS in `pj_app` MainWindow; add plot dock widgets via `WidgetRegistry` | `pj_app/src/main_window.cpp` |
| 2.7 | Linked X-axis zoom: `PlaybackEngine::TimeViewRange` + link-group membership | `pj_app_core/` + `pj_plot_widgets/` |

**Verification**: open file → drag curves into plot → multi-plot ads docking → linked X-zoom works; tracker crosshair on plots.

### Phase 3 — Media Widgets + Shell (~2 weeks)

| Step | Work | Location |
|------|------|----------|
| 3.1 | `Media2DDockWidget` wrapping `pj_media_qt::MediaViewerWidget`; `IDataWidget`; factory | `pj_media_widgets_qt/` |
| 3.2 | Wire `trackerChanged` → `MediaSource::setTimestamp` → repaint | `pj_media_widgets_qt/` |
| 3.3 | Curve list panel (QTreeView over `CatalogModel`, filter proxy, value-at-tracker column) | `pj_app/` |
| 3.4 | Time slider widget (tracker drag + play/pause/rate/loop/datetime) | `pj_app/` |
| 3.5 | MainWindow splitter layout; multi-tab support (Qt ADS manager per tab) | `pj_app/` |
| 3.6 | Drag-drop topic paths from curve list into any widget family | `pj_app/` + widget families |

**Verification**: media widget docked next to plot; tracker scrubs both; drag-drop video topic works.

### Phase 4 — Transforms & Derived Series (~2 weeks)

| Step | Work | Location |
|------|------|----------|
| 4.1 | Port 11 built-in SISO/MIMO transforms | `pj_app_core/src/transforms/*.cpp` |
| 4.2 | `TransformRegistry` (wraps `DerivedEngine`, cascade delete, script-backed transforms) | `pj_app_core/src/transform_registry.cpp` |
| 4.3 | Transform editor dialog | `pj_app/src/transform_editor_dialog.cpp` |
| 4.4 | Wire: attach/detach from plot context menu, replot on change | MainWindow signal wiring |
| 4.5 | Lua function editor (QCodeEditor/QScintilla, preview), bound to `pj_scripting` | `pj_app/src/function_editor_widget.cpp` |

**Verification**: all 11 transforms registered; derived-series editor creates persistent topics; Lua custom function creates a new topic visible in `CatalogModel`.

### Phase 5 — Persistence & Undo (~2 weeks)

| Step | Work | Location |
|------|------|----------|
| 5.1 | Define `WorkspaceDocument` schema (versioned, migration hooks) | `pj_app_core/include/pj_app_core/workspace_document.hpp` |
| 5.2 | `toJson()` / `fromJson()` | `pj_app_core/src/workspace_document.cpp` |
| 5.3 | Per-widget `saveState()` / `loadState()` via `IDataWidget` | all widget family modules |
| 5.4 | `WorkspaceManager` — gather/apply across widgets, dock-tree serialization via ads | `pj_app_core/` |
| 5.5 | Save/Load layout menu actions; recent layouts menu | `pj_app/` |
| 5.6 | `UndoManager` snapshot-based, cap 100 | `pj_app_core/` |
| 5.7 | Wire undo/redo (snapshot on undoable changes, Ctrl+Z/Y) | MainWindow |
| 5.8 | Auto-save last session on close, restore on launch | MainWindow |

**Verification**: round-trip (save → close → reopen → load → identical state); `UndoManager` unit test for sequence; missing-extension degraded restore scenario.

### Phase 6 — Plugins, Streaming, Reactive Scripts (~2 weeks)

| Step | Work | Location |
|------|------|----------|
| 6.1 | `ToolboxManager` — plugin scanning, activation UI, dialog engine binding, `notifyDataChanged` | `pj_app_core/` + `pj_app/` |
| 6.2 | Verify reactive Lua scripts toolbox end-to-end (uses `pj_scripting` + `onTimeChanged`) | End-to-end scenario |
| 6.3 | Streaming verification with a ported streaming DataSource (follow-live + auto-pause-on-scrub) | End-to-end scenario |
| 6.4 | Multi-file loading (merge/replace dialog, per-file prefix) | `SessionManager` |
| 6.5 | File reload from history, recent data files menu, drag-drop file loading | `pj_app/` |
| 6.6 | Plugins → Marketplace menu entry opens `pj_marketplace_ui` window | MainWindow |

### Phase 7 — Parity-plus Polish (~2 weeks)

| Step | Work | Effort |
|------|------|--------|
| 7.1 | XY plot mode in `PlotWidget` (timestamp-join of two series) | Medium |
| 7.2 | Theme support (light/dark QSS, icon sets) | Small |
| 7.3 | Preferences dialog (plugin dirs, theme, defaults, time display) | Small |
| 7.4 | Statistics dialog (per-curve min/max/mean/stddev) | Small |
| 7.5 | Toast notifications (lift from PJ3) | Small |
| 7.6 | Colormap editor, plot save to file + clipboard, streaming notifications panel | Small each |
| 7.7 | Keyboard shortcuts, cheatsheet/help dialog, fullscreen (F10) | Small each |
| 7.8 | PJ3 `.xml` layout import adapter (optional separate tool) | Medium, not blocking |

Total estimate: ~16 weeks (4 months) single-developer, assuming plugins and marketplace are truly done.

## Critical Files Reference

### In this repository — existing module APIs consumed by the new modules

| Purpose | Path |
|---|---|
| Datastore query API | `pj_datastore/include/pj_datastore/query.hpp` |
| Derived engine (transform interfaces) | `pj_datastore/include/pj_datastore/derived_engine.hpp` |
| Data engine (storage owner) | `pj_datastore/include/pj_datastore/engine.hpp` |
| ObjectStore (media blobs) | `pj_datastore/include/pj_datastore/object_store.hpp` |
| Plugin host bridges | `pj_datastore/include/pj_datastore/plugin_data_host.hpp` |
| Plugin loaders (DataSource, Parser, Toolbox) | `pj_plugins/include/pj_plugins/host/*.hpp` |
| Dialog engine (Qt) | `pj_plugins/dialog_protocol/include/pj_plugins/host_qt/dialog_engine.hpp` |
| Media `MediaSource` abstraction | `pj_media/pj_media_core/include/pj_media/media_source.hpp` |
| Media Qt widget | `pj_media/pj_media_qt/include/pj_media/media_viewer_widget.hpp` |
| Marketplace UI | `pj_marketplace/include/pj_marketplace/marketplace_window.hpp` |
| Design guidelines | `docs/cpp_design_recommendations.md` |
| Strategic plan | `PJ4_PLAN.md` |

### In PlotJuggler 3.x — lift source + port reference

The PJ3 repo lives at `~/ws_plotjuggler/src/PlotJuggler`.

**Lift wholesale into `pj_plot_widgets/`**:

| Purpose | Path (relative to PJ3 root) |
|---|---|
| Plot widget base | `plotjuggler_app/plotwidget_base.{h,cpp}` |
| Concrete plot widget | `plotjuggler_app/plotwidget.{h,cpp}` |
| Dock container (ads) | `plotjuggler_app/plotdocker.{h,cpp}` |
| Tabbed plot widget | `plotjuggler_app/tabbedplotwidget.{h,cpp}` |
| Custom tracker | `plotjuggler_app/customtracker.{h,cpp}` |
| Zoomers, axis-time, legend | `plotjuggler_app/plot_zoomer_base.{h,cpp}`, `axistimeoffset.{h,cpp}`, etc. |
| Curve list panel + filter | `plotjuggler_app/curvelist_*.{h,cpp}` |

**Port (algorithms) into `pj_app_core/transforms/`**:

| Purpose | Path |
|---|---|
| 11 built-in transforms | `plotjuggler_app/transforms/*.cpp` |

**Reference only (reimplement with `pj_scripting`)**:

| Purpose | Path |
|---|---|
| PJ3 Lua custom function | `plotjuggler_app/transforms/lua_custom_function.cpp` |
| PJ3 Reactive Lua function | `plotjuggler_base/include/PlotJuggler/reactive_function.h` |

**Reference only (do not lift the pattern, re-do with new architecture)**:

| Purpose | Path |
|---|---|
| Main orchestration (3900 LOC monolith) | `plotjuggler_app/mainwindow.cpp` |
| Toast notifications (small self-contained lift candidate) | `plotjuggler_app/toast_manager.{h,cpp}` |

## Verification

**Automated (each phase)**:

- `./build.sh --debug && ./test.sh && ./run_clang_tidy.sh` — must pass green
- Unit tests in each new module (Qt-free for scripting + datastore; `QCoreApplication` for app_core; Qt Test for widgets)
- Headless integration: load file → ingest → query tracker → assert data shape

**Per-phase key verification**:

| Phase | Key verification |
|---|---|
| 0 | All modules build; `pj_app` launches empty main window; existing tests green |
| 1 | `pj_scripting` Lua unit tests pass; `SessionManager` loads a file and emits `dataChanged`; `PlaybackEngine` tracker advances correctly in headless test |
| 2 | Open file → drag curves into plot → multi-plot ads docking → linked X-zoom works; tracker crosshair on plots |
| 3 | Media widget docked next to plot; tracker scrubs both; drag-drop video topic works |
| 4 | All 11 transforms registered; derived-series editor creates persistent topics; Lua custom function creates a new topic visible in catalog |
| 5 | Save layout → close app → reopen → load layout → identical state including media widgets, transforms, tracker time. Undo/redo cycle preserved |
| 6 | Toolbox plugin loads and writes new topics; reactive Lua toolbox re-evaluates on tracker move; streaming source follows live and auto-pauses on user scrub; marketplace window installs a plugin |
| 7 | XY mode, dark theme, statistics dialog, toasts, keyboard shortcuts all functional |

**End-to-end scenarios (Phase 6+)**:

1. Open CSV, drag 3 curves onto a plot, scrub timeline → curves + tracker line update
2. Open MCAP with video + IMU, tile plot and media widgets → both track global timeline
3. Define a Lua custom function over two curves → derived curve appears in catalog
4. Save layout → close app → reopen → layout + data + transforms restored
5. Install a plugin via marketplace → it appears in File → Open menu → load a file with it
6. Start a streaming source → tracker follows live; scrub backward → follow-live pauses; scrub to now → follow-live resumes
7. Undo a curve deletion → curve restored; redo → curve removed
8. Open PJ3 `.xml` layout: **intentionally unsupported in v1** — user exports from PJ3 via a migration tool (out of scope v1) or rebuilds layout

**Manual acceptance checklist**: one pass through each menu entry, each toolbox, each built-in transform, each file loader plugin, on a reference dataset.

## Deviations from Prior Planning Documents

- `PJ4_PLAN.md`: strict zero-Qt inside `pj_app_core` is **relaxed** to "Qt allowed, no widgets". Pragmatic trade: cheaper timers/settings/signals for the same testability boundary.
- Earlier version of this document: repo layout kept **monorepo for now**; module boundaries remain clean so the assumed-long-term separate `plotjuggler_app` repo split is a mechanical move later.
- Earlier version of this document: Qt Charts rejected as a plot backend alternative — Qwt wholesale lift from PJ3 is the chosen path, no `IPlotBackend` abstraction needed.
- Earlier version of this document: a single `pj_app_widgets` module is replaced by three sibling widget families (`pj_plot_widgets`, `pj_media_widgets_qt`, `pj_3d_widgets`); shared app-global widgets (curve list, time slider, dialogs) live in `pj_app`.
- Earlier version of this document: scripting runtime previously lived inside `pj_app_core`; now extracted to its own `pj_scripting` module so Lua can be swapped for Python without touching the GUI or the services layer.

## Out of Scope (v1)

- 3D widgets implementation (architecture locked in Section 2.4; scope is ~6-7 weeks on top of app v1, so deferred to post-v1 delivery)
- PJ3 XML layout import (manual rebuild required)
- Hot plugin reload
- Cross-process plugin sandbox
- Backpressure / overflow policy in streaming (defer to `pj_datastore` retention)
- Additional scripting languages beyond Lua (`pj_scripting` ready for them, but not shipped)
- StatePublisher plugin protocol (Toolbox covers the use case)
