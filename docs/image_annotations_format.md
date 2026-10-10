# Image Annotations Format

PlotJuggler uses a canonical `PJ.ImageAnnotations` wire format when 2D
annotation overlays need to be stored, transported, or replayed as bytes.
Source-specific adapters convert third-party messages, such as ROS messages,
into the owned `PJ::sdk::ImageAnnotations` value; the codec then serializes
that value to the protobuf-wire payload described by the schema.

This keeps PlotJuggler internals and renderers independent from the original
message schema. Consumers should decode the bytes into
`PJ::sdk::ImageAnnotations` and operate on the canonical values, not on the
source message type that produced them.

For the broader builtin type catalog, see [builtin_type.md](builtin_type.md).

## Contract

The schema identifier for this format is:

```text
PJ.ImageAnnotations
```

The public C++ helpers live in:

```cpp
#include <pj_base/builtin/image_annotations_codec.hpp>
```

`serializeImageAnnotations()` writes this payload. `deserializeImageAnnotations()`
reads it back into `PJ::sdk::ImageAnnotations`.

The field-level contract is `pj_base/proto/pj/ImageAnnotations.proto` and its
imported `pj_base/proto/pj/*.proto` files. The markdown here intentionally does
not duplicate those field tables; the `.proto` files are the source of truth for
field numbers, field names, and protobuf wire types.

The current C++ implementation uses PlotJuggler's private wire primitives
rather than generated Protobuf code. Public SDK headers expose
`PJ::sdk::ImageAnnotations`, not generated Protobuf classes, and `pj_base` does
not require a Protobuf runtime dependency.

## Attribution

The initial schema layout is adapted from the Foxglove SDK schema catalog,
licensed under MIT by Foxglove Technologies Inc. PlotJuggler keeps the adopted
field numbers and protobuf scalar/message shapes where they are useful, but the
schemas are not a byte-for-byte or descriptor-identical copy.

PlotJuggler-specific differences include:

- the protobuf package is `PJ`, not `foxglove`;
- imports use `pj/...`;
- `Point2`, `Point3`, `Vector2`, `Vector3`, and `Quaternion` are grouped in
  `Geometry.proto`;
- the C++ codec maps only the fields represented by
  `PJ::sdk::ImageAnnotations`.

## SDK Mapping

The wire schema is slightly richer than the current SDK value. The codec maps
the renderable annotation fields and ignores the rest:

| Schema field | SDK behavior |
|--------------|--------------|
| `ImageAnnotations.circles` | Mapped to `ImageAnnotations::circles`. |
| `ImageAnnotations.points` | Mapped to `ImageAnnotations::points`. |
| `ImageAnnotations.texts` | Mapped to `ImageAnnotations::texts`. |
| Top-level `timestamp` | Serialized when non-zero, decoded into `ImageAnnotations::timestamp`; a timestamp of 0 is indistinguishable from an absent one. |
| `image_topic` (field 6, string) | Serialized when non-empty, decoded into `ImageAnnotations::image_topic`. |
| Top-level `metadata` | Not serialized or decoded today. |
| Per-annotation `timestamp` | Not serialized or decoded today. |
| Per-annotation `metadata` | Not serialized or decoded today. |
| `TextAnnotation.background_color` | Not represented by the SDK type; skipped on decode and not emitted on encode. |

`PJ::sdk::ImageAnnotations::image_topic` is part of the payload: it is written
as field 6 when non-empty, so runtime association between an annotation set
and its image stream can be carried on the wire. A reader predating this
addition ignores the unknown field.

## Codec Rules

Circles are stored in the wire schema as `diameter`; the SDK stores `radius`.
The codec converts between `diameter` and `radius * 2.0`.

Colors are stored in the schema as normalized `double` channels in `[0, 1]`.
The SDK stores RGBA `uint8_t` channels. Decode clamps normalized values to
`[0, 1]` and rounds to the nearest byte, so a round trip may differ by one
channel value because of floating-point rounding.

Point topology uses the schema enum values:

| Wire value | SDK topology |
|------------|--------------|
| `1` | `kPoints` |
| `2` | `kLineLoop` |
| `3` | `kLineStrip` |
| `4` | `kLineList` |

The writer never emits `UNKNOWN`/`0`. The reader maps unknown or zero topology
values to `kPoints`, so incomplete or forward-compatible payloads still decode
to a renderable annotation.

An annotation value with no circles, points, or texts serializes to an empty byte
buffer. Decoding an empty buffer is treated as invalid input by the current
reader.

The reader accepts protobuf wire types `VARINT`, `I64`, `LEN`, and `I32`. It
decodes the mapped fields and skips unknown fields, including unknown nested
fields, so compatible schema additions can be tolerated. Malformed protobuf data,
invalid length-delimited fields, or truncated nested messages fail decoding.
