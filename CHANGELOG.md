# Changelog

All notable changes to `plotjuggler_sdk` are recorded here. Versioning policy is in
[`CLAUDE.md`](./CLAUDE.md) → "Release Versioning".

## [0.24.0]

### Feature: sandboxed wasm parser modules and authoring preset (MINOR)

Functional parser modules can now be validated, compiled once, and executed as
WASI reactors through the pinned Wasmer 7.0.1 C API:

- The wasm loader admits only reactors with the frozen operational signatures,
  `_initialize`, exactly one manifest section, bounded exported memory, no
  start function, and the v1 empty import allow-list.
- Store-per-instance execution copies ABI blocks through guest allocation,
  revalidates linear-memory ranges after every guest call, preserves host
  payload splice semantics, and classifies traps or metering exhaustion as
  contract violations.
- Instruction metering, declared-memory caps, and pure session admission
  budgets bound calls, artifact size, modules, claims, instances, and aggregate
  declared memory. Adversarial trap, infinite-loop, memory-growth, and
  quarantine-replay fixtures pin the failure behavior.
- The installed `pj-wasm-embed-manifest` frontend embeds or verifies exact
  manifest bytes and performs the shared static ABI audit.
- `pj_add_parser_module(... TARGETS wasm)` provides the wasi-sdk 27 C++17
  reactor preset, manifest embedding, and post-link audit; `TARGETS native wasm`
  emits both artifacts from one author source.

The wasm execution libraries remain optional when `PJ_WASMER_ROOT` is unset.

## [0.23.1]

### Fix: Conan `plugin_host` component links the parser-module host (PATCH)

The Conan recipe's `plugin_host` component omitted `pj_parser_module_host` and
`pj_parser_claim_catalog`, both part of the `plotjuggler_sdk::plugin_host`
umbrella since 0.22.0, so Conan consumers of the host umbrella could not link
the claim catalog, route resolver, or native parser-module loader. The recipe
also gains the `parser_module` component and ships `PjParserModule.cmake` as a
build module, so `find_package(plotjuggler_sdk COMPONENTS parser_module)` and
`pj_add_parser_module()` work from the Conan package exactly as from the
installed CMake package. No header, ABI, or behavior change.

Also silences a GCC 15 `-Wfree-nonheap-object` false positive in
`parser_module_abi.cpp` that made every `-O3 -Werror` (Release package) build
fail on that compiler; Debug and `-O2` builds were unaffected.

## [0.23.0]

### Fix: entry-point symbol provenance and modern platform loaders (MINOR)

Plugin admission now proves that the ABI marker and family vtable getter are
defined by the candidate DSO itself instead of accepting definitions from a
dependency. POSIX uses defining-object identity, macOS restricts handle-scoped
lookups to the first image, and Windows uses filesystem-native wide paths with
package-scoped dependency search and defining-module checks that reject
forwarded PE exports. Recorded normalized absolute load paths and their
best-effort symlink-resolved forms let deferred dialog-vtable provenance accept
either defining-path spelling without re-stating the candidate, so staged
deletion, later working-directory changes, and macOS dyld realpath reporting
are safe. Plugin install rpaths now resolve bundled dependencies relative to
the plugin on Linux and macOS.

Native functional parser modules now share the same absolute-path open and
symbol-provenance checks, including `RTLD_FIRST` on macOS. Their narrow load
API retains its explicit UTF-8 contract on Windows and rejects invalid UTF-8
before calling the platform loader.

New filesystem-path overloads and already-open-handle adoption APIs let hosts
validate, inspect, and instantiate a candidate through one native module open.
Static initializers now run once per admission instead of up to three times.

There is no C-ABI or protocol change: `PJ_ABI_VERSION`, all family protocol
versions, vtable layouts, and `abi/baseline.abi` remain unchanged. The release
is MINOR because the installed C++ host API gains additive overloads.

## [0.22.0]

### Feature: extensible parser routing and functional parser modules (MINOR)

Parser selection can now be described, resolved, and executed through stable,
additive SDK contracts:

- The new `pj.parser_route_claims.v1` extension reports exact scalar/object
  handler claims, while `pj.parser_functional.v2` adds the object splice sink
  without changing the frozen v1 declarations. `MessageParserHandle` negotiates
  v2 first, falls back to v1, reconstructs splices, and rejects objects whose
  type differs from the selected claim.
- The parser-module ABI defines lifecycle exports, little-endian binding/input/
  output codecs, canonical-object splice eligibility, native manifest metadata
  exports, and the frozen wasm manifest custom-section name and byte codec.
- The host claim catalog validates module manifests and synthesized plugin
  claims. Its deterministic resolver applies exact/wildcard, provenance,
  priority, pin, probe-cache, and fail-closed selection policy.
- The native loader and per-instance runtime validate exports, lifecycle
  results, descriptors, and splices, reconstruct spliced canonical objects, and
  expose contract-strike quarantine and session-disable state. Module tokens
  are synchronized index+generation values, so stale tokens fail with a
  diagnostic rather than aliasing a new instance.
