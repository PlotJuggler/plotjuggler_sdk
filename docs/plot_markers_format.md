# Plot Markers Format

Use the canonical `PJ.PlotMarkers` wire format to store, transport or replay
plot-marker findings as bytes. `PlotMarkers` holds the markers for one series
or dataset-global topic. The codec serializes this set to the schema's
protobuf-wire payload.

A marker describes a finding in plot time, analogous to
[`ImageAnnotations`](image_annotations_format.md) in image space.
Their structures differ. Markers are homogeneous records distinguished by
`kind`; `PlotMarkers` is a flat list of them.
See [builtin_type.md](builtin_type.md) for the broader catalog.

## Contract

The schema identifier for this format is:

```text
PJ.PlotMarkers
```

The public C++ helpers live in:

```cpp
#include <pj_base/builtin/plot_markers_codec.hpp>
```

`serializePlotMarkers()` writes this payload. `deserializePlotMarkers()` reads it
back into `PJ::sdk::PlotMarkers`.

The field-level contract is `pj_base/proto/pj/PlotMarkers.proto` and its imported
`pj_base/proto/pj/*.proto` files (`Color.proto`, `KeyValuePair.proto`). As with
the other builtins, the C++ codec uses PlotJuggler's private wire primitives
rather than generated Protobuf code; the `.proto` files are the source of truth
for field numbers and wire types.

## What the marker does NOT carry (by design)

- **No `id`.** A producer owns its set and republishes it wholesale (last-writer
  -publish), so no per-marker id is carried; identity (if ever needed for acks /
  cross-run correlation) is a host concern layered on top — not serialized into the
  value. This keeps `PlotMarker` consistent with every other builtin, none of which
  carry an id.
- **No `source`.** No builtin records its creator; provenance is the dataset/topic
  the marker lives under. Producer-specific extras go in `metadata`.
- **No `scope`.** A marker's reach is decided by *which topic* it is addressed to
  (a series topic vs. a dataset-global topic), not by a field.

## SDK Mapping

| Schema field (`PlotMarker`) | SDK behavior |
|-----------------------------|--------------|
| `kind` | `PlotMarker::kind`. Unknown values decode to `kRegion`. |
| `t_start` / `t_end` | `int64` ns; `PlotMarker::t_start` / `t_end`. |
| `value_low` / `value_high` | `double`; ValueBand bounds / optional Event point value. |
| `has_value` | `PlotMarker::has_value` (the Event point value is meaningful). |
| `status` / `severity` | enums; unknown values decode to `kNone` / `kInfo`. |
| `category` / `label` / `description` | strings. |
| `color` | `PJ.Color` message; alpha 0 means "derive from severity". |
| `metadata` | `repeated PJ.KeyValuePair` → `std::vector<MarkerProperty>`. |

## Codec Rules

Colors are stored as normalized `double` channels in `[0, 1]` (a `PJ.Color`
message); the SDK stores RGBA `uint8_t`. Decode clamps to `[0, 1]` and rounds to
the nearest byte, so a round trip may differ by one channel value due to
floating-point rounding.

Enum fields are written as their raw numeric value and always emitted. The reader
maps unknown `kind` to `kRegion`, unknown `status` to `kNone`, and unknown
`severity` to `kInfo`, so forward-compatible payloads still decode.

A `PlotMarkers` value with no markers serializes to an empty byte buffer. A
zero-size buffer decodes to an empty set, including when its pointer is null.
A null pointer with nonzero size is invalid.

The reader decodes the mapped fields and skips unknown fields (including unknown
nested fields), so compatible schema additions are tolerated. Malformed protobuf
data, invalid length-delimited fields, or truncated nested messages fail decoding.
