# PlotJuggler New Plugin Interface Requirements

## 1. Purpose and Scope

This document defines the requirements for the new PlotJuggler plugin interfaces.

It is normative for plugin-facing runtime contracts. It is not an execution plan,
not an SDK design document, and not a deployment/distribution specification.

Legacy PlotJuggler plugins are useful references to understand the required behavior, but they are not normative for the new system.

The plugin families in scope are:

- **DataSource**: unified source family covering both finite imports and continuous/live sources.
- **MessageParser**: independent plugins that convert raw payloads plus metadata into writes into the host.
- **Toolbox**: stateful interactive tools that can read existing data and write new data.

The following are explicitly out of scope for this document:

- `StatePublisher`
- native `Transform` plugin ABI
- plugin discovery, installation, marketplace, or exact SDK publishing workflow
- detailed datastore/data-model semantics that belong in `pj_datamodel`

## 2. Common Plugin Requirements

### 2.1 Stable Portable Boundary

- All new plugins must be loadable without recompiling PlotJuggler.
- Binary compatibility must not depend on using the same compiler, C++ standard library, or Qt ABI as the host.
- The plugin boundary must be language-neutral and ABI-safe.
- No C++ classes, STL containers, Qt objects, `QObject*`, `QWidget*`, Qt signals/slots, or compiler-specific ABI details may cross the boundary.
- No C++ exceptions may cross the plugin boundary.
- Plugins must not receive direct references or pointers to internal host data structures such as `PlotDataMapRef` or concrete datastore classes.

### 2.2 Instance Isolation

- Multiple independent instances of the same plugin type must be supported concurrently.
- Configuration, runtime state, and lifecycle must be instance-local.
- Hidden mutable global state must not be required for correctness.

### 2.3 Common Persistence Contract

- All plugin families must support the same save/load interface.
- The plugin decides what state/configuration it persists.
- The host must be able to restore plugin state deterministically from that interface alone.
- Hidden ambient configuration sources such as `QSettings` must not be required for correct restore.
- The serialized representation is not fixed by this document.

### 2.4 Common Host Interaction Rule

- Legacy Qt signal-based communication must not cross the new plugin boundary.
- Plugins may interact with the host only through explicitly defined plugin interfaces.
- Families that require runtime/lifecycle interaction with the host must use host-defined interaction surfaces rather than ad hoc callbacks or Qt-specific mechanisms.
- `MessageParser` remains headless and request/response in v1. This document does not require a general runtime event surface for it.

### 2.5 Common UI Rule

- Whenever a plugin family exposes UI, it must use the common host-rendered dialog protocol.
- Native widget injection is not allowed.
- Returning or exchanging `QWidget*` across the plugin boundary is not allowed.

### 2.6 Common Write Contract

- `DataSource`, `MessageParser`, and `Toolbox` must share the same logical write contract.
- The host write interface must support two first-class write styles:
  - incremental logical writes
  - bulk writes via serialized Arrow IPC
- Plugins may use whichever write style is appropriate for their workload.
- Plugins write through that contract; they do not mutate host-owned data structures directly.
- The exact host data model and datastore semantics are defined elsewhere.

### 2.7 External SDK Consumption

- The new plugin interfaces must support third-party plugin development in a
  repository independent from PlotJuggler.
- The primary supported SDK consumption model is a package, with Conan as the
  intended distribution path.
- Git submodule or vendored-source consumption is allowed as an alternative
  development path.
- A plugin author must be able to build against the plugin SDK without depending
  on `pj_datastore`.
- A plugin author must be able to build against the plugin SDK without depending
  on Abseil.
- The packaged SDK surface must contain the full plugin-facing ABI and the C++
  wrappers needed by plugin authors.
- Host-only runtime implementations, datastore adapters, and Qt host
  integration are not part of the external plugin SDK.
- The SDK should be componentized so headless plugin families do not require the
  dialog/UI SDK unless they actually use it.

## 3. Shared Host Interaction Surfaces

This section defines plugin-facing capabilities only. Detailed data semantics belong in `pj_datamodel`.

### 3.1 Write Surface

- The write surface is shared by `DataSource`, `MessageParser`, and `Toolbox`.
- It must support both incremental writes and bulk Arrow IPC writes.
- Family-specific permissions differ, but the underlying write contract is the same.

### 3.2 Read Surface

- Only `Toolbox` requires read access.
- The read surface is logical only. Bulk Arrow read/export is not required for plugins.
- The read surface must support:
  - enumeration of the available data
  - reading one individual field timeseries at a time
- Materialization/decompression is acceptable when reading actual sample data.
- The exact query/view shapes are defined elsewhere.

### 3.3 UI Surface