- The standalone header-only C++17 authoring kit provides bounded CDR/protobuf
  readers and field locators, time normalization, and the complete native
  functional-module export wrapper. Parse callbacks receive the per-message
  `Timestamp`; CDR plans support bounded sequences/strings and string
  arrays/sequences while rejecting cyclic or over-depth schemas at bind.
  `ObjectWriter` covers all nine splice-eligible canonical types: Image,
  PointCloud, DepthImage, OccupancyGrid, CompressedPointCloud, Mesh3D,
  VideoFrame, OccupancyGridUpdate, and VoxelGrid. Bulk allocations are fallible
  under `-fno-exceptions` and report data errors.
- A shared wasm custom-section codec embeds exact manifest bytes. The wasi-sdk
  27 compile gate statically audits reactor exports and their frozen wasm
  signatures, manifest delivery, and absence of native-only metadata exports;
  wasm loading, execution, and `pj_add_parser_module(... TARGETS wasm)` are not
  part of this release.

All additions preserve the existing plugin ABI and protocol versions.

## [0.21.0]

### Fix: convenience registerService honors its documented assertion (PATCH)

The non-returning `ServiceRegistryBuilder::registerService` overloads documented
a debug assertion but discarded the `tryRegisterService` status in every build
type, so duplicate or null registrations were silent. A rejection now trips
`PJ_ASSERT`, and the doc-comments state the exact NDEBUG behavior (status
dropped; callers that must report failures use `tryRegisterService`). Adds the
first regression tests for the builder's registration rules
(`service_registry_builder_test`, built with `PJ_ASSERT_THROWS` so the
invariant is observable in every build type).

### Feature: pure-functional MessageParser C extension (MINOR)

MessageParser scalar/object results no longer require new hosts to cast an
opaque plugin context to `MessageParserPluginBase*` and call C++ methods across
the DSO boundary:

- New installed C header `pj_base/parser_functional_protocol.h` defines the
  stable `pj.parser_functional.v1` extension and appendable caller-owned scalar
  and object sink tables. Calls are synchronous and exact-once; borrowed scalar
  and canonical-wire views expire when their sink callback returns. All
  malformed tables, parser/sink exceptions, unknown object tags, and missing or
  duplicate sink calls fail closed through `PJ_error_t`.
- `MessageParserPluginBase` exposes the extension automatically only after at
  least one `SchemaHandler` is registered. A newly rebuilt legacy `parse()`-only
  plugin does not falsely advertise functional support; a handler registered
  during `bindSchema()` becomes visible to a later uncached query. Existing
  custom extension IDs remain available.
- Object input reuses `PJ_payload_t`: the extension consumes one ownership
  anchor and releases it exactly once on every path, preserving zero-copy input
  for an already-anchored `PayloadView`. Plugin-side `ObjectRecord`/`std::any`
  values are serialized to canonical bytes before returning; the host decodes
  them into allocations whose destructors and type-erasure managers are wholly
  host-owned. The already-frozen `PJ_payload_t` / `PJ_payload_anchor_t`
  declarations now live in shared `plugin_data_api.h` (and remain transitively
  available from `data_source_protocol.h`) so parser and data-source protocols
  do not import one another.
- `MessageParserHandle` adds `supportsFunctionalParsing()`,
  `parseScalarsFunctional()`, and bare-span/anchored
  `parseObjectFunctional()` overloads. Pre-0.21 plugins remain detectable by
  extension absence so PlotJuggler 0.21 can use its isolated deprecated bridge
  during migration; removal is deferred to SDK 1.0.
- New `serializeBuiltinObject()` / `deserializeBuiltinObject()` dispatch every
  stable builtin tag and accept zero-byte proto3 default messages when the tag
  is known. `RobotDescription` gains its missing canonical schema and codec.
- The built-in extension table and trampolines are DSO-local. This prevents ELF
  `STB_GNU_UNIQUE`/symbol interposition from sharing one inline table across
  plugins and leaving function pointers into the wrong or unloaded DSO.
- Tests pin the C layouts, all builtin dispatch routes, exact-once/error rules,
  anchor release, truthful legacy detection, and a real `dlopen`/`dlclose`
  lifecycle where the host-owned object remains valid after plugin unload.

This is additions-only for the unreleased 0.21 line. `PJ_ABI_VERSION` remains
5, `PJ_MESSAGE_PARSER_PROTOCOL_VERSION` remains 4, the family vtable and
minimum-vtable floor do not grow, and the existing ABI baseline is unchanged.

### Feature: dialog host-information tail slot (MINOR)

Dialogs can now observe the embedding host's SDK version, PlotJuggler version,
and runtime dialog capabilities through a backward-compatible optional vtable
tail slot:

- New C ABI types `PJ_dialog_host_capability_t` (width-pinned) and appendable
  `PJ_dialog_host_info_t`, whose string views are call-duration-only.
