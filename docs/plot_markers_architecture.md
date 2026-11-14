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

`PlotMarkers` is a **builtin object** containing a list of `PlotMarker`.
The host **ObjectStore** holds one set per `(dataset, marker-topic)`.
The **producer owns the set and republishes the whole blob** on each change
(last-writer-publish). The store never mutates individual markers, avoiding
per-id deletes, payload IDs and read-modify-write races.

```
 Producer (plugin / host data processor)
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

A marker is a **homogeneous, id-less record**. A topic holds a **list** of them,
distinguished by `kind`. `ImageAnnotations` instead groups heterogeneous
primitives in `points[]`, `circles[]` and `texts[]`.

Use the existing types in
[`pj_base/builtin/plot_markers.hpp`](../pj_base/include/pj_base/builtin/plot_markers.hpp)
and [their codec](plot_markers_format.md). Timestamp anchors are integer
nanoseconds; the header defines which fields each kind uses.

Design notes:
- **Flat anchor + `kind`** (not a `oneof`, not per-kind vectors). Simplest for the
  hand-written wire codec; the cost is that an invalid combination (a `Region` with
  `value_high` set) is *representable* — the codec/renderer just ignore irrelevant
  fields.
- **`has_value`** because a bare `double` cannot express "absent" for the optional
  `Event` value.
- **`ColorRGBA`** is already shared by including `image_annotations.hpp`; do not
  create another color type.
- `BuiltinObjectType::kPlotMarkers`, `PJ_builtin_object_type_t`'s matching tag
  and `plot_markers_codec` ship. New builtin types are append-only MINOR changes, and
  each new `PJ_builtin_object_type_t` enumerator must be classified in
  `pj_base/feature_floors.json` (a plugin emitting it needs that floor). Refresh
  `pj_base/abi/baseline.abi` only for an intentional MAJOR break; see
  [Versioning and releasing](RELEASING.md#abi-and-api-changes).

## 3. Why ObjectStore + republish (not a dedicated store, not DataEngine)

The model is **producer-owns-the-set + republish**, which removes the need for
store-side per-marker mutation — and that makes the existing **ObjectStore** the right
home (markers are just another builtin object).

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
  toolbox-host slot that re-resolves the topic on each republish.
- **Per-series read is direct** (one object topic per series). Cross-series aggregation
  ("all `warning+` markers in the dataset") enumerates the dataset's marker topics
  (a known limitation, acceptable at marker scale).

## 5. Producers

Every producer publishes the **same** `PlotMarkers` set; they differ in where the markers
come from:
- **Plugins (object-topic writes).** A toolbox or data source builds its set and republishes
  it via `registerObjectTopicOnDataset` / `registerObjectTopic` + `pushOwnedObject`, as in §4.
- **Derived markers (`pj.data_processors.v1`).** A plugin, script or agent submits a
  `kind="markers"` data processor (`DataProcessorsHostView::createMarkers` in
  `pj_base/sdk/plugin_data_api.hpp`): the host runs the script, owns the lifecycle and
  publishes the serialized set under `markerObjectTopicName(key)`. With
  `PJ_DATA_PROCESSOR_FLAG_EPHEMERAL` the node is a live preview, never persisted.

### Marker metadata and preview topics

A marker set is recognized and routed by its object-topic **metadata**, never by name
(`pj_base/sdk/object_topic_metadata.hpp`): `kDerivedMetadataKey` = `kDerivedMarkersValue`
marks a host-published set, `kMarkerScopeMetadataKey` (`kMarkerScopeSeries` /
`kMarkerScopeDataset` / `kMarkerScopeAll`) says which plots draw it,
`kMarkerTargetMetadataKey` names the series a series-scoped set annotates, and
`kMarkerOwnerMetadataKey` names the generator that publishes it.

`plot_markers.hpp` also defines the helpers producer and overlay share: `markerSeriesKey`
(per-series marker topic from a curve's topic and field names), `kPreviewMarkerTopic`
(marker-topic prefix of an ephemeral preview set) and `isPreviewMarkerTopic` (true for the
bare or object-topic form).

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
- This is the **`pj_plotting` Qwt path, not the scene2D image path** — markers
  annotate time-series plots, not image frames.

## 7. Limits

- **Cross-series aggregate query** enumerates topics (per-series read is direct).
- **Concurrent writers** to the same topic: the republish model assumes a single owner
  per marker topic; two live writers to one topic would clobber (last-writer wins).
- **Stable identity** (acks / correlating the same finding across producer re-runs) is
  not provided by anonymous republished sets — it would be layered on if needed.
