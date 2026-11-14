# Functional parser modules (experimental)

**Status: experimental.** Nothing under `experimental/parser_module/` is
installed, packaged, or covered by the SDK compatibility promise. The export
ABI (`PJ_PARSER_MODULE_ABI_VERSION` 1), the manifest schema and the authoring
API may all change without a MAJOR release. No host loads modules yet.

It is built and tested here when `PJ_BUILD_EXPERIMENTAL` is ON (the default
follows `PJ_BUILD_TESTS`). Headers keep their `pj_base/...` and
`pj_plugins/host/...` include spellings, so promoting the component back into
the installed SDK is a move, not a rewrite.

| Path | Contents |
|---|---|
| `include/pj_base/parser_module/` | Header-only C++17 authoring kit (`module.hpp` is the umbrella) |
| `include/pj_base/parser_module_abi.h`, `parser_module_manifest.hpp` | Export ABI, block codecs, wasm manifest-section codec |
| `include/pj_plugins/host/` | Native loader, instance runtime, manifest-to-claims decoding |
| `cmake/PjParserModule.cmake` | `pj_add_parser_module()` |
| `AUTHORING.md` | Manifest, schema-locator, splice, lifetime and error contracts |

Open before the ABI can be called stable:

- The object output descriptor carries no record timestamp; the scalar output does.
- The parse input copies the whole payload inline on every message.
- A module cannot report a contract violation through a result code, only
  through a `pj.parser.contract_violation:` message prefix.
- `decodeParserModuleManifest` accepts exactly one `module_abi` value.

## Authoring kit

The kit is standalone: its headers may include others in
`include/pj_base/parser_module/`, C/C++ standard-library headers and the shared
C++17 checked arithmetic in `pj_base/time_math.hpp`. `time.hpp` uses that
arithmetic.

Headers must remain suitable for a WASI reactor build: no filesystem, threads,
iostreams, host SDK linkage or exceptions crossing public/ABI calls.

All data received from a host or message is borrowed through `ByteView` /
`PayloadView`. Fallible operations return the local `pj::Status` or
`pj::Expected<T>` types. Output storage is owned by `pj::Blob`, whose allocation
entry points report failure instead of exposing allocation exceptions.

The umbrella include for module authors is:

```cpp
#include <pj_base/parser_module/module.hpp>

class RawImageParser final : public pj::FunctionalParser {
 public:
  pj::Status bind(const pj::BindingInfo& info) override {
    return info.route() == pj::Route::kObject
               ? pj::Status::ok()
               : pj::Status::decline("object route only");
  }

  pj::Status parseObject(pj::PayloadView payload, pj::Timestamp timestamp,
                         pj::ObjectWriter& output) override {
    auto image = output.image();
    if (timestamp.has_value) {
      if (auto status = image.setTimestamp(timestamp.nanoseconds);
          !status.isOk()) {
        return status;
      }
    }
    if (auto status = image.setEncoding("mono8"); !status.isOk()) {
      return status;
    }
    return image.setData(payload);
  }
};

PJ_FUNCTIONAL_PARSER(RawImageParser)
```

Override `parseScalars(PayloadView, Timestamp, ScalarWriter&)` for scalar
claims. `ObjectWriter` provides `image`, `pointCloud`, `depthImage`,
`occupancyGrid`, `compressedPointCloud`, `mesh3D`, `videoFrame`,
`occupancyGridUpdate`, `voxelGrid`, and `gridMap` builders.

Native modules are built with `pj_add_parser_module(... TARGETS native)`. The
target links no SDK library; its include paths expose this kit and the shared
checked time arithmetic.
`TARGETS wasm` is not available. The wasi-sdk gate compiles and
statically audits a reactor fixture, but it does not provide wasm authoring or
execution.

See [AUTHORING.md](AUTHORING.md) for the manifest, schema-locator, splice,
lifetime, and error contracts.

## Host loader and runtime

Functional parser modules use the family-independent export ABI in
`include/pj_base/parser_module_abi.h`, not a plugin-family vtable. The host-side
`NativeParserModule` loader opens each artifact with local, immediate symbol
resolution, resolves every operational and native metadata export, gates ABI
version 1, and copies the embedded manifest for subsequent
`ParserClaimCatalog` admission. Native module mappings have process-session
lifetime in v1 and are never unloaded.

`NativeParserModuleInstance` owns one create/bind/parse/destroy lifecycle. It
uses the frozen little-endian codecs for input and output, consumes all
module-owned descriptor views before returning, validates output route and
expected object type, and checks a splice against both the canonical object's
eligible field and the original payload bounds. A valid splice is materialized
into that field, so the returned canonical object is complete; its sidecar
retains the original offset and bytes for future zero-copy integration.
Malformed returned descriptors, ineligible/out-of-bounds returned splices,
type/route mismatch, and bad tokens are contract violations; other
module-reported per-message failures are data errors. The separate
non-thread-safe `ParserModuleStrikeTracker` quarantines a module claim on its
third contract violation, permits one same-descriptor recreation, and disables
the claim after a repeated three-strike cycle. Executor placement, generation
ownership, folder scanning, and rescan policy remain application concerns.

Module authors use the standalone C++17 headers under
`include/pj_base/parser_module/` through the zero-linkage
`plotjuggler_sdk::parser_module` target, an in-tree ALIAS only (nothing here is
installed or packaged). The readers compile ROS 2 concatenated
`.msg` bundles or protobuf `FileDescriptorSet` field paths at bind time; the
CDR locator supports fixed arrays, bounded and unbounded sequences, bounded
strings, and string arrays/sequences, while cyclic or over-depth schemas fail at
bind. The typed `ObjectWriter` emits Image, PointCloud, DepthImage,
OccupancyGrid, CompressedPointCloud, Mesh3D, VideoFrame, OccupancyGridUpdate,
VoxelGrid and GridMap descriptors, including each type's eligible single-splice form.
Module parse callbacks receive the per-message `pj::Timestamp` alongside the
payload. `BindingInfo` views expire when `bind()` returns; `owningCopy()` is the
fallible retention path. Bulk storage uses nothrow allocation, and protobuf
matching is bounded, so allocation failure is returned as data error rather
than escaping as a trap. `PJ_FUNCTIONAL_PARSER` supplies the complete native
export set, uses synchronized index+generation instance tokens to reject stale
handles, and catches user exceptions at the C boundary.
`pj_add_parser_module(... TARGETS native)` builds a hidden-visibility module and
embeds its JSON manifest behind the native metadata exports. `TARGETS wasm` is
not available. The shared host codec can append and read the exact
JSON bytes in a wasm `pj_parser_module_manifest` custom section; the conditional
wasi-sdk 27 compile gate builds the toy source with C++17 and exceptions
disabled, then statically audits the reactor model and every operational export
signature. This is structural conformance only: no wasm loader or execution
runtime exists.

Native modules use the same absolute-path normalization, package-scoped
platform open, and defining-module provenance checks as the plugin-family
loaders for every required ABI export (`pj_plugins/src/detail/library_loader.hpp`).
Their narrow path API is explicitly UTF-8 on Windows.
