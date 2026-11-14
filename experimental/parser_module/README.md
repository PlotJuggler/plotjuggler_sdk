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
| `include/pj_base/parser_module_wasm.hpp` | Static wasm artifact audit shared by the loader and the embed tool |
| `include/pj_plugins/host/` | Native and wasm loaders, instance runtimes, session budget, manifest-to-claims decoding |
| `cmake/PjParserModule.cmake` | `pj_add_parser_module()` (native and wasm targets) |
| `cmake/parser_module_wasi_no_io_stubs.cpp` | WASI I/O fallbacks that keep authored reactors import-free |
| `tools/pj_wasm_embed_manifest.cpp` | `pj-wasm-embed-manifest`: embed or verify manifest bytes, run the static audit |
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

Native and wasm modules are built with `pj_add_parser_module` using `TARGETS
native`, `TARGETS wasm`, or both. Each target links no SDK library; its include
paths expose this kit and the shared checked time arithmetic. Wasm targets
require `PJ_WASI_SDK_ROOT` to select wasi-sdk 27 and are post-linked through
the `pj-wasm-embed-manifest` embed-and-audit frontend.

WASI reactor modules use the same operational exports and compile with
exceptions disabled. Their manifest is delivered in the
`pj_parser_module_manifest` custom section, so the native-only manifest address
and length exports are omitted automatically when targeting wasm. Authored
reactors have an empty import set, a declared linear-memory maximum (default
256 MiB, configurable with `PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES`), and a
declared table maximum (wasm-ld emits one; the host caps it at 65536 elements).

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
`ParserModuleStrikeTracker` quarantines a module claim on its third contract
violation, permits one same-descriptor recreation, and disables the claim after
a repeated three-strike cycle or a contract violation during the replay itself.
It is shared by native and wasm instances and is thread-safe, because the scalar
and object routes of one claim run on different threads. Executor placement,
generation ownership, folder scanning, and rescan policy remain application
concerns.

Module authors use the standalone C++17 headers under
`include/pj_base/parser_module/` through the zero-linkage
`plotjuggler_sdk::parser_module` target. The readers compile ROS 2 concatenated
`.msg` bundles or protobuf `FileDescriptorSet` field paths at bind time; the
CDR locator supports fixed arrays, bounded and unbounded sequences, bounded
strings, and string arrays/sequences, while cyclic or over-depth schemas fail at
bind. The typed `ObjectWriter` emits Image, PointCloud, DepthImage,
OccupancyGrid, CompressedPointCloud, Mesh3D, VideoFrame, OccupancyGridUpdate,
and VoxelGrid descriptors, including each type's eligible single-splice form.
Module parse callbacks receive the per-message `pj::Timestamp` alongside the
payload. `BindingInfo` views expire when `bind()` returns; `owningCopy()` is the
fallible retention path. Bulk storage uses nothrow allocation, and protobuf
matching is bounded, so allocation failure is returned as data error rather
than escaping as a trap. `PJ_FUNCTIONAL_PARSER` supplies the complete native
export set, uses synchronized index+generation instance tokens to reject stale
handles, and catches user exceptions at the C boundary.
`pj_add_parser_module(... TARGETS native wasm)` builds hidden-visibility native
and wasi-sdk 27 reactor artifacts from one source. It embeds the JSON manifest
behind the native metadata exports and in the wasm custom section. Wasm
reactors omit those two metadata exports and carry the exact JSON bytes in the
`pj_parser_module_manifest` custom section. The shared host codec appends and
reads that section; the conditional wasi-sdk 27 compile gate builds the same
toy module with C++17 and exceptions disabled, then statically audits the
reactor model and every operational export signature without executing wasm.

Native modules use the same absolute-path normalization, package-scoped
platform open, and defining-module provenance checks as the plugin-family
loaders for every required ABI export (`pj_plugins/src/detail/library_loader.hpp`).
Their narrow path API is explicitly UTF-8 on Windows.

## WebAssembly loader

`WasmParserModule` and `WasmParserModuleInstance` (target
`pj_wasm_parser_module_host`) load and execute wasm reactors through a
statically linked Wasmer 7.0.1 C API. Like everything here, the executor is
in-tree only: nothing is installed or packaged, so installed SDK packages stay
Wasmer-free.

### Requirements and CMake variables

| Variable | Meaning |
|---|---|
| `PJ_WASI_SDK_ROOT` (cache or environment) | wasi-sdk 27 root. Enables `TARGETS wasm`, the WASI conformance test (label `wasi`) and the wasm fixtures. Any other major version is a configure error. |
| `PJ_WASMER_ROOT` (cache or environment) | Wasmer 7.0.1 C-API root with `include/wasm.h`, `include/wasmer.h` and `lib/libwasmer.a`. Enables `pj_wasm_parser_module_host` and the execution tests (label `wasmer`). Any other version is a configure error. |
| `PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES` | Declared linear-memory maximum of authored reactors; default 256 MiB, a positive multiple of 64 KiB. |
| `PJ_PARSER_MODULE_WASM_STACK_SIZE_BYTES` | Shadow-stack size of authored reactors; default 1 MiB, at least 256 KiB. |

