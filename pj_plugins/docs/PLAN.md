# DataSource Execution Plan

## Summary

The shared plugin data interface is already implemented and tested.

Completed foundation:

- `pj_base` owns the plugin-facing data ABI and C++ SDK.
- `pj_datastore` owns the datastore-backed implementations of those data hosts.
- DataSource work must build on top of that foundation and must not introduce a
  second parallel plugin SDK for data access.
- The external plugin SDK must be consumable from an independent repository,
  with Conan as the primary distribution path.

The remaining project is the DataSource control plane:

- define the portable `DataSource` family ABI and C++ SDK
- make that SDK consumable by external plugin authors through packageable CMake
  targets/components
- define the host runtime that binds one source instance to one application
  `data_source`
- integrate delegated parsing through a host-owned parser-binding surface
- reuse the existing dialog protocol for configuration, including integrated
  host-owned parser controls
- migrate legacy sources in a staged order from simplest to most demanding

This plan covers `DataSource` only.

## 1. Current Status

### 1.1 Done

The following work is complete and should be treated as fixed input:

- plugin-facing data ABI in `pj_base`
- C++ wrappers for source, parser, and toolbox data hosts in `pj_base`
- datastore-backed source/parser/toolbox host adapters in `pj_datastore`
- unit tests for the shared data interface and datastore adapters

Consequences:

- `DataSource` plugins must reuse `PJ_source_write_host_t` from `pj_base`
- delegated parser integration must reuse the existing parser write host model
- no new topic/field/value ABI should be invented in the DataSource layer
- plugin authors in an external repository must not need `pj_datastore` or
  Abseil to build against the SDK

### 1.2 Not the target architecture

Do not treat a separate `pj_plugins/data_source_protocol` public SDK as the end
state.

The agreed direction is:

- `pj_base`: plugin-facing ABI headers and plugin-side C++ wrappers, packaged as
  the external SDK core
- `pj_datastore`: datastore-backed host implementations for the shared data plane
- `pj_plugins`: host runtime, library loading, dialog integration, parser
  binding, and concrete source plugins

If any prototype exists under `pj_plugins/data_source_protocol`, it is only a
temporary reference and must be relocated or removed rather than extended as a
second public SDK.

## 2. Target Split of Responsibilities

### 2.1 `pj_base`

`pj_base` must contain all plugin-facing DataSource control-plane definitions:

- raw DataSource ABI header
- plugin-side C++ wrapper / base class
- runtime-host views used by DataSource plugins
- common enums and small POD structs specific to DataSource lifecycle/control
- package-facing CMake targets/components for external plugin authors

It must not depend on `pj_datastore`, Qt, or Abseil.

The external SDK must be usable:

- from a Conan package as the primary path
- from git submodules or vendored source as a secondary path
- without requiring plugin authors to include or link `pj_datastore`

### 2.2 `pj_datastore`

`pj_datastore` remains responsible only for the shared data plane:

- `PJ_source_write_host_t`
- `PJ_parser_write_host_t`
- `PJ_toolbox_host_t`

No new datastore-side data-access surface is planned for DataSource.

Any additional work in `pj_datastore` should only happen if the DataSource
runtime uncovers a missing capability in the already-defined data hosts.

### 2.3 `pj_plugins`

`pj_plugins` must contain host-side orchestration:

- shared-library loading of DataSource plugins
- instance lifetime wrappers
- runtime host implementation for messages, progress, stop requests, and parser
  binding
- integration with `dialog_protocol`
- migration of legacy source plugins

`pj_plugins` is host-side only. It is not the dependency root that an external
plugin repository should need to consume.

## 3. Public Interfaces Still To Build

### 3.1 DataSource plugin ABI in `pj_base`

Define one raw ABI plus one C++ SDK wrapper for the `DataSource` family.

The exported DataSource instance surface must include:

- `create` / `destroy`
- `get_manifest`
- `bind_write_host`
- `bind_runtime_host`
- `load_config`
- `save_config`
- `start`
- `stop`
- `pause`
- `resume`
- `poll`
- `current_state`
- `capabilities`
- `get_last_error`

Rules:

- `bind_write_host` uses the already-implemented `PJ_source_write_host_t`
- `bind_runtime_host` provides DataSource-specific control-plane services only
- `pause` and `resume` are always present in the ABI but may return
  `not_supported`
- `poll` is a host-driven cooperative callback invoked after successful `start`
  while the instance remains non-terminal
