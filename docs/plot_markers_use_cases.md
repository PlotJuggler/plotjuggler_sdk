# Plot Markers — Use Cases & Examples

> The SDK ships `PJ::sdk::PlotMarkers`, its codec and the generic object-write/read
> services. See [the wire contract](plot_markers_format.md) and
> [architecture](plot_markers_architecture.md) before implementing a producer.
> Rendering, panels and report UI are host features; examples below describe
> their intended use, not additional SDK services.
>
> Plot Markers borrow the *concept* of [`ImageAnnotations`](image_annotations_format.md)
> — a canonical SDK builtin object with a wire codec — but **not its structure**.
> An image annotation overlays a video frame; a plot marker annotates a *time-series
> plot*, and its shape is its own (see the architecture doc).

## 1. Motivation

Plugins — and a future **AI agent** — need a way to put **graphical markers** on
plots (a shaded time region, a point at an event, a value band) and to **ask which
markers exist** on a given series. Use the existing `PlotMarkers` type and codec
through [ObjectStore services](../V4_STORE.md).

**A marker is a structured finding.** It carries a pass/fail status, severity
and category alongside its time anchor. Reports and consumer-side filtering
use this content even if the marker is never drawn.

A marker has three consumers of one data model:
- the **plot renderer** (a Qwt overlay on the time-series plot),
- a **markers panel** (list, filter, jump-to-time),
- a **JSON report** (pass/fail + anomalies + timestamps + severity).

Producers edit an in-memory set and republish the whole `PlotMarkers` value;
there is no per-marker add/delete API. Toolbox plugins and the host can read
sets through the generic object pipeline. Future agents and ingestion parsers
can use the same type.

## 2. Vocabulary

A **marker** is a record with a `kind`, an anchor, and shared semantic fields. There
are four kinds:

| Kind | Anchor | Example | Visual form |
|------|--------|---------|-------------|
| `Region` | time span `[t_start, t_end]` | "velocity exceeds 1 rad/s here" | translucent vertical band |
| `Event` | single time `t` (+ optional value) | "`OK → ERROR` transition" | tick / point at `(t, value)` |
| `ValueBand` | value span `[y_low, y_high]` | "valid operating range" | translucent horizontal band |
| `Label` | a time `t` + text | free annotation | text callout |

Every kind carries the same **semantic fields**:

| Field | Meaning |
|-------|---------|
| `kind` | One of the four above. |
| `status` | `none` \| `pass` \| `fail` — the finding verdict. |
| `severity` | `info` \| `warning` \| `error` \| `critical` — drives default color. |
| `category` | Free string for the anomaly / annotation type (e.g. `"overspeed"`). |
| `label` | Short human-readable title (tooltip, panel, the `Label` kind's text). |
| `description` | Optional longer text. |
| `color` | Optional RGBA override; default derives from `severity`. |
| `metadata` | Key/value bag for producer-specific extras (e.g. `peak=1.83`, or the threshold that produced the finding). |
| anchor | The timestamps and/or value range appropriate to `kind`. |

Markers omit these fields (see the architecture doc for the full reasoning):
- **no `id`** — the producer owns and republishes a whole set; the store does not
  return per-marker identities;
- **no `source`** — no builtin records its creator; provenance is the location, and
  optional provenance goes in `metadata`;
- **no `scope`** — *where* a marker is addressed says it (see §3).

> `ValueBand` is the one exception to §3: a y-range is in a specific series' units, so
> it is always **series-bound** and never global.

## 3. Addressing model

A marker lives under `(dataset, father-name)`, **like a timeseries**.
Its address determines where it belongs:

- **Series marker** → addressed to that series' topic (e.g. `cmd_vel/x`). It renders
  on every plot showing `cmd_vel/x`.
- **Global marker** → addressed to a dataset-level "global" topic. It renders on
  every plot of that dataset whose visible time window overlaps the marker.

The father-name determines whether the marker is global or scoped.
No payload field sets its scope.

## 4. Use cases

- **UC-1 — Region from a threshold.** *"Highlight where velocity exceeds 1 rad/s."*
  A toolbox plugin scans `joint_2/vel`, coalesces each above-threshold run into one
  `Region` (`severity=warning`, `category="overspeed"`, `metadata.peak=1.83`), and
  publishes the set under `markerObjectTopicName("joint_2/vel")`.
  The plot shows translucent shaded spans.

- **UC-2 — Event markers from state transitions.** *"Mark every `OK → ERROR`
  transition."* The plugin builds an `Event` at each transition time
  (`status=fail`, `severity=error`) and publishes the set for the `/status`
  series. The plot shows ticks.

- **UC-3 — ValueBand operating range.** A plugin includes a `ValueBand` for the
  nominal range of `motor/temp` in its published set (series-bound); samples
  leaving the band become obvious.

- **UC-4 — Agent republishes a marker set.** An agent (or script) builds the set for a
  topic — e.g. a `Region{1.0s..2.0s, severity=error, label="discontinuity"}` on
  `cmd_vel/x` of the Waymo dataset — and publishes the whole `PlotMarkers` set to that
  topic. To add, change, or remove a marker it edits its in-memory set and republishes
  the whole set (last-writer-publish) — there is no per-marker store mutation or
  in-place "modify."

- **UC-5 — Read by series.** A report tool asks *"give me the markers on `cmd_vel/x` in
  the Waymo dataset"* → it reads that series' marker object topic (`ToolboxObjectReadHostView::readLatestAt`) and
  deserializes the `PlotMarkers` set directly (no scanning). Filtering by time range /
  `severity ≥ warning` is done on the deserialized set.

- **UC-6 — JSON report.** A run exports a report — `overall_status`, plus every
  finding with its timestamps and severity. The data model supports this with no GUI;
  a true headless CLI entry point is a separate, deferred effort (PJ4 is GUI-only
  today), so v1 exposes export as a host action.

- **UC-7 — GUI inspection.** A user sees markers colored by severity, hovers for a
  tooltip with the semantic fields, and uses a **markers panel** to filter (by
  severity / kind / series), jump to a marker's time, and toggle visibility.

## 5. Illustrative JSON (a query result / report)

The wire form is the codec-serialized marker list. This JSON is a *report view*
of the same data. It has no `source`, `scope` or per-marker `id`.
The producer owns and republishes the whole set. Identity for acknowledgments
or cross-run correlation, if needed, belongs in the host rather than the marker.

```json
{
  "report": { "overall_status": "fail", "dataset": "Waymo" },
  "markers": [
    {
      "kind": "region",
      "series": "cmd_vel/x",
      "t_start": 1.00,
      "t_end": 2.00,
      "status": "fail",
      "severity": "error",
      "category": "discontinuity",
      "label": "cmd_vel/x discontinuity",
      "metadata": { "jump": 3.4 }
    },
    {
      "kind": "event",
      "series": "/status",
      "t": 19.05,
      "status": "fail",
      "severity": "error",
      "category": "state_transition",
      "label": "OK -> ERROR",
      "metadata": { "from": "OK", "to": "ERROR" }
    }
  ]
}
```

`series` here reflects the *topic the marker was addressed to*;
`kGlobalMarkerTopic` (`"__global__"`) marks a dataset-global marker. The report
uses seconds for readability; SDK marker timestamps are integer nanoseconds.