Either root left unset skips its part with a status message. CI installs both
and runs `ctest -L wasi` and `ctest -L wasmer`.

### Admission

Before compilation the loader runs the same static audit as the
`pj-wasm-embed-manifest` tool (`validateParserModuleWasmArtifact`): exactly one
manifest section, an empty import set (the frozen v1 allow-list), the frozen
export ABI, `_initialize`, no start function, exported linear memory with a
declared maximum, and tables with a declared maximum. The authoring preset
supplies unreachable WASI I/O fallbacks (`cmake/parser_module_wasi_no_io_stubs.cpp`),
so no fd, path, socket, clock, random, environment or scheduler capability
enters the allow-list. Compilation prefers Wasmer's Singlepass backend (linear
compile time; there is no cancellable compile API) and always runs
synchronously inside `load()`, so the host keeps it off the UI thread.

### Execution

One engine-owned compiled module is instantiated in one independent store per
bound instance. Store calls may migrate between threads sequentially, but the
host must serialize calls on an instance. ABI blocks are copied through guest
allocation; the runtime reacquires linear memory after every guest call,
validates every returned range, and resolves splices against the original host
payload.

### Fault policy

The wasm wrapper carries exactly the native wrapper's fault contract: guest
traps and metering exhaustion join malformed descriptors and bad offsets as
returned contract violations, and module-reported parse errors are strike-free
data errors. The wrapper only classifies; the host records faults, quarantines,
replays create/bind and disables through the shared `ParserModuleStrikeTracker`.

### Limits and session budget

| Limit | Default | Where |
|---|---|---|
| Instructions per guest call (Wasmer metering, reset for every ABI call) | 10,000,000 | `WasmParserModuleLimits::metering_points_per_call` |
| Artifact size | 64 MiB | `WasmParserModuleLimits::maximum_artifact_bytes` |
| Declared linear-memory maximum | 256 MiB | `ParserModuleWasmLimits::maximum_linear_memory_bytes` |
| Declared table elements (host memory outside the linear-memory budget) | 65536 | `ParserModuleWasmLimits::maximum_table_elements` |
| Type / import / export entries inspected (WebAssembly JS-API limits) | 1,000,000 / 100,000 / 100,000 | `parser_module::kWasmMaximumTypes`, `kWasmMaximumImports`, `kWasmMaximumExports` |
| Manifest JSON size | 4 MiB | `kMaximumParserModuleManifestBytes` |
| Claims per manifest | 4096 | `kMaximumParserModuleManifestClaims` |
| Bytes in each manifest string (`id`, `name`, `version`, `claim_id`, `encoding`, `type_name`, each schema digest) | 1024 | `kMaximumParserModuleManifestStringBytes` |
| Schema digests per claim | 64 | `kMaximumParserModuleClaimSchemaDigests` |
| Fields in one scalar output | 1,048,576 | `parser_module::kMaximumScalarOutputFields` |
| Field-name plus string-value bytes in one scalar output (names may alias) | 64 MiB | `parser_module::kMaximumScalarOutputBytes` |

The fixed limits bound what the host allocates while reading untrusted
metadata, which guest memory caps and metering do not cover. The entry counts
apply to wasm inspection; the manifest bounds apply to every manifest decode,
native or wasm, as do the scalar output bounds (`readOutputDescriptorV1` rejects an over-limit descriptor, so it
is a contract violation). Host allocation failure inside the limits is not a
module fault: `load()` rejects, and a wasm `parse()` returns a strike-free data
error with `PJ_MODULE_ERR_ALLOCATION_FAILURE`.

Metering is an instruction budget, not a wall-clock deadline: bulk operators
such as `memory.copy` cost one point regardless of size, and the pinned archive
exposes no public interrupt or epoch API. Native stack depth uses Wasmer's
guarded default.

The optional, thread-safe `ParserModuleSessionBudgetTracker` bounds a whole
session: module count (64), artifact bytes (64 MiB each), total claims (4096),
active instances (128) and aggregate per-instance declared memory (4 GiB). It
counts resources under opaque reservation ids (one per module and one per
instance, each consumed once on release) and never keys on manifest identity,
which remains catalog policy. Both loaders accept a null budget and
then perform no admission accounting; `NativeParserModule::load(path, sink)`
keeps its budget-free behaviour.

### Wasmer pin (7.0.1, evaluated against 7.2.1 on 2026-08-09)

- 7.2.x adds nothing the loader needs: `wasm_module_share/obtain` are still
  absent from the static archive, the exported metering symbol set is
  identical, and the "interruptable computation" work remains internal Rust
  surface with no public C interrupt/epoch API. The only C-API additions are
  `wasmer_features_*` toggles the loader does not require.
- The WASI-syscall CVEs fixed in 7.2.0 (unbounded host allocation in
  `getcwd`/`random_get`, `poll_oneoff`, `sock_recv`/`sock_recv_from`) are
  structurally unreachable here: the empty import allow-list rejects any
  module importing those syscalls before instantiation. **Re-evaluate the pin
  before ever widening the import allow-list**; no release currently combines
  those fixes with x86_64-darwin support.
- 7.2.0 dropped the x86_64-darwin target, so moving the pin would end wasm
  parser-module support on Intel macOS.
