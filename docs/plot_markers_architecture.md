# Plot Markers — Architecture

> Markers are a **builtin object** stored in the **ObjectStore** — there is no
> dedicated marker store and no marker-store service; producers republish a whole
> `PlotMarkers` set under an object topic (last-writer-wins). The *what/why* and
> examples live in [plot_markers_use_cases.md](plot_markers_use_cases.md).
>
> Plot Markers reuse the **concept** of [`ImageAnnotations`](image_annotations_format.md)
> (a canonical SDK builtin object with a wire codec) but not its structure — and, like
> every builtin object, they ride the generic ObjectStore pipeline.

## 1. Layering & data flow

A marker set is a **builtin object** (`PlotMarkers` = a list of `PlotMarker`), one per
`(dataset, marker-topic)`, stored in the host **ObjectStore**. The **producer owns its
set and republishes the whole blob** on any change (last-writer-publish); the store is
never mutated marker-by-marker, so no per-id delete / id-in-payload / RMW race is needed.

```
 Producer (toolbox plugin / future Lua / in-process host)
        │  build PlotMarkers set  →  serialize (PlotMarkers codec)
        │  register object topic  "__markers__/<topic>"  on the dataset
        │  push the whole serialized set  (republish)
        ▼   ── generic object-write surface (no marker-specific service) ──
 Host ObjectStore   ← holds the latest serialized PlotMarkers blob per topic
        ▼
 PJ4 consumers:  pj_plotting Qwt overlay (latestAt + deserialize) · JSON export
```

- **SDK (`plotjuggler_sdk`)** owns only the **type + codec** (`PlotMarkers`) plus the
  marker object-topic naming convention (`markerObjectTopicName`, `kGlobalMarkerTopic`
  in `pj_base/builtin/plot_markers.hpp`). Producers write via the **generic
  object-write surface** (`registerObjectTopic*` + `pushOwnedObject`) — the same one
  images/point clouds use. Annotating an *existing* dataset uses
  `registerObjectTopicOnDataset(DatasetId, …)` (idempotent: re-resolves the topic).
- **Host (`PJ4`)** owns the **ObjectStore** (which already holds all object media),
  rendering, and export. No marker-specific store or service.
- Producers never address individual markers — they publish a set. Identity, if ever
  needed (acks / cross-run correlation), would be layered on top without changing the
  SDK type or the object pipeline.

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

## 3. Why ObjectStore + republish (not a dedicated store, not DataEngine)

Markers were originally planned in a dedicated mutable store keyed on "delete one
specific marker". With the in-process `pj_scripting` Lua engine becoming the primary
producer, the model flipped to **producer-owns-the-set + republish**, which removes the
need for store-side per-marker mutation — and that makes the existing **ObjectStore**
the right home (markers become just another builtin object).

- **`DataEngine` (columnar) — no.** Strictly numeric columns (`PrimitiveType`),
  append-only immutable chunks. A marker is a structured record over a span; it fits
  neither the type nor the mutability model.
- **`ObjectStore` — yes.** A marker *set* for a topic is one serialized `PlotMarkers`
  blob = one object entry. The producer republishes the whole blob on every change, so
  ObjectStore's append + latest-at semantics suffice — no per-entry delete needed.
  Editing one marker = the producer mutates its in-memory set and re-pushes; there is
  no read-modify-write against the store and no concurrency race for a single owner.
  Set a keep-latest retention budget so superseded snapshots don't accumulate; the
  overlay reads `latestAt(MAX)` so markers show regardless of the playback cursor.
- **No dedicated store, no id in the payload.** `PlotMarker` stays id-less like every
  other builtin; identity (if ever needed for acks / cross-run correlation) is layered
  on top, not baked into the type.

## 4. The object-write surface (SDK C-ABI)

Markers reuse the **generic object pipeline** — there is no marker-specific service.
A producer builds its set, serializes it, registers the object topic, and pushes:

```text
register object topic  markerObjectTopicName(topic)  on the dataset
push the whole serialized PlotMarkers set            (republish, last-writer wins)
```

- The marker topic is a series field path (e.g. `cmd_vel/x`) or `kGlobalMarkerTopic`;
  the object topic name is `markerObjectTopicName(topic)` = `"__markers__/" + topic`
  (a reserved namespace so it never collides with media object topics).
- **Producers creating their own dataset** use `registerObjectTopic(source, …)`.
  **Producers annotating an existing dataset** (a compiled toolbox over loaded data)
  use `registerObjectTopicOnDataset(DatasetId, …)` — a tail-appended, **idempotent**
  toolbox-host slot that re-resolves the topic on each republish. In-process producers
  (host / future Lua) can equally call `ObjectStore` directly.
- **Per-series read is direct** (one object topic per series). Cross-series aggregation
  ("all `warning+` markers in the dataset") enumerates the dataset's marker topics
  (a known limitation, acceptable at marker scale).

## 5. Producers

All produce the **same** `PlotMarkers` set and publish it the same way; they differ
only in where the markers come from:
- **In-process host (today).** The Help → Add Demo Markers action builds a set and
  publishes it via `ObjectStore` directly — the simplest exercise of the path.
- **Toolbox plugin (today).** Analysis over already-loaded series → republish its set
  via `registerObjectTopicOnDataset` + `pushOwnedObject`.
- **Lua scripting / AI agent (future, `pj_scripting`).** The primary producer: an
  in-process script regenerates and republishes its set; the agent most naturally emits
  Lua. No new ABI — it rides the same path.
- **Ingestion parser (future).** Markers already present in a recording decode into
  `PlotMarkers` — the same role an image parser plays for `ImageAnnotations`.

## 6. Rendering (PJ4 host)

- A **`PlotMarkersItem : QwtPlotItem`** overlay in `pj_plotting/widget/`, modeled on
  `CurveTracker`. Its `draw(painter, xMap, yMap, rect)` reads the marker set for each
  target `(dataset, topic)` from `ObjectStore::latestAt` + `deserializePlotMarkers`,
  maps marker times→pixels, and paints: `Region`/`ValueBand` as translucent fills,
  `Event` as ticks/points, `Label` as text.
- **Render rule:** a series marker draws on every plot showing that series; a global
  marker draws on every plot of its dataset.
- **Repaint** on `SessionManager::markersChanged` (a store-agnostic signal a producer
  fires after republishing) and on time/zoom changes.
- A **markers panel** in `pj_app` (filterable list, click-row → seek) is optional/future.
- This is the **`pj_plotting` Qwt path, not the scene2D image path** — markers
  annotate time-series plots, not image frames.

## 7. Open / deferred

- **Headless CLI.** PJ4 is GUI-only today (`pj_app/src/main.cpp` always opens a
  window). The data model is headless-ready (Qt-free `pj_runtime`/store), but a true
  no-GUI runner is a separate effort; v1 exposes JSON export as a host action.
- **Cross-series aggregate query** enumerates topics (per-series read is direct).
- **Concurrent writers** to the same topic: the republish model assumes a single owner
  per marker topic; two live writers to one topic would clobber (last-writer wins).
- **Stable identity** (acks / correlating the same finding across producer re-runs) is
  not provided by anonymous republished sets — it would be layered on if needed.