- the host owns poll cadence; the source must tolerate redundant calls
- `poll` returns `true` when the call succeeded, even if it did no work
- `poll` returns `false` only for an unrecoverable poll-time failure; details
  are available through `get_last_error()`
- `poll` must not block; long-running work belongs on source-owned threads
- no new data-plane vocabulary is introduced here

### 3.2 Package-facing SDK shape

The DataSource work must produce package-facing SDK targets/components for
external plugin authors.

Required shape:

- one core SDK component rooted in `pj_base`
- one DataSource SDK component layered on top of that core
- one dialog SDK component for UI-capable plugins

Rules:

- a headless DataSource plugin must be buildable with the core SDK plus the
  DataSource SDK only
- a UI-capable DataSource plugin may add the dialog SDK component
- the host runtime and datastore adapters are not part of the plugin author
  dependency surface
- the SDK targets/components must be suitable for Conan packaging

### 3.3 Capability flags

Lock the v1 capability model to this set:

- `finite_import`
- `continuous_stream`
- `direct_ingest`
- `delegated_ingest`
- `supports_pause`
- `has_dialog`

Do not add arbitrary plugin-defined actions in v1.

### 3.4 Runtime states

Lock the DataSource state machine to:

- `idle`
- `configuring`
- `starting`
- `running`
- `paused`
- `stopping`
- `stopped`
- `failed`

Transitions:

- instance creation starts in `idle`
- dialog display is a host concern, but while the host is driving dialog-based
  setup the source is treated as `configuring`
- `start` transitions to `starting`, then either `running` or `failed`
- `pause` transitions `running -> paused`
- `resume` transitions `paused -> running`
- `stop` transitions to `stopping`, then `stopped`
- a finite source that has completed its import calls `request_stop(stopped, "")`
  through the runtime host; the host then transitions it to `stopping` then
  `stopped`
- a finite source that encounters an unrecoverable error during import calls
  `request_stop(failed, reason)` through the runtime host
- a continuous source may transition `running -> starting -> running` while it
  is internally reconnecting after a recoverable transport disruption
- asynchronous runtime failures transition to `failed`; the host stops and
  disposes the instance
- `failed` is terminal and reserved for unrecoverable errors
- recoverable transport disruptions are handled inside the source, reported
  through runtime messages, and do not require host disposal or restart
- host auto-restart of a failed source is optional policy, not part of the
  plugin ABI contract

### 3.5 DataSource runtime host surface

Define one runtime-host ABI provided to each DataSource instance.

It must provide:

- message reporting
- progress begin / update / finish
- cancellation check for long-running finite imports
- state notification
- source-initiated stop / close request
- delegated parser binding and raw-message dispatch

The runtime host API must be safe to call from source-owned worker threads.

Rules:

- `notify_state` is informational only; it does not by itself terminate or
  dispose the source instance
- plugin-initiated successful completion or terminal failure must use
  `request_stop(...)`

### 3.6 Delegated parser binding surface

Delegated parsing is not implemented by the DataSource plugin directly.
Instead, the host provides a parser-binding surface through the DataSource
runtime host.

Define one opaque parser-binding handle and these operations:

- `ensure_parser_binding(binding_request) -> parser_binding_handle`
- `push_raw_message(binding_handle, host_timestamp_ns, payload_bytes)`

`binding_request` must contain:

- topic name
- parser encoding key
- optional type name
- optional schema bytes
- host-owned parser config blob

Rules:

- bindings are scoped to the bound `data_source`
- the host creates the actual `MessageParser` instance for the corresponding
  `(data_source, topic)` pair
- the DataSource never instantiates parser plugins directly
- repeated binding requests for the same topic and same parser metadata are
  idempotent
- incompatible rebinding for an already-bound topic is an error
- `ensure_parser_binding` returns `false` on failure; details are available
  through `get_last_error()` on the runtime host

## 4. Config and Dialog Model

### 4.1 Persisted state envelope

Persist one host-owned JSON envelope per DataSource instance:

```json
{
  "version": 1,
  "source_config": { "... plugin-owned ..." },
  "parser_binding": { "... host-owned ..." }
}
```

Rules:

- `source_config` is produced and consumed only by the DataSource plugin through
  `save_config` / `load_config`
- `parser_binding` is produced and consumed only by the host runtime
- the DataSource plugin never interprets host-owned parser state
- parser configuration remains logically independent even when shown inside the
  same dialog as source-specific controls