- Optional `PJ_dialog_vtable_t::set_host_info` after `manifest_json`.
  `PJ_DIALOG_PROTOCOL_VERSION` remains 4, `PJ_DIALOG_MIN_VTABLE_SIZE` remains
  pinned to the required prefix, and old vtables are accepted unchanged.
- `DialogHandle::setHostInfo()` owns the runtime `PJ_HAS_TAIL_SLOT` gate and
  returns an explicit `Unsupported`/`Accepted`/`Rejected` result. Unsupported
  slots are never called and leave the error output untouched; rejection may
  optionally carry a plugin error.
- `DialogPluginBase` copies host information, exposes protected `hostInfo()` and
  typed `DialogHostCapability` checks, accepts appendable partial host-info
  structs with zero/empty defaults, and uses last-writer-wins replacement after
  successful deliveries. Empty `hostInfo()` is the fallback signal for a
  pre-0.21 or non-conforming embedding host.

The generated SDK version header and `PJ_SDK_HAS_DIALOG_HOST_INFO` feature-test
macro are deferred to their separate 0.21 package; no `PJ_SDK_HAS_*` macro is
introduced here. PlotJuggler host wiring also lands separately.

### Feature: QStackedWidget dialog SDK binding (MINOR)

Dialogs can now describe and observe `QStackedWidget` state through additive
JSON, without changing the dialog C ABI, `PJ_dialog_vtable_t`, or protocol
version:

- `WidgetData::setStackedPage()` and `setStackedIndex()` emit `stacked_page`
  and `stacked_index`; the direct child page's Qt `objectName` is the stable
  identity and wins when both keys are present. Invalid empty/unknown names and
  negative/out-of-range indexes are host warning/no-ops.
- `WidgetDataView` exposes both optional representations independently and
  rejects integer values outside `INT_MIN..INT_MAX` without narrowing.
- The host-side `WidgetEventBuilder::stackedPageChanged()` always emits both
  representations; `WidgetEvent` parses either key defensively, and
  `DialogPluginTyped::onStackedPageChanged()` dispatches the complete pair once;
  negative indexes and empty page names fail closed.

This package is the SDK surface only; the Qt application, signal blocking, and
signal connection land in the companion PlotJuggler host package. No
`PJ_SDK_HAS_*` macro is introduced here.

### Feature: QTreeWidget dialog SDK binding (MINOR)

Dialogs can now publish and observe identity-stable hierarchical trees through
additive JSON, without changing the dialog C ABI, C vtable, protocol version, or
introducing a `PJ_SDK_HAS_*` macro:

- New `TreeCheckState`, `TreeCell`, and `TreeItem` SDK types. Items use stable
  plugin-owned string IDs in a flat `id` / `parent_id` snapshot; array order is
  preserved as unsorted sibling order. Cells carry display text, optional typed
  `NumericValue` sort keys, tooltip, and a host semantic icon name. V1 check
  state applies to column 0.
- `WidgetData` setters cover headers, complete snapshots, logical selected and
  expanded IDs, tri-state ID visibility (array filter, empty zero-match filter,
  JSON-null reset), and single/multi selection mode. Every state channel is an
  independent additive update.
- `WidgetDataView::treeItems()` validates a complete snapshot before returning
  it and rejects empty/duplicate IDs, missing parents, self/longer cycles,
  malformed cells/sort keys, and invalid check-state strings atomically. It
  follows the strict table-delta `nullopt` rejection precedent and adds an
  optional validation-error output for host diagnostics. Unknown state IDs are
  left for host-side pruning. `TreeVisibilityUpdate` preserves all three states.
  Tree header/ID arrays also reject their whole channel on any non-string member,
  so malformed input cannot be misread as a destructive empty update.
- Four distinct host event objects cover complete logical selection, item
  activation, expansion, and check-state changes. `DialogPluginTyped` appends
  the matching virtual callbacks after the stacked callback and claims tree
  keys before all older channels: exactly one well-formed tree event dispatches;
  malformed or mixed tree payloads fail closed without fallthrough. Activation
  columns are decoded losslessly and accepted only in `0..INT_MAX`.
- `may_have_children` supports host-private lazy placeholders followed by a
  later complete snapshot. Tree deltas remain deferred; a future delta must
  reuse the table protocol's fresh sequence and all-or-nothing rules.

This package is the Qt-free SDK surface only. QTreeWidget reconciliation,
placeholders, signal blocking, selection preservation, ancestor-closure
filtering, and typed sorting land in the companion PlotJuggler host package.

### Feature: structured file-picker dialog SDK binding (MINOR)

Dialogs can now request and observe structured single/multiple-open, save, and
directory pickers through additive JSON, without changing the dialog C ABI,
protocol version, or introducing a `PJ_SDK_HAS_*` macro:

