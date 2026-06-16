# Plot Markers Format

PlotJuggler uses a canonical `PJ.PlotMarkers` wire format when plot-marker
findings need to be stored, transported, or replayed as bytes. A `PlotMarkers`
value is the set of markers for one topic (one series, or a dataset-global
topic); the codec serializes it to the protobuf-wire payload described by the
schema.

A marker is a *time-centric* finding on a plot, the analog of the *image-centric*
[`ImageAnnotations`](image_annotations_format.md). It is **not** structured like
`ImageAnnotations`: a marker is a homogeneous record distinguished by `kind`, and
`PlotMarkers` is a flat list of them. For the broader builtin type catalog, see
[builtin_type.md](builtin_type.md).

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

- **No `id`.** Identity (the delete handle) is owned by the host marker store and
  surfaced by the marker API — not serialized into the value. This keeps
  `PlotMarker` consistent with every other builtin, none of which carry an id.
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

A `PlotMarkers` value with no markers serializes to an empty byte buffer. Decoding
a null or empty buffer is treated as invalid input by the current reader.

The reader decodes the mapped fields and skips unknown fields (including unknown
nested fields), so compatible schema additions are tolerated. Malformed protobuf
data, invalid length-delimited fields, or truncated nested messages fail decoding.
