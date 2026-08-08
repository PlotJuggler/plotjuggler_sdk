# Functional parser module

A functional parser module maps one or a few message types from an existing
encoding to canonical PlotJuggler objects or scalar fields. It is a single C++17
source compiled as a native shared library, with no plugin-family vtable or
MessageParser boilerplate.

Use a **MessageParser plugin** when you own an encoding and must decode its broad
type universe. Use a **parser module** when the encoding already exists and you
only need an exact custom type rendered as an object or scalars.

## Minimal native module

```cpp
#include <pj_base/parser_module/module.hpp>

#include <cstdint>
#include <limits>

class RawMonoImageParser final : public pj::FunctionalParser {
 public:
  pj::Status bind(const pj::BindingInfo& info) override {
    if (info.route() != pj::Route::kObject ||
        info.expectedObjectType() != pj::ObjectWriter::kImageObjectType) {
      return pj::Status::decline("this module only produces Image objects");
    }
    return pj::Status::ok();
  }

  pj::Status parseObject(pj::PayloadView payload, pj::Timestamp timestamp,
                         pj::ObjectWriter& output) override {
    if (payload.size > std::numeric_limits<uint32_t>::max()) {
      return pj::Status::error("image row is too large");
    }

    auto image = output.image();
    if (timestamp.has_value) {
      if (auto status = image.setTimestamp(timestamp.nanoseconds);
          !status.isOk()) {
        return status;
      }
    }
    const auto width = static_cast<uint32_t>(payload.size);
    if (auto status = image.setWidth(width); !status.isOk()) {
      return status;
    }
    if (auto status = image.setHeight(1); !status.isOk()) {
      return status;
    }
    if (auto status = image.setEncoding("mono8"); !status.isOk()) {
      return status;
    }
    if (auto status = image.setRowStep(width); !status.isOk()) {
      return status;
    }
    return image.setData(payload);
  }
};

PJ_FUNCTIONAL_PARSER(RawMonoImageParser)
```

`Timestamp` is per-message input. Check `has_value` before using
`nanoseconds`. Override `parseScalars(PayloadView, Timestamp, ScalarWriter&)`
instead, or as well, when the manifest claims the scalar route.

## Claims manifest

```json
{
  "module_abi": 1,
  "id": "com.example.raw-mono-image",
  "name": "Raw mono image parser",
  "version": "1.0.0",
  "claims": [
    {
      "claim_id": "raw-image-v1",
      "encoding": "ros2msg",
      "type_name": "example_msgs/msg/RawImage",
      "routes": ["object"],
      "object_type": "kImage",
      "schema_digests": [
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
      ],
      "priority": 0
    }
  ]
}
```

- `module_abi` must equal the frozen parser-module ABI version (`1`).
- `id` is the stable provider identity. Never change it for a display rename.
- `name` is display-only metadata.
- `version` is a valid SemVer string.
- `claim_id` is stable and unique within `id`; together they form claim identity.
- `encoding` is a case-sensitive SDK-registered encoding such as `ros2msg` or
  `protobuf`.
- `type_name` is encoding-normalized: `pkg/msg/Type` for `ros2msg`, or the full
  dotted protobuf message name. Modules claim exact types, not `"*"` objects.
- `routes` is a non-empty array containing `"scalar"`, `"object"`, or both.
- `object_type` is required with the object route and forbidden without it. Use
  the exact `BuiltinObjectType` spelling, such as `kImage`.
- `schema_digests` is optional. Each entry is `sha256:` plus 64 hexadecimal
  digits; an empty or omitted set accepts any schema digest at catalog matching.
- `priority` is required and must be in `[-1000, 1000]`. Host-owned provenance
  tiers outrank priority, so do not use priority as a trust signal.

Manifest order fixes each claim's `claimIndex()`. The native loader copies the
embedded bytes; catalog ingestion validates the complete JSON transactionally.

## Build

```cmake
find_package(plotjuggler_sdk 0.22 REQUIRED COMPONENTS parser_module)

pj_add_parser_module(raw_mono_image_parser
  SOURCE raw_mono_image_parser.cpp
  MANIFEST raw_mono_image_parser.module.json
  TARGETS native
)
```

The helper embeds the manifest, hides every non-ABI symbol, and exports the
complete native `pj_module_*` set. SDK 0.22 accepts only `TARGETS native`;
requesting `TARGETS wasm` stops configuration with “wasm support arrives with
the SDK wasm loader milestone”. The shipped WASI check is structural
conformance testing, not a wasm authoring or execution target.

## Choose a schema-compatibility strategy

Use the least brittle rung that fits the format:

1. **Hardcode one layout** only when the wire schema is immutable and externally
   versioned.
2. **Digest allow-list.** Put accepted `sha256:<64-hex>` values in the claim and
   return `Status::decline(...)` from `bind()` for an unsupported revision, so
   the resolver may try another claim.
3. **Inspect at bind.** Compile requested paths once with `CdrFieldLocator` from
   a ROS 2 concatenated `.msg` bundle, or `ProtoFieldLocator` from a serialized
   `FileDescriptorSet`, then reuse the plan for every message.

`CdrFieldLocator` traverses fixed arrays, bounded/unbounded sequences, bounded
strings, and string arrays/sequences. Cyclic schemas and traversal deeper than
64 levels fail at bind with an error `Status`. The CDR and protobuf readers also
bounds-check every per-message traversal.

## ObjectWriter builders and splices

The nine builders with frozen splice-eligible bulk fields are:

- `image()`
- `pointCloud()`
- `depthImage()`
- `occupancyGrid()`
- `compressedPointCloud()`
- `mesh3D()`
- `videoFrame()`
- `occupancyGridUpdate()`
- `voxelGrid()`

Every builder has `setData(PayloadView)`, which copies the bytes into the full
canonical wire object. Every builder also has
`setDataFromInput(InputSpanRef)`, which omits that one bulk field from the wire
and records one splice into the exact parse payload. Obtain a safe reference
from `CdrReader::spanRef(...)` when possible. Splice offsets are relative to the
payload start, not the CDR encapsulation or a nested field; the writer rejects
out-of-range, repeated, or mixed copy/splice selection before it can emit an
invalid descriptor.

## Traps

- The kit is header-only and WASI-clean: no threads, filesystem, iostream, host
  SDK linkage, or exceptions across its API. In SDK 0.22 the supported build
  product is nevertheless native-only.
- Return `pj::Status` / `pj::Expected<T>`; do not throw. `Blob` uses nothrow
  allocation and protobuf matching is bounded, so allocation failure is a
  reported data error rather than a process abort or contract strike.
- Every view in `BindingInfo` is borrowed until `bind()` returns. Call
  `info.owningCopy()` and retain the returned `OwnedBindingInfo` if later parses
  need any field.
- `Status::decline(...)` from bind means “this valid claim does not accept this
  binding”. A module-reported negative parse result, including malformed
  application data or allocation failure, is a data error and does not earn a
  strike.
- Bad/stale-token returns, a successful call that supplies a malformed output
  descriptor, wrong route or object type, or an ineligible/out-of-bounds splice
  are contract violations. Native hosts feed those results to
  `ParserModuleStrikeTracker`: three strikes quarantine the claim; after one
  successful create/bind replay, another three-strike cycle disables it for the
  session. The tracker is host-driven state, not module-author API.
- Instance tokens are generated index+generation values. Token `0` is reserved
  for creation errors; stale tokens are rejected and leave a retrievable
  diagnostic. Do not derive meaning from token bits.
