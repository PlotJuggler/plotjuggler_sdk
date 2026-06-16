# Plot Markers — Use Cases & Examples

> **Status: design draft.** This document defines *what* the Plot Markers feature is
> for and *how it is used*, through concrete examples. The architecture (types,
> store, API surface, rendering) lives in
> [plot_markers_architecture.md](plot_markers_architecture.md). Field and API names
> here are illustrative until the type is frozen.
>
> Plot Markers borrow the *concept* of [`ImageAnnotations`](image_annotations_format.md)
> — a canonical SDK builtin object with a wire codec — but **not its structure**.
> An image annotation overlays a video frame; a plot marker annotates a *time-series
> plot*, and its shape is its own (see the architecture doc).

## 1. Motivation

Plugins — and a future **AI agent** — need a way to put **graphical markers** on
plots (a shaded time region, a point at an event, a value band) and to **ask which
markers exist** on a given series. Today there is no such API: annotations exist
only for *images*, tied to an image frame, not to plot time.

The decisive framing: **a marker is a structured *finding*, not just a drawing.** It
carries semantic content — a pass/fail status, a severity, a category — alongside its
anchor in time. That content is what the JSON report and the query API operate on,
whether or not the marker is ever drawn.

A marker has three consumers of one data model:
- the **plot renderer** (a Qwt overlay on the time-series plot),
- a **markers panel** (list, filter, jump-to-time),
- a **JSON report** (pass/fail + anomalies + timestamps + severity).

Producers create *and delete* markers, and any producer (or the host) can *query*
them — the API is **symmetric and bidirectional**. Today the producer is an analysis
**toolbox** plugin; a future **AI agent** and an **ingestion parser** (markers that
already exist in a recording) are natural additional producers sharing the exact same
type.

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

Three things a marker **does not** carry, and why (see the architecture doc for the
full reasoning):
- **no `id`** in the authored marker — identity is owned by the *store* and handed
  back on `add` (consistent with every other builtin, none of which carry an id);
- **no `source`** — no builtin records its creator; provenance is the location, and
  optional provenance goes in `metadata`;
- **no `scope`** — *where* a marker is addressed says it (see §3).

> `ValueBand` is the one exception to §3: a y-range is in a specific series' units, so
> it is always **series-bound** and never global.

## 3. Addressing model

A marker lives under `(dataset, father-name)` — **exactly like a timeseries**. You
don't tag a marker with "where it belongs"; you *put it* where it belongs:

- **Series marker** → addressed to that series' topic (e.g. `cmd_vel/x`). It renders
  on every plot showing `cmd_vel/x`.
- **Global marker** → addressed to a dataset-level "global" topic. It renders on
  every plot of that dataset whose visible time window overlaps the marker.

So "is this marker global or scoped?" is answered by *which father-name you addressed
it to*, not by a field in the payload.

## 4. Use cases

- **UC-1 — Region from a threshold.** *"Highlight where velocity exceeds 1 rad/s."*
  A toolbox plugin scans `joint_2/vel`, coalesces each above-threshold run into one
  `Region` (`severity=warning`, `category="overspeed"`, `metadata.peak=1.83`), and
  `add`s it to the `joint_2/vel` topic. The plot shows translucent shaded spans.

- **UC-2 — Event markers from state transitions.** *"Mark every `OK → ERROR`
  transition."* The plugin `add`s an `Event` at each transition time
  (`status=fail`, `severity=error`) to the `/status` series. The plot shows ticks.

- **UC-3 — ValueBand operating range.** A plugin `add`s a `ValueBand` for the nominal
  range of `motor/temp` (series-bound); samples leaving the band become obvious.

- **UC-4 — Agent creates and deletes markers.** An agent, driving the SDK API, calls
  `add(dataset="Waymo", father="cmd_vel/x", marker=Region{1.0s..2.0s, severity=error,
  label="discontinuity"})` and gets back a store-assigned id. Later it removes that
  one marker with `delete(dataset="Waymo", father="cmd_vel/x", id)`. Adding more
  markers to the same topic and deleting specific ones covers all editing needs — no
  in-place "modify."

- **UC-5 — Query by series (bidirectional read).** The agent (or a report tool) asks
  *"give me the markers on `cmd_vel/x` in the Waymo dataset"* →
  `query(dataset="Waymo", series="cmd_vel/x")` returns that topic's markers directly
  (no scanning). Optional filters narrow by time range and/or `severity ≥ warning`.

- **UC-6 — JSON report.** A run exports a report — `overall_status`, plus every
  finding with its timestamps and severity. The data model supports this with no GUI;
  a true headless CLI entry point is a separate, deferred effort (PJ4 is GUI-only
  today), so v1 exposes export as a host action.

- **UC-7 — GUI inspection.** A user sees markers colored by severity, hovers for a
  tooltip with the semantic fields, and uses a **markers panel** to filter (by
  severity / kind / series), jump to a marker's time, and toggle visibility.

## 5. Illustrative JSON (a query result / report)

The wire form is the codec-serialized marker list; this JSON is the *report view* of
the same data. Note there is no `source`/`scope` field, and `id` is the store handle
returned alongside each marker, not part of the authored marker.

```json
{
  "report": { "overall_status": "fail", "dataset": "Waymo" },
  "markers": [
    {
      "id": 1,
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
      "id": 2,
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

`series` here reflects the *topic the marker was addressed to*; `"__global__"` (or
similar) marks a dataset-global marker.
