# Plot Markers — Architecture

> **Status: design draft.** The *what/why* and examples live in
> [plot_markers_use_cases.md](plot_markers_use_cases.md). This document is the
> *how*: the type, the store, the API surface, and how PJ4 renders markers. Names
> and signatures are illustrative until frozen.
>
> Plot Markers reuse the **concept** of [`ImageAnnotations`](image_annotations_format.md)
> (a canonical SDK builtin object with a wire codec) but not its structure.

## 1. Layering & data flow

Three layers, with a clean boundary at the SDK C-ABI:

```
 Producer (toolbox plugin / future agent / ingestion parser)
        │  add(dataset, father, marker) → id
        │  delete(dataset, father, id)
        │  query(dataset, [series], [filters]) → markers
        ▼   ── SDK C-ABI (marker host-view service) ──
 Host marker store   ← owns identity (the marker id) and the mutable set
        ▼
 PJ4 consumers:  pj_plotting Qwt overlay · markers panel · JSON export
```

- **SDK (`plotjuggler_sdk`)** owns the **type + codec** and the **marker host-view
  service contract** (the C-ABI a producer speaks). This is the only code a plugin or
  agent compiles against — which is exactly why the typed contract must live here.
- **Host (`PJ4`)** owns the **marker store** (the identity owner) behind that
  contract, plus rendering, the panel, and export.
- Producers and the agent **never** see the store — only the API. The store is an
  implementation detail and can change without touching a single producer.

## 2. The `PlotMarker` / `PlotMarkers` type

A marker is a **homogeneous, id-less record**; a topic holds a **list** of them. This
is the structural departure from `ImageAnnotations` (which groups heterogeneous
primitives `points[]`/`circles[]`/`texts[]`): markers are one uniform record type
distinguished by a `kind`.

```cpp
namespace PJ {
namespace sdk {

enum class MarkerKind : uint8_t {
  kRegion,     ///< time span [t_start, t_end] — shaded vertical band
  kEvent,      ///< single time t_start (+ optional value) — tick / point
  kValueBand,  ///< value span [value_low, value_high] — horizontal band (series-only)
  kLabel,      ///< text callout anchored at t_start
};

enum class MarkerStatus   : uint8_t { kNone, kPass, kFail };
enum class MarkerSeverity : uint8_t { kInfo, kWarning, kError, kCritical };

/// Producer-specific key/value extension hatch — keeps the schema stable as
/// producers attach extra fields (threshold, peak, from/to, …) without a schema bump.
struct MarkerProperty {
  std::string key;
  std::string value;
  bool operator==(const MarkerProperty&) const = default;
};

/// One marker. Carries NO id (the store owns identity), NO source (no builtin
/// records its creator), NO scope (the topic it lives under says that).
struct PlotMarker {
  MarkerKind kind = MarkerKind::kRegion;

  // --- anchor (interpret by kind; irrelevant fields ignored) ---
  Timestamp t_start    = 0;     ///< Region start · Event/Label time · (ValueBand: ignored)
  Timestamp t_end      = 0;     ///< Region end · (others: ignored)
  double    value_low  = 0.0;   ///< ValueBand low · Event point value · (others: ignored)
  double    value_high = 0.0;   ///< ValueBand high · (others: ignored)
  bool      has_value  = false; ///< Event: value_low is a meaningful point value.

  // --- semantics / presentation (shared by every kind) ---
  MarkerStatus   status   = MarkerStatus::kNone;
  MarkerSeverity severity = MarkerSeverity::kInfo;
  std::string    category;
  std::string    label;
  std::string    description;
  ColorRGBA      color = {0, 0, 0, 0};   ///< a=0 → derive from severity.
  std::vector<MarkerProperty> metadata;

  bool operator==(const PlotMarker&) const = default;
};

/// The canonical object a marker query/render reads: the set of markers for one
/// topic (one series, or the dataset-global topic).
struct PlotMarkers {
  std::vector<PlotMarker> markers;
  bool operator==(const PlotMarkers&) const = default;
  [[nodiscard]] bool empty() const noexcept { return markers.empty(); }
};

}  // namespace sdk
}  // namespace PJ
```

Design notes:
- **Flat anchor + `kind`** (not a `oneof`, not per-kind vectors). Simplest for the
  hand-written wire codec; the cost is that an invalid combination (a `Region` with
  `value_high` set) is *representable* — the codec/renderer just ignore irrelevant
  fields.
- **`has_value`** because a bare `double` cannot express "absent" for the optional
  `Event` value.
- **`ColorRGBA`** currently lives in `image_annotations.hpp`; reuse here requires
  promoting it to a shared vocabulary header so `PlotMarkers` does not include
  image-annotation code.