- New `FilePickerMode`, `FilePickerFilter`, `FilePickerOptions`,
  `FilePickerStatus`, and `FilePickerResult` types with documented canonical
  wire strings. Stable filter IDs survive localized or normalized native filter
  text.
- `WidgetData::setFilePicker(..., FilePickerOptions)` emits the complete
  structured object plus the closest legacy action, title, Qt filter string,
  and save suffix. Multiple-open deliberately degrades to legacy single-open;
  directory mode degrades to the legacy folder action. Existing picker
  overloads remain unchanged.
- The writer preserves caller input, following tree/stacked precedent;
  `WidgetDataView::filePickerOptions()` atomically rejects invalid modes,
  empty/duplicate filter IDs, missing selected IDs, empty patterns, and
  malformed fields while legacy accessors continue to read the co-serialized
  fields.
- Structured result events carry their canonical request mode and preserve
  every selected path, browser display name, selected filter ID, and error.
  Status, mode, and paths are required; absent display names, selected filter
  ID, and error default to empty. Fully valid Selected results co-emit the first
  mode-correct legacy path; malformed Selected, cancelled, unsupported, and
  error results do not. A binary fixture
  verifies the previous dispatcher sees selected compatibility keys and
  remains silent for other statuses.
- `DialogPluginTyped::onFilePickerResult()` is appended after the tree
  callbacks. The structured key is authoritative: its legacy co-key is
  optional, a present co-key must match both the result mode and first path,
  and both legacy keys are forbidden. Unknown additive keys are tolerated.
  Malformed or mismatched events fail closed. The normative cross-channel
  priority is documented in `docs/dialog-sdk-reference.md`. The default
  handler routes from result mode and forwards the first selected path exactly
  once to the matching legacy callback; an override receives only the
  structured callback.

The shared picker controller, staged-file lease ownership, and browser/WASM
host implementation remain companion PlotJuggler work.

### Example: canonical SDK 0.21 dialog controls

The installed-SDK consumer now builds one Qt-free `DialogPluginTyped` example
that composes the 0.21 dialog additions: graceful host-info fallback,
object-name-keyed stacked pages, a stable-ID topic tree with independent
visibility filtering and lazy snapshot publication, and a capability-gated
structured multi-file picker with distinct selected/cancelled/unsupported
handling. Protocol-level tests drive the example through `DialogHandle`,
`WidgetDataView`, and `WidgetEventBuilder`, including static-manifest and tree
validation checks.
### Feature: canonical object-topic renderer metadata (MINOR)

- Added `PJ::sdk::ObjectTopicMetadataBuilder`. Its typed
  `builtinObjectType()` method emits the canonical `builtin_object_type` key
  using the exact `PJ::sdk::name()` value, while custom string metadata is
  escaped and emitted in deterministic key order. `build()` returns
  `Expected<std::string>` so invalid/reserved types and attempts to insert the
  canonical key as custom metadata remain errors even when assertions are
  disabled.
- Added typed C++ registration overloads on `SourceObjectWriteHostView` and
  `ToolboxHostView` (including existing-dataset registration). Existing raw
  JSON overloads remain source-compatible—including calls with `{}`—and
  invalid typed metadata is returned without calling the raw host slot. No C
  ABI struct, vtable, or protocol changed.
- Clarified that `MediaMetadataBuilder::mediaClass()` supplies supplemental
  media metadata and does not select a canonical built-in renderer.

### Feature: message-parser runtime diagnostics service (MINOR)

MessageParser plugins can now acquire the optional
`"pj.parser_runtime.v1"` service during `bind()` and report recoverable or
aggregated parse conditions without failing the current message. The new
appendable `PJ_parser_runtime_host_vtable_t` carries severity, a machine-stable
deduplication code, representative text, and an occurrence count. Existing
family vtables, protocol versions, and minimum-vtable-size constants are
unchanged.

- `ParserRuntimeHostView::reportDiagnostic()` is a safe no-op when the service
  is absent; `MessageParserPluginBase` exposes `parserRuntimeHost()` and
  `parserRuntimeHostBound()` to derived parsers.
- `ParserRuntimeHost` adapts the C service to an embedder-provided
  `ParserDiagnosticSink`, and `testing::ParserRuntimeRecorder` captures owning
  diagnostic records in parser unit tests.
- Parser diagnostics remain distinct from fatal `Status` failures, and
  `classifySchema()` remains side-effect free.

### Feature: SDK version and manifest compatibility foundation (MINOR)

SDK releases now expose a single version identity and shared compatibility
vocabulary for package consumers and plugin manifests.

- The root `VERSION` file is the single source of truth for CMake, Conan, and
  release packaging.
- The generated and installed `pj_base/sdk/version.hpp` header provides
  `PJ_SDK_VERSION_MAJOR`, `PJ_SDK_VERSION_MINOR`, `PJ_SDK_VERSION_PATCH`,
  `PJ_SDK_VERSION_AT_LEAST`, and `PJ::sdkVersion()`.