### 4.2 Dialog ownership model

When a DataSource has configuration UI:

- the plugin supplies its source-specific dialog through the existing dialog
  protocol
- the host may enrich that dialog with host-owned parser controls for delegated
  sources

The required UX for delegated sources is one integrated dialog. Do not create a
second modal step just for parser configuration.

### 4.3 Parser UI injection

Standardize one reserved placeholder widget name:

- `pj_parser_slot`

If the source dialog contains a widget with that object name, the host renders
the parser selector/config panel there.

If the placeholder is absent, the host appends a default parser group at the end
of the dialog.

### 4.4 Supported delegated parser UI patterns in v1

Lock v1 to three patterns only:

- `global_selectable_parser`
  - one user-selected parser encoding for the whole source
  - examples: `UDP`, `Websocket`, `MQTT`, `ZMQ`
- `global_fixed_parser`
  - parser encoding fixed by the source
  - examples: `Foxglove`, `PlotJuggler Bridge`
- `per_topic_fixed_parser`
  - parser encoding determined per topic/channel from source metadata
  - example: `MCAP`

Do not support arbitrary per-topic user-selected parser encodings in v1.

### 4.5 Dialog-less start from restored config

The host start flow is:

1. create the DataSource instance
2. bind the already-existing source write host
3. bind the DataSource runtime host
4. load `source_config`
5. load the host-owned `parser_binding` state for delegated sources
6. if there is no UI or the host chooses a headless start path, call `start`
7. otherwise open the dialog, merge parser UI if needed, save the envelope, then
   call `start`

The runtime may skip the dialog when restored config is known-valid or when the
caller explicitly requests a headless restart.

## 5. Implementation Phases

### Phase 0: Shared data plane

Status: done

Completed:

- plugin data ABI in `pj_base`
- plugin data C++ wrappers in `pj_base`
- datastore-backed host adapters in `pj_datastore`
- unit tests for the shared data plane

No further work is planned here unless the DataSource runtime exposes a missing
requirement.

### Phase 1: DataSource control-plane ABI in `pj_base`

Status: next (prototype exists in `pj_plugins/data_source_protocol/`)

A working prototype of the DataSource ABI, C++ SDK wrapper, runtime host view,
host-side library loader, and mock plugin tests already exists under
`pj_plugins/data_source_protocol/`. That location is temporary.

This phase relocates the plugin-facing surface into `pj_base` and refines it:

- move the raw DataSource ABI header to `pj_base`
- move the plugin-side C++ wrapper / base class to `pj_base`
- move the DataSource runtime-host view to `pj_base`
- move corresponding unit tests
- keep host-side library loader and instance handle in `pj_plugins`
- remove `pj_plugins/data_source_protocol/` once relocation is complete
- review and refine the ABI against this plan during the move

Acceptance:

- a mock DataSource plugin can be instantiated and driven without Qt widgets or
  parser integration
- no exceptions cross the ABI
- the control plane reuses the existing `PJ_source_write_host_t` instead of
  inventing a new data API

### Phase 2: Package-facing SDK targets/components

Status: next

Implement:

- package-facing CMake targets/components for the core SDK, DataSource SDK, and
  dialog SDK
- dependency boundaries that keep `pj_datastore` and Abseil out of the external
  plugin SDK surface
- at least one out-of-tree example plugin build path in the test/validation
  story

Acceptance:

- an external DataSource plugin repository can consume the SDK through package
  targets/components
- a headless plugin does not need the dialog SDK
- a UI-capable plugin can add the dialog SDK without host-runtime code
- no accidental `pj_datastore` or Abseil dependency leaks into the plugin SDK

### Phase 3: Host-side library loading and runtime in `pj_plugins`

Implement in `pj_plugins`:

- shared-library loader for DataSource plugins
- RAII instance wrapper
- runtime host implementation for:
  - messages
  - progress
  - cancel check
  - state notifications
  - source-initiated stop requests
  - delegated parser binding

Acceptance:

- direct sources can write topics and fields through the bound source host
- delegated sources can bind parsers and push raw payloads
- host-mediated stop / failed transitions work for background-thread sources

### Phase 4: Dialog integration and parser UI injection

Extend the host dialog layer to:

- show the plugin-owned dialog
- render host-owned parser controls into `pj_parser_slot`
- persist the config envelope
- support dynamic dialog updates driven by `on_tick`