- New canonical type → append `kPlotMarkers` to `BuiltinObjectType` /
  `PJ_builtin_object_type_t` (append-only; **MINOR** SDK bump; refresh
  `abi/baseline.abi`) + a `plot_markers_codec` mirroring the `image_annotations_codec`
  *pattern*.

## 3. Why a dedicated marker store (not ObjectStore, not DataEngine)

The feature's core operation is **delete one specific marker**. That single
requirement decides the storage:

- **`DataEngine` (columnar) — no.** It stores scalar samples as numeric columns. A
  marker is a heterogeneous record with a *span* `[t1,t2]`, an enum severity, strings.
  Forcing it into columns and inventing a "sample timestamp" for a span is a deep
  impedance mismatch.
- **`ObjectStore` — no (for the real design).** It is a timestamped blob log whose
  only removal verbs are **front-eviction** (`evictBefore`, `removeTopic`, `clear`):
  it *structurally cannot delete entry #42*. To get per-marker delete on top of it you
  must store the whole set as **one snapshot blob** and rewrite it on every edit — and
  smuggle an `id` *into* the canonical type that no other builtin has.
- **Dedicated marker store — yes.** One marker per entry, real per-id add/delete, and
  identity owned by the store (a handle like `ObjectStore`'s native `SequentialUID`).
  This keeps `PlotMarker` clean and consistent with all 16 other builtins, and the
  `id` lives *outside* the payload, surfaced by the API.

The `id` you asked about earlier is therefore a **store handle**, not a type field.

> **Fallback.** If shipping speed ever outweighs cleanliness, back the same API with
> an `ObjectStore` snapshot (one blob per topic, `id` carried inside the blob,
> rewrite-on-edit). The producer-facing API is identical either way — this is purely
> a host-internal choice.

## 4. The marker host-view service (SDK C-ABI)

Store-agnostic, symmetric (create + query), addressed by data identity:

```text
add   (dataset, father_name, PlotMarker)                       -> MarkerId
delete(dataset, father_name, MarkerId)                         -> Status
query (dataset, father_name, [time_range], [min_severity], …)  -> {MarkerId, PlotMarker}[]
```

- `father_name` is the series topic (e.g. `cmd_vel/x`) or the dataset-level global
  topic. There is no panel/GUI addressing.
- **Per-series query is direct** — read the markers under that topic; no scan.
- Cross-series aggregation ("all `warning+` markers in the dataset") enumerates the
  dataset's marker topics (a known limitation, acceptable at marker scale).
- This is the contract the **AI agent** drives and that **plugins** call; both are
  data-domain operations, which is why they belong on the SDK ABI (and "create a
  plot / edit layout" deliberately does **not** — that is app-control, not data).

## 5. Producers

All produce the **same** `PlotMarker` type; they differ only in where the markers
come from:
- **Toolbox plugin (today).** Analysis over already-loaded series → `add`. The first
  production user of the marker write path (analogous to the existing object-write
  path that no toolbox uses yet).
- **AI agent (future).** Drives the same `add`/`delete`/`query` API to manage markers
  programmatically.
- **Ingestion parser (future).** Markers that already exist in a recording (an event
  channel, annotation messages) decode into `PlotMarker`s — the same role an image
  parser plays for `ImageAnnotations`.

## 6. Rendering (PJ4 host)

- A **`PlotMarkersItem : QwtPlotItem`** overlay in `pj_plotting/widget/`, modeled on
  `CurveTracker` (the existing custom Qwt item). Its `draw(painter, xMap, yMap, rect)`
  maps marker times→pixels via `canvasMap()` and paints: `Region`/`ValueBand` as
  translucent fills, `Event` as ticks/points, `Label` as text.
- **Render rule:** a series marker draws on every plot showing that series; a global
  marker draws on every plot of its dataset whose visible X-window overlaps it.
- **Repaint** on data-change (the marker store notifying the host) and on time/zoom
  changes, exactly as `CurveTracker` repaints on `PlaybackEngine::currentTimeChanged`.
- **Markers panel** in `pj_app` (modeled on `CurveListPanel`): filterable list,
  click-row → `PlaybackEngine::setCurrentTime(marker_time)`.
- This is the **`pj_plotting` Qwt path, not the scene2D image path** — markers
  annotate time-series plots, not image frames.

## 7. Open / deferred

- **Headless CLI.** PJ4 is GUI-only today (`pj_app/src/main.cpp` always opens a
  window). The data model is headless-ready (Qt-free `pj_runtime`/store), but a true
  no-GUI runner is a separate effort; v1 exposes JSON export as a host action.
- **Cross-series aggregate query** enumerates topics (per-series query is direct).
- **Concurrent writers** to the same topic (agent + plugin) need host-side locking on
  that topic.
- **`ObjectStore`-snapshot fallback** (§3) remains available behind the same API.