- The Qt-free `PJ::SemVer` utility strictly parses concrete SemVer 2.0.0
  versions and compares their precedence; build metadata does not affect
  precedence, and equality intentionally follows precedence equivalence.
- Plugin manifests may declare the optional `min_sdk_required` SDK contract
  floor. Missing or empty values remain undeclared, while malformed values
  invalidate the manifest. This field is distinct from the advisory
  `min_plotjuggler_version` application-release expectation.

No C ABI struct, vtable, ABI version, or plugin protocol version changes.

## [0.20.0]

### Feature: descriptor import v1 — import a persisted source descriptor, promote the materialized artifact (MINOR)

A provider plugin (any family) can now advertise "pj.descriptor_import.v1"
through the existing `get_plugin_extension` hook, and a host can offer the
optional "pj.source_promotion.v1" source-promotion service through the
`bind()` registry — zero new family-vtable slots, no capability bit (presence
= capability). This is the SDK half of a canonical-layout-replay design: a
layout stores an opaque provider descriptor; on load the host queries the
provider (trust + identity + planned artifact path + `estimated_bytes`),
optionally starts an import job, and the provider asks the host to promote
its materialized artifact to a stock file-backed source.

- New family-neutral installed C header `pj_base/descriptor_import_protocol.h`:
  `PJ_descriptor_import_provider_v1_t` with `query_descriptor` (sync, strictly
  bounded — no network; always returns provider `source_identity` + planned
  `local_path_utf8`; `estimated_bytes`, 0 = unknown) and `start_import` taking
  a caller-sized `PJ_descriptor_import_start_request_v1_t{descriptor_json,
  flags, max_transfer_bytes}` (v1 flags mask = 0 — unknown bits fail closed)
  with exactly two serialized callbacks: `on_dataset` (zero-or-one, precedes
  the dataset's progress/publication/promotion) and `on_terminal`
  (exactly-once, last: SUCCEEDED_PROMOTED / SUCCEEDED_EAGER_ONLY / FAILED /
  CANCELLED), returning a joinable-job fat pointer (cancel / join / destroy).
  The promotion request carries provider-supplied `loader_plugin_id` +
  `loader_config_json` so a non-MCAP artifact promotes through its own
  companion loader; the service is bound per plugin instance so the host
  derives the provider identity itself. Every new appendable struct is
  struct_size-versioned under an explicit growth contract (owner
  zero-initializes, peer touches only fields wholly covered); the two
  fat-pointer handles are deliberately ABI-frozen; enums are
  FORCE_INT32-pinned with fail-closed unknowns.
- C++ wrappers in `pj_base/sdk/descriptor_import.hpp`:
  `DescriptorImportProviderView` (typed extension consumer, fail-closed enum
  mapping), RAII `JoinableJob` (owns the callback closures; destroy-before-
  release ordering; refuses ABI-violating job handles leak-over-UAF),
  `SourcePromotionHostView::promoteToFileSource()` +
  `PJ::sdk::SourcePromotionHostService` trait.
- Generic dataset-ingest lifecycle: `DatasetIngestHostView` (progress
  start/update/finish, cooperative stop, report, parser access) obtained via
  new `ToolboxRuntimeHostView::createDatasetIngest()` /
  `releaseDatasetIngest()` — C++ aliases over the EXISTING
  `create_parser_ingest`/`release_parser_ingest` slots — and
  `DataSourceRuntimeHostView::datasetIngest()`. This makes the dataset-scoped
  lifecycle canonical for both delegated parsing and direct toolbox writes
  (previously `ParserIngestHostView` hid it and direct Arrow writers could not
  reach the progressive-import surface).
- ABI-layout sentinels now pin every new struct (the first pins for extension
  structs).

No vtable grows anywhere: `PJ_ABI_VERSION` (5), every `PJ_*_PROTOCOL_VERSION`,
every `PJ_*_MIN_VTABLE_SIZE`, and `abi/baseline.abi` unchanged (additions
only: one new installed C header, header-only C++ additions, and out-of-line
`ToolboxRuntimeHostView` methods).

## [0.19.0]

### Fix: static plugin exports support namespaced classes (PATCH-level)

The static variants of the plugin export macros formed their getter symbol by
token-pasting the C++ class argument. Qualified names such as
`mosaico::MosaicoToolbox` therefore compiled as dynamic plugins but failed when
`PJ_STATIC_PLUGINS` was enabled. Each plugin family now has a backward-compatible
`*_PLUGIN_NAMED(ClassName, SymbolName, manifest)` form: `ClassName` may be
qualified, while `SymbolName` is the unqualified token used for the static getter.
Existing macros and getter names are unchanged for unqualified classes.

### Feature: exported manifest decoder — one validation policy for DSO and static plugins (MINOR)

`decodeManifest(source_path, family, manifest_json)` is now part of the
`pj_plugins/host/plugin_catalog.hpp` API instead of being private to DSO
discovery. A host that registers statically linked plugins (no DSO scan) can
decode their embedded manifests through the exact validation the DSO path
applies — required non-empty `id`/`name`/`version`, typed-field rejection, and
the message-parser `encoding` requirement — instead of re-implementing a
second, weaker decoder. Additive; no ABI or protocol change.

## [0.18.0]

### Feature: typed table sort keys — numeric columns sort numerically (MINOR)

A table cell crossed the dialog protocol as text only, so the host could compare
nothing but the rendered string and every numeric column sorted
lexicographically (`720` before `7` before `65`). Sort keys now travel beside the
display text. Backward-compatible JSON additions — no C ABI change,
`PJ_DIALOG_PROTOCOL_VERSION` unchanged, `abi/baseline.abi` unchanged:

- `PJ::TableItem { std::string text; std::optional<NumericValue> value; }` — the
  display string and the ordering truth. Constructors cover a text cell, a
  numeric cell (`std::to_string` rendering), and a numeric cell with plugin-owned
  rendering (`TableItem(v, "5.60536e+08")`), which also expresses a *hidden* key:
  display a date, sort on `int64` nanoseconds. `NumericValue` keeps the native
  width, so `int64`/`uint64` values past 2⁵³ round-trip exactly rather than
  degrading through a `double`.
- `WidgetData::setTableRows(name, vector<vector<TableItem>>)` — an overload beside
  the string one. Emits display text as the usual `rows` plus a **sparse**
  `column_values` map (`{"<col>": [v0, v1, …]}`): only columns where some cell has
  a value, with valueless cells as `null`, and the key omitted entirely when no
  cell has one. Old hosts never look for it and read `rows` exactly as before.
  Deriving both keys from one `TableItem` matrix means display and value cannot
  desync.
- `WidgetData::setTableSortIndicator(name, column, ascending)` → `sort_indicator`
  — draws the header arrow for a table that sorts itself via `onHeaderClicked`,
  which Qt otherwise leaves unpainted because its own sorting is off. Cosmetic
  only.
- `WidgetDataView::tableColumnValues(name)` / `tableSortIndicator(name)` for host
  implementations. `tableColumnValues` drops a column whose value count disagrees
  with `rows` or whose key is not a non-negative integer, rather than sorting some
  rows by number and the rest by text.
- `WidgetDataView::tableRows()` hardening: a non-string cell now yields an empty
  string instead of being skipped. Skipping shifted every later cell one column
  left and mis-aligned the row against its headers.
- The plain-string `setTableRows` overload now erases any `column_values` a
  previous typed delivery left on the same table, so alternating overloads can
  never pair fresh rows with stale sort keys.
- The batch table deltas (below) are sort-key aware: `appendTableRows` gains a
  `TableItem` overload emitting a sparse `append_values` column map aligned to
  the appended rows, and `TableCellUpdate` becomes `{row, col, TableItem}` —
  an update replaces the whole cell (text + optional key; a keyless item
  clears the key), serialized as `[row, col, text]` or
  `[row, col, text, value]`. `TableDeltaView` decodes both (strict: a
  malformed key or a misaligned `append_values` column rejects the whole
  delta), so a typed table stays typed while it streams.

### Feature: batch table deltas for large QTableWidgets (MINOR)

Mutate a table without resending the whole `rows` array (backward-compatible
JSON addition; no C ABI change, `PJ_DIALOG_PROTOCOL_VERSION` unchanged):

- `WidgetData::appendTableRows` / `updateTableCells` / `removeTableRows` write
  a per-widget `table_delta` object (`seq`, `append`, `append_values`,
  `update_cells`, `remove_rows`). `seq` is plugin-owned; hosts apply a delta only when its seq
  differs from the last one applied to that widget, in the order update_cells
  → remove_rows → append, with all indexes addressing the pre-delta table.
  New `TableCellUpdate` struct.
- `WidgetDataView::tableDelta()` returns the decoded `TableDeltaView` for host
  implementations (strict: a malformed op rejects the whole delta;
  `remove_rows` arrives descending and duplicate-free), with
  `tableDeltaSeq()` as the cheap staleness pre-check.
- `dialog-plugin-guide.md` gains a "Large tables" section documenting the
  omit-unchanged-fields pattern (with measured costs) and the delta ops.
- SDK-side only: hosts apply `table_delta` from the companion PlotJuggler
  change onward; older hosts ignore the key (harmless no-op).

### Feature: QDateTimeEdit event surface (MINOR)

The dialog protocol's QDateTimeEdit setters (`setDateTime` / `setDateTimeRange`,
shipped earlier) gain their missing event direction, so the widget becomes an
input, not just a display (backward-compatible JSON addition; no C ABI change,
`PJ_DIALOG_PROTOCOL_VERSION` unchanged):