Acceptance:

- one integrated dialog works for both direct and delegated sources
- parser configuration is visibly host-owned but feels native in the same dialog
- restored config round-trips through the envelope without `QSettings`

### Phase 5: First migrations

Migration order is fixed:

1. `DataLoadCSV` or `DataLoadParquet`
2. `DataLoadMCAP`
3. `DataStreamSample`
4. `DataStreamUDP` or `DataStreamWebsocket`
5. `DataStreamMQTT` or `DataStreamZMQ`
6. `DataStreamPlotJugglerBridge`
7. `DataStreamFoxgloveBridge`
8. `DataLoadZcm` and `DataStreamZcm`
9. `DataLoadULog`

Selection rule:

- choose the smaller plugin when two options satisfy the same phase goal

### Phase 6: Legacy compatibility cleanup

After at least one direct and one delegated source are stable:

- remove remaining assumptions that a source mutates shared host containers
- replace old signal-driven host integration with the new runtime host surface
- move all persisted source state into the common save/load path

## 6. Migration Mapping

### Direct finite sources

- `CSV`
- `Parquet`
- `ULog`
- `ZCM log`

Required features:

- bound source write host from `pj_base`
- dialog protocol
- progress and cancel for large imports
- direct field creation and direct writes

### Delegated finite source

- `MCAP`

Required features:

- bound source write host
- integrated host-owned parser UI
- delegated parser binding per topic
- progress and cancel

### Direct continuous sources

- `Sample`
- `ZCM stream`

Required features:

- bound source write host
- start / stop lifecycle
- runtime message reporting

### Delegated continuous sources

- `UDP`
- `Websocket`
- `MQTT`
- `ZMQ`
- `Foxglove`
- `PlotJuggler Bridge`

Required features:

- integrated source + parser dialog
- delegated parser binding
- lazy topic creation
- runtime state transitions
- runtime messages
- optional pause / resume

## 7. Test Plan

### Shared-data regressions

- keep the existing `pj_base` and `pj_datastore` data-host tests green
- do not duplicate data-plane tests in the DataSource control-plane layer
- add only targeted coverage for the new control-plane integration points

### DataSource ABI and wrapper tests

- load a mock DataSource plugin
- save / load config round-trip
- state-machine transition coverage
- unsupported `pause` / `resume` returns a structured error, not a crash
- no exceptions cross the DataSource ABI

### External SDK consumption tests

- out-of-tree example plugin build using only the exported/package SDK
- headless DataSource example using the core SDK plus DataSource SDK only
- UI-capable DataSource example using the core SDK, DataSource SDK, and dialog
  SDK
- verify no accidental `pj_datastore` include/link dependency leaks into the SDK
- verify no Abseil dependency leaks into the SDK

### Direct-source runtime tests

- one source instance writes multiple topics into one bound `data_source`
- lazy topic creation works after `start`
- stopping a source does not clear already-written data
- headless start from restored config works without opening a dialog

### Delegated-source runtime tests

- `ensure_parser_binding` is idempotent for same topic and metadata
- incompatible rebinding for an existing topic fails cleanly
- pushing raw payloads routes through host-created parser instances
- one DataSource can feed multiple delegated topics concurrently

### Dialog integration tests

- direct source dialog works unchanged
- delegated source dialog renders parser controls in `pj_parser_slot`
- missing `pj_parser_slot` falls back to appended parser group
- dialog-driven topic discovery works with repeated `on_tick`
- source config and parser config persist independently inside the host envelope

### Migration acceptance tests

- `CSV` or `Parquet` migrated source reproduces old user-visible behavior
- `MCAP` migrated source supports topic selection plus delegated parsing
- `Sample` migrated source proves continuous direct runtime
- one simple delegated continuous source proves end-to-end raw payload routing

## Assumptions and Defaults

- the shared plugin data interface in `pj_base` and `pj_datastore` is fixed input
- Conan is the primary external SDK distribution path
- git submodule consumption remains supported as a secondary path
- `MessageParser` remains headless in v1
- parser configuration is host-owned and persisted outside plugin-owned
  `source_config`
- the existing dialog protocol is reused rather than replaced
- one integrated dialog is required for delegated sources
- reconfiguration while running is implemented as stop -> edit -> restart in v1
- stopping a DataSource does not implicitly delete or clear the bound
  `data_source`
- the ULog post-load auxiliary window is not treated as a required v1
  DataSource capability
