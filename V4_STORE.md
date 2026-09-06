# ObjectStore Plugin ABI

`pj_datastore::ObjectStore` is the timestamped opaque-payload store that
complements `DataEngine`. `DataEngine` stores typed scalar series; `ObjectStore`
stores raw bytes for object topics such as structured messages, images, point
clouds, annotations, and other domain-specific payloads.

The v4 plugin ABI exposes ObjectStore through optional services. A plugin family
can use scalar storage, object storage, or both without changing its protocol
entry point.

## Services

| Service | Raw ABI type | C++ SDK view | Consumer |
|---|---|---|---|
| `pj.source_object_write.v1` | `PJ_object_write_host_t` | `SourceObjectWriteHostView` | DataSource plugins that create object topics |
| `pj.parser_object_write.v1` | `PJ_parser_object_write_host_t` | `ParserObjectWriteHostView` | MessageParser plugins bound to an object topic |
| `pj.toolbox_object_read.v1` | `PJ_object_read_host_t` | `ToolboxObjectReadHostView` | Toolbox plugins that read object topics |
| `pj.toolbox.v1` | `PJ_toolbox_host_t` | `ToolboxHostView` | Toolbox plugins that create or annotate object topics |

The service traits live in `pj_base/include/pj_base/sdk/service_traits.hpp`.
The raw ABI structs live in `pj_base/include/pj_base/plugin_data_api.h`.
The C++ SDK views live in `pj_base/include/pj_base/sdk/plugin_data_api.hpp`.

## DataSource Object Writes

DataSource plugins resolve `pj.source_object_write.v1` when they need to publish
object topics. The source-scoped write host supports:

- `registerTopic(name, metadata_json)`: create an object topic under the current
  dataset. The metadata JSON is opaque to core and is retained verbatim. A
  renderable built-in object uses `builtin_object_type` with the exact
  `PJ::sdk::name()` value (for example, `"kImage"`). The C++ view also offers
  `registerTopic(name, BuiltinObjectType, extra_metadata)` to build this
  canonical field safely.
- `pushOwned(topic, timestamp, bytes)`: eager write; the store copies the bytes.
- `pushLazy(topic, timestamp, fetch)`: lazy write; the store keeps a fetch
  closure and resolves bytes on demand.
- `setRetentionBudget(topic, time_window_ns, max_memory_bytes)`: configure
  automatic eviction for a topic.

The host-side implementation is `DatastoreSourceObjectWriteHost` (in the
`pj_datastore` module of the PlotJuggler application repo, not part of this SDK).

## Parser Object Writes

MessageParser plugins resolve `pj.parser_object_write.v1` only when the host has
bound that parser instance to an object topic. The parser-scoped host mirrors the
scalar parser write host: the topic is selected by the host, and the parser only
pushes payloads.

The parser object write host supports eager and lazy writes:

- `pushOwned(timestamp, bytes)`
- `pushLazy(timestamp, fetch)`

This lets a parser write scalar fields through `pj.parser_write.v1` and a raw
payload through `pj.parser_object_write.v1` from the same `parse()` call.

The host-side implementation is `DatastoreParserObjectWriteHost`.

## Toolbox Object Writes

Use `ToolboxHostView` from `pj.toolbox.v1` to publish objects from a toolbox:

- `registerObjectTopic(source, name, metadata_json)` registers under a source
  created by the toolbox.
- `registerObjectTopicOnDataset(dataset, name, metadata_json)` registers under
  an existing `DatasetId`; repeated registration returns the existing handle.
- Both registration methods accept `BuiltinObjectType` with optional
  `ObjectTopicMetadataBuilder` metadata to set the canonical renderer key.
- `pushOwnedObject(topic, timestamp, bytes)` copies the payload into the store.
- `setObjectTopicRetention(topic, max_entries)` bounds retained snapshots;
  use `1` when republishing a whole marker set.

These optional tail slots return an error on older hosts. Reuse them for
[plot markers](docs/plot_markers_architecture.md); no marker-specific store or
per-marker mutation API is needed.

## Toolbox Object Reads

Toolbox plugins resolve `pj.toolbox_object_read.v1` when they need read access
to ObjectStore topics. The read host supports:

- `lookupTopic(name)` for name-only lookup
- `lookupTopicOnDataset(dataset, name)` to disambiguate names shared by loaded
  datasets; returns `nullopt` on a miss or when the host lacks this tail slot
- topic enumeration
- metadata lookup
- entry count and time range queries
- `readLatestAt(topic, timestamp)`

Successful reads return an owning byte handle. The handle keeps the resolved
bytes alive independently of later store mutations, evictions, or topic removal.
The plugin releases the handle through the SDK wrapper.

The host-side implementation is `DatastoreToolboxObjectReadHost`.

## Ownership Rules

Owned entries are copied into the store immediately.

Lazy entries retain a plugin-provided fetch context until the entry is evicted,
the topic is removed, or the store is cleared. The destroy callback runs exactly
once. The SDK `pushLazy()` overloads hide the raw C callback and destroy-function
plumbing behind a C++ callable.

Reads return stable handles. A caller that already holds a handle can continue
using it after the ObjectStore changes because the handle owns a shared reference
to the resolved byte buffer.

## Core Boundary

ObjectStore stores bytes without interpreting their domain. It does not decode
payloads, choose renderers, maintain UI state or define topic-specific
presentation policy. It stores metadata as opaque JSON for callers to interpret.

At the SDK/application boundary, `builtin_object_type` is the one canonical
renderer-discovery key. Its values are the exact names from `PJ::sdk::name()`.

## Tests

The ObjectStore ABI surface is covered by tests in the `pj_datastore` module of
the PlotJuggler application repo:

- `tests/plugin_data_host_object_test.cpp`
- `tests/plugin_data_host_object_read_test.cpp`
- `tests/plugin_parser_object_write_test.cpp`

The underlying store behavior is covered by `tests/object_store_test.cpp`.