- `WidgetEvent` key: `datetime_iso` (string — the edited datetime, wall-clock
  local ISO-8601; fractional seconds only for ms-precision editors), with
  `WidgetEventBuilder::dateTimeChanged()` on the host side,
  `WidgetEvent::dateTimeChanged()` on the plugin side, and typed dispatch via
  `DialogPluginTyped::onDateTimeChanged()`.
- Docs: `dialog-sdk-reference.md` gains the previously missing QDateTimeEdit
  section; the setter contract is clarified (empty/unparsable strings are
  ignored — the widget keeps its current value; `QDateEdit`/`QTimeEdit`
  subclasses share the binding).

### Fix: PJ_DIALOG_PLUGIN two-arg form broke under the MSVC legacy preprocessor (PATCH-level)

The overload-by-arg-count dispatch behind `PJ_DIALOG_PLUGIN(Class, kManifest)`
mis-expanded under MSVC's traditional preprocessor (the default without
`/Zc:preprocessor`): `__VA_ARGS__` was forwarded into the selector as a single
glued argument, so the two-arg call silently picked the one-arg legacy branch
with `"Class, kManifest"` as the class name (C2064/C2912 at the call site —
found via pj-official-plugins#230's Windows CI).

- `PJ_DIALOG_PLUGIN_EXPAND` rescan added to the dispatch — the macro now
  expands identically under both MSVC preprocessors and GCC/Clang. No ABI or
  call-site change; purely a header fix.
- `pj_dialog_sdk` no longer injects `INTERFACE /Zc:preprocessor` into
  consumers — the flag is unnecessary now, and it never reached Conan
  consumers anyway (CMakeDeps regenerates targets from `package_info()` and
  drops upstream `INTERFACE_COMPILE_OPTIONS`, which is how the breakage
  shipped unnoticed). Consumers that want the conformant preprocessor set it
  themselves.
- New compile-only regression target `mock_dialog_legacy_pp_plugin` builds the
  mock dialog with `/Zc:preprocessor-` on MSVC, so Windows CI now exercises
  the legacy-preprocessor path the SDK's conformant-only build never covered.

## [0.17.0]

### Feature: dialog-protocol additions for deletable lists and chart/list placeholders (MINOR)

Backward-compatible JSON protocol additions (unknown keys are ignored by old
hosts/plugins; no C ABI change, `PJ_DIALOG_PROTOCOL_VERSION` unchanged):

- `WidgetData` per-widget keys via `setListItemsDeletable` / `setListPlaceholder` /
  `setChartPlaceholder`: `list_deletable` (bool — rows grow a delete affordance), `list_placeholder` / `chart_placeholder` (string — centered
  hint shown while the list/chart is empty).
- `WidgetEvent` key: `item_delete_index` (int — row whose delete affordance
  was clicked), with `WidgetEventBuilder::itemDeleteRequested()` on the host
  side, `WidgetEvent::itemDeleteRequestedIndex()` on the plugin side, and
  typed dispatch via `DialogPluginTyped::onItemDeleteRequested()`.
- `WidgetDataView` accessors (`listDeletable` / `listPlaceholder` /
  `chartPlaceholder`) for host implementations.

## [0.16.2]

### Fix: 0.16.1's Apple `to_chars` guard tested a misspelled macro and never engaged (PATCH)

The 0.16.1 fallback guard checked `__ENVIRONMENT_MACOS_VERSION_MIN_REQUIRED__`,
which does not exist — the compiler defines
`__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__` (verified against clang's
`OSTargets.cpp`) — so `defined(...)` was always false and every Apple build
still compiled the floating-point `std::to_chars` path. The guard now uses the
correct macro and is fail-safe: any Apple target not provably macOS ≥ 13.3
(including non-macOS Apple platforms, which spell the macro differently) takes
the `snprintf` fallback. Guard behavior verified by preprocessing the header's
exact `#if` line under deployment targets 13.0 / 13.3 / 26.0 / undefined /
non-Apple.

## [0.16.1]

### Fix: double formatting in `plugin_data_api.hpp` on older Apple deployment targets (PATCH)

`SettingsStoreView::setValue(key, double)` — an inline method in an installed
public header — used the floating-point `std::to_chars` overload, which libc++
marks *unavailable* below a macOS 13.3 deployment target. Any macOS build with
an older target (Conan Center's build farm, or a consumer setting
`CMAKE_OSX_DEPLOYMENT_TARGET`) failed to compile. On such targets the method
now falls back to `snprintf("%.17g")`, which round-trips any IEEE double; all
other platforms keep the shortest-round-trip `std::to_chars` path. Behavioral
difference on the fallback path only: non-minimal digit strings (e.g. `0.1` →
`0.10000000000000001`) — values are preserved exactly.

Also seeds the Conan Center recipe at `0.16.1` (its `tool_requires
cmake/[>=3.22]` fix from `618b92e` plus this header fix are both required for
CCI's macOS builders).

## [0.16.0]

### SDK slimming: duplicate-resolution catalog moved to the host (HOST-FACING REMOVAL)

`PluginRuntimeCatalog` — the layer that resolved duplicate plugin ids across scan
folders by authoritative → compatibility → version → folder priority — is host
*policy*, not a plugin-facing mechanism, so it moved to the PlotJuggler app
(`pj_runtime`). The SDK stays a thin discovery + loader layer: `scanPluginDsos`,
`inspectPluginDso`, the RAII loaders, and the C ABI are unchanged. (#144)

- **Removed** `pj_plugins/host/plugin_runtime_catalog.hpp` and the installed
  `pj_plugin_runtime_catalog` library (dropped from the `pj_plugin_host`
  umbrella and the install set).

**Versioning note.** No plugin links `PluginRuntimeCatalog` (it was host-side
only) and `abi/baseline.abi` is unchanged, so by the plugin-impact rule this is
a MINOR, not a MAJOR. It does remove host-facing public API: a *host* built
against 0.15.0's catalog must adopt the app-side implementation (`pj_runtime`).

## [0.15.0]

### DataSource: per-topic pause/resume — advertise-without-subscribe + demand-driven control (ADDITIVE)

Lets a streaming source expose *all* its topics cheaply while only transmitting data
for topics the host is actually displaying. Strictly additive (tail slot + plugin
extension, both `struct_size`/`PJ_HAS_TAIL_SLOT`-gated); every existing plugin keeps
working with no recompile, and a new plugin degrades gracefully on an old host.

- **New capability** `PJ_DATA_SOURCE_CAPABILITY_PER_TOPIC_PAUSE = 1 << 6` (C++ mirror
  `kCapabilityPerTopicPause`).
- **Plugin → host advertise**: new runtime-host tail slot
  `notify_available_topics(topics, count)` carrying `PJ_available_topic_t`
  (`topic_name`/`parser_encoding`/`type_name`/`schema`, mirroring
  `PJ_parser_binding_request_t`), so the host can list and a-priori classify topics
  (via `classify_schema`) before any data flows. C++ helper
  `DataSourceRuntimeHostView::notifyAvailableTopics(Span<const AvailableTopic>)`.
- **Host → plugin control**: new `get_plugin_extension("pj.topic_subscription.v1")`
  extension `PJ_topic_subscription_v1_t::set_active_topics(names, count)` — declarative
  full active-set; the plugin diffs and subscribes/unsubscribes. Host wrapper
  `DataSourceHandle::setActiveTopics(...)` (no-op when the extension is absent).
- Runtime-host vtable size grows 96 → 104 (`notify_available_topics` at offset 96);
  `PJ_ABI_VERSION` (5), `PJ_DATA_SOURCE_PROTOCOL_VERSION` (4), and
  `PJ_DATA_SOURCE_MIN_VTABLE_SIZE` (128) unchanged. `abi/baseline.abi` unchanged
  (additions only).

## [0.14.0]

### Host service: markers + transforms unified into `pj.data_processors.v1`

The two whole-series host-driven services were collapsed into ONE contract — Pablo's
`pj.data_processors.v1` — with a string `kind` discriminator, so a plugin chooses
`markers`/`transform` (and future engines) from the **same** `create` call:

- **Removed** `PJ_markers_host_vtable_t` / `MarkersHostService` (the old `pj.markers.v1`,
  which never left this branch) and the separate `PJ_generators_host_vtable_t` /
  `GeneratorsHostService` (`pj.generators.v1`) that briefly carried the markers backend.
- **Generalized** `pj.data_processors.v1` `create_data_processor` to the unified shape:
  added `kind` (`"transform"` → DerivedEngine timeseries; `"markers"` → ObjectStore
  PlotMarkers), `language`, a `flags` bitset with `PJ_DATA_PROCESSOR_FLAG_EPHEMERAL`,
  and an `out_topics` resolved-name return. The separate `create_data_processor_ephemeral`
  slot is **removed** — ephemeral preview is now the EPHEMERAL flag on `create`.
- **Generalized** `validate_data_processor_script` to take a `kind` argument.
- `DataProcessorsHostView` gains the unified `create(kind, …)` plus thin convenience
  shims `createTransform` / `createEphemeralTransform` / `createMarkers`; the
  `createTransform`/`createEphemeralTransform`/`remove`/`list`/`recipeOf` signatures are
  unchanged, so existing transform call-sites are unaffected.

**Migration for `pj.data_processors.v1` consumers (Pablo's transform-editor work).** The
ABI `create_data_processor` and `validate_data_processor_script` slot signatures changed,
so a host that IMPLEMENTS the service (e.g. `DataProcessorsRuntimeHost`) must update its
vtable fill; callers using `DataProcessorsHostView::createTransform` keep working via the
shim, while `validateScript` call-sites gain a leading `kind` argument (`"transform"`).

**Versioning note.** This was an ABI/API break to a service that had merged to `main`.
It shipped as `0.14.0` rather than `1.0.0` because no public tag had ever carried
`pj.data_processors.v1`, so no released plugin was broken by the change.