- UI-capable plugin families use the same host-rendered dialog protocol.
- UI state must integrate with the common save/load interface.
- The dialog protocol is part of the external plugin SDK for UI-capable plugin
  families.
- In delegated-ingestion workflows, the host may embed parser selection and parser
  configuration controls inside a `DataSource` dialog.
- Those parser controls are host-owned, not parser-owned, even when they appear in
  the same integrated dialog as source-specific controls.

## 4. Family-Specific Requirements

### 4.1 DataSource

- `DataSource` is the unified replacement for the old `DataLoader` and `DataStreamer` split.
- One `DataSource` instance corresponds directly to one application-level `data_source`.
- A `DataSource` may be finite or continuous.
- A `DataSource` may be file-based or connection-based.
- Atomic import is not required. Finite sources may ingest progressively.
- Continuous sources may require stop/pause/resume semantics.
- A `DataSource` is write-only. It has no read/query access to existing host data.
- A `DataSource` may write multiple topics within its associated `data_source`.
- Topics may appear lazily at runtime. The host must support lazy creation of topics, and when delegated parsing is used, the corresponding parser instances.
- A `DataSource` may either:
  - decode data directly and write through the common write contract
  - hand raw payloads plus metadata to a `MessageParser`
- This document does not define whether one specific topic may mix direct and delegated ingestion modes.
- When UI is needed, it must use the common dialog protocol.
- The `DataSource` family ABI and plugin-side wrappers are part of the external
  plugin SDK.

### 4.2 MessageParser

- `MessageParser` is a first-class independent plugin family.
- One `MessageParser` instance corresponds to exactly one `(data_source, topic)` pair.
- A `MessageParser` is write-only. It has no read/query access to existing host data.
- A `MessageParser` is headless in v1.
- Its purpose is to convert raw payloads plus metadata into writes through the common write contract.
- The host must be able to instantiate and invoke a `MessageParser` independently of any particular `DataSource` implementation.
- Parser configuration remains first-class and host-managed even though parser
  plugins are headless in v1.
- Parser configuration remains logically independent from `DataSource`
  configuration even when delegated-source dialogs present both in one integrated
  UI.
- Parser instances may maintain per-instance runtime state across messages.
- A `MessageParser` may extend the set of fields within its bound topic as needed, subject to the host data model defined elsewhere.
- `MessageParser` configuration is independent of `DataSource` configuration.
- `MessageParser` configuration/state must use the common save/load interface.

### 4.3 Toolbox

- `Toolbox` is a separate plugin family with the same portable boundary constraints as the others.
- A `Toolbox` is stateful and long-lived.
- `Toolbox` state must round-trip through the common save/load interface.
- A `Toolbox` has full read access to host data through the logical read surface.
- `Toolbox` reads are on-demand. Reactive subscription/live callback semantics are not required.
- A `Toolbox` writes through the same common write contract used by `DataSource` and `MessageParser`.
- A `Toolbox` may create new `data_source` objects.
- A `Toolbox` may write into existing `data_source` objects as well as new ones it creates.
- A `Toolbox` may read and write within the same operation.
- A `Toolbox` may perform destructive updates such as delete/replace. The exact granularity is intentionally left unspecified here.
- When UI is needed, it must use the common dialog protocol.

## 5. Permission Summary

| Family | Read existing data | Write data | Create new `data_source` | Destructive updates | UI |
|---|---|---|---|---|---|
| `DataSource` | No | Yes | No; each instance is bound to one `data_source` | No | Optional |
| `MessageParser` | No | Yes | No | No | No in v1 |
| `Toolbox` | Yes | Yes | Yes | Yes | Yes |

## 6. Explicit Non-Goals / Out of Scope

- Plugin discovery, installation, marketplace, and exact Conan recipe/publishing
  policy.
- Exact ABI versioning scheme and loader policy.
- Exact runtime event taxonomy or event transport details.
- Backpressure and overflow policy.
- Detailed host data model semantics such as ordering, null behavior, field naming/path conventions, schema evolution rules, and DAG execution semantics. These belong in `pj_datamodel`.
- Recreating the legacy plugin boundary based on `QObject`, `QWidget*`, `PlotDataMapRef`, or direct XML/Qt host coupling.
- `StatePublisher`.
- Native `Transform` plugin ABI.

## 7. Deferred to Other Documents

The following are intentionally deferred to other documents, especially `pj_datamodel`:

- the exact semantics of `data_source`, `topic`, and `field`
- field naming and path conventions
- timestamp ordering rules and duplicate-timestamp semantics
- null/absent semantics and type rules
- schema evolution rules
- datastore read/write consistency guarantees
- derived-function and DAG semantics
