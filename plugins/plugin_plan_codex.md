# PlotJuggler Portable Plugin System — Codex Plan (v3)

## 1. Executive Summary

This document is an improved architecture and migration plan based on:

- `plugins/plugin-plan.md` (original proposal)
- `plugins/claude_review.md`
- `plugins/codex_review.md`

Core direction is preserved:

- Use Arrow as the data boundary.
- Use C ABI for compute plugins (parsers/transforms).
- Move parser dispatch to the host.
- Reduce loader/streamer architectural duplication.

Key corrections in this v3:

1. **Handle-based C ABI** (P0 blocker fix).
2. **Explicit discovery/versioning policy** for marketplace-grade loading.
3. **Timestamp + metadata contracts** clarified.
4. **Hard test/performance gates** between phases.
5. **Compute-first rollout** (DataSource dialog rewrite is deferred/non-blocking).

---

## 2. Goals and Non-Goals

### 2.1 Goals

1. Enable binary plugin distribution via marketplace with safe host loading.
2. Allow parser/transform plugins to be compiler-agnostic and language-agnostic.
3. Decouple transport/container plugins from message decoding plugins.
4. Keep migration practical for existing built-in and third-party plugins.

### 2.2 Non-Goals (early phases)

1. Rewriting all DataSource dialogs into `.ui + JSON` immediately.
2. Removing all legacy plugin interfaces in one release.
3. Solving every plugin type in phase 1 (focus first on parser/transform pipeline).

---

## 3. Priority Findings (What Must Be Addressed)

## P0 — Design blockers

1. **C ABI must use instance handles.**
2. **Discovery/versioning policy must be explicit** (search paths, symbol probing, compatibility).

## P1 — High-risk migration issues

1. Timestamp authority rules are underspecified.
2. Metadata (`user_defined`, non-timeseries payload) has no defined boundary path.
3. Transform roundtrip cost can regress performance at scale without batching/delta policy.
4. Config precedence (`layout config` vs `QSettings` vs defaults) is undefined.

## P2 — Important completeness

1. Migration inventory must include `PluginsZcm` and `VideoViewer`.
2. Custom widgets are broader than `QCodeEditor` (`LineEdit` also used).
3. Post-load UI flows (for example ULog parameters dialog) need lifecycle hooks.

---

## 4. Target Architecture

## 4.1 Plugin Families

1. **Parser plugin (C ABI, Arrow)**
   - Input: raw bytes + context.
   - Output: Arrow batch + metadata sidecar.

2. **Transform plugin (C ABI, Arrow)**
   - Input: Arrow batch.
   - Output: Arrow batch.

3. **DataSource plugin (Qt C++)**
   - Produces direct Arrow batches or delegated raw messages.
   - In early phases, can keep native `QWidget` dialogs.

4. **StatePublisher / Toolbox**
   - Out of scope for early ABI migration.

## 4.2 DataSource Modes

1. **Direct mode**: source emits Arrow directly.
2. **Delegated mode**: source emits raw messages + message context; host dispatches parser.
3. **Both**: supported when source can operate either way.

---

## 5. C ABI v1.0 (Handle-Based)

## 5.1 ABI Versioning

Define host/plugin compatibility using:

1. `abi_family` (for example `pj_parser`, `pj_transform`)
2. `abi_major` (breaking)
3. `abi_minor` (additive)
4. `plugin_semver`

Rules:

1. Host loads only matching `abi_major`.
2. Host may run lower/equal `abi_minor`.
3. Optional capabilities are feature-probed via `dlsym` / function table null checks.

## 5.2 Required Parser Symbols

```c
typedef struct pj_parser_handle_t* pj_parser_handle;

typedef struct {
  uint32_t abi_major;
  uint32_t abi_minor;
  const char* plugin_name;
  const char* plugin_version;
  const char* encoding;       // "ros2msg", "protobuf", "json", ...
} pj_parser_info;

// required
int32_t pj_parser_get_info(pj_parser_info* out_info);
int32_t pj_parser_create(const char* init_json, uint32_t init_len, pj_parser_handle* out);
void    pj_parser_destroy(pj_parser_handle h);
void    pj_parser_free(void* ptr);

// configure per stream/topic/schema context
int32_t pj_parser_configure(pj_parser_handle h,
                            const char* topic,
                            const char* type_name,
                            const uint8_t* schema_data,
                            uint32_t schema_len,
                            const char* config_json,
                            uint32_t config_len);

// parse to Arrow C Data Interface
int32_t pj_parser_parse_native(pj_parser_handle h,
                               const uint8_t* msg_data,
                               uint32_t msg_len,
                               double host_timestamp,
                               struct ArrowArray* out_array,
                               struct ArrowSchema* out_schema);
```

Notes:

1. Parser output **must always** include `_timestamp`.
2. `_timestamp` column is the **only authoritative timestamp** for host ingestion.
3. `host_timestamp` is an input fallback the parser may use when payload has no embedded timestamp.
4. Calling `parse` before successful `configure` must return a specific error code (for example `PJ_ERR_NOT_CONFIGURED`).

## 5.3 Optional Parser Symbols

```c
const char* pj_parser_describe_parameters();      // JSON schema
int32_t pj_parser_parse_ipc(...);                 // WASM path
```

## 5.4 Transform Symbols (same handle pattern)

`create/destroy/configure/calculate/reset` with handle parameter and Arrow I/O.

---

## 6. Discovery, Loading, and Marketplace Packaging

## 6.1 Manifest

Each plugin ships `plugin.json`:

```json
{
  "name": "my_parser",
  "type": "parser",
  "abi_family": "pj_parser",
  "abi_major": 1,
  "abi_minor": 0,
  "plugin_version": "1.2.0",
  "encoding": "protobuf",
  "format": "native",
  "library": "libmy_parser.so",
  "min_host_version": "4.0.0"
}
```

## 6.2 Search Paths

Host scans (in order):

1. User marketplace cache dir.
2. User local plugins dir.
3. System install plugins dir.
4. Additional dirs from environment variable.

## 6.3 Loader Policy

1. Native load with `RTLD_LOCAL` (avoid symbol bleed across plugins).
2. Resolve required symbols first; fail closed if any are missing.
3. Resolve optional symbols opportunistically.
4. Keep plugin instance isolation at handle level.

### Required vs Optional Symbols (Parser)

Required:

1. `pj_parser_get_info`
2. `pj_parser_create`
3. `pj_parser_destroy`
4. `pj_parser_free`
5. `pj_parser_configure`
6. `pj_parser_parse_native`

Optional:

1. `pj_parser_parse_ipc`
2. `pj_parser_describe_parameters`

Host behavior:

1. Missing required symbol -> reject plugin.
2. Missing optional symbol -> capability disabled, plugin still loadable.

---

## 7. Data Contracts

## 7.1 Timeseries Shapes

1. **Wide format**: `_timestamp` + one column per series.
2. **Tall format**: `_series_name`, `_timestamp`, value columns.

## 7.2 Metadata Channel

Define explicit metadata sidecar for non-timeseries payload:

1. `topic_metadata_json` blob attached per batch or per stream.
2. Use this for current `user_defined` use cases (for example MCAP file path metadata).
3. Host decides storage mapping into `PlotDataMapRef::user_defined`.

## 7.3 Scatter/XY

Define explicit Arrow mapping for scatter:

1. Required columns: `_x`, `_y`, optional `_series_name`.
2. No synthetic timestamp required for scatter.

---

## 8. Host Architecture

## 8.1 Dispatch Model

Retain and formalize original plan’s per-topic parser caching (Section 6.3 in original):

1. Cache key includes topic and parser identity.
2. Reconfigure when schema hash changes.
3. Support mixed encodings in a single source stream.

## 8.2 One-Shot Migration Constraint

This plan assumes all built-in plugins are migrated together in one coordinated release.

Implications:

1. No runtime dual-support/deprecation window is required.
2. Validation against legacy behavior is done in CI and pre-release qualification, not via long-term adapter coexistence.

---

## 9. UI/Dialog Strategy

## 9.1 Compute-first policy

For Phase 1-2:

1. Keep DataSource dialogs native (`QWidget*`) where already working.
2. Do not block ABI migration on `.ui + JSON` protocol maturity.

## 9.2 `.ui + JSON` protocol

Treat as incremental capability:

1. Start with simpler plugins.
2. Support custom widgets registry (`QCodeEditor`, `LineEdit`, others).
3. Add post-load UI lifecycle hooks explicitly.

---

## 10. Error, Warning, and Notification Semantics

1. Replace blocking parser-internal UI popups in new ABI plugins with structured events/errors.
2. Host policy controls: continue/skip/abort/report summary.
3. For migrated plugins, preserve current UX semantics where practical (for example continue/abort choices), but delivered through host policy and summaries.

---

## 11. Third-Party Ecosystem Strategy

1. Publish migration guide and SDK update before release candidate.
2. Run external plugin compatibility sprint (prioritize ROS ecosystem) during beta.
3. Provide reference ports for at least one parser and one source plugin from third-party repos.
4. Freeze ABI for first marketplace launch after release candidate sign-off.

---

## 12. Phased Implementation Plan with Entry Gates

## Phase 0 — Host Dispatch Decoupling in Current Qt/C++

1. Move parser selection/dispatch out of MQTT/UDP/WebSocket/ZMQ plugins into host.
2. Keep legacy interfaces unchanged.

Exit gates:

1. Functional parity for existing streamers.
2. No regression in parser selection UX.

## Phase 1 — C ABI Parsers + Arrow Bridge

1. Implement handle-based parser ABI.
2. Implement manifest discovery + loader.
3. Implement Arrow -> `PlotDataMapRef` conversion path.
4. Port easiest parsers first (`DataTamer`, `IDL`, `LineInflux`).

Exit gates:

1. Contract tests pass.
2. Parity tests pass on representative files/streams.
3. Performance gate met:
   - Throughput: <= 20% slower than legacy baseline on 1M-message MCAP.
   - Peak RSS: <= 30% increase vs baseline.
   - Live polling overhead: <= 2ms added latency per cycle at target stream rate.

## Phase 2 — C ABI Transforms

1. Implement transform ABI with handle model.
2. Port SISO transforms, then MIMO (`QuaternionToRollPitchYaw`).
3. Add delta/batching policy to avoid full-history recompute where possible.

Exit gates:

1. Transform correctness parity.
2. Live update latency budget met on large datasets:
   - <= 2x legacy transform compute time on 1M-point benchmark.
   - No frame drop at configured refresh target under representative load.

## Phase 3 — Source Model Unification

1. Introduce new DataSource API broadly.
2. Migrate simple DataSources first.
3. Keep native dialogs by default.

Exit gates:

1. No regression in layout restore and runtime configuration.
2. No regression against baseline outputs for migrated built-in plugins.

## Phase 4 — Optional Enhancements

1. `.ui + JSON` host-rendered dialog protocol expansion.
2. WASM parser/transform runtime support.
3. Marketplace UX and trust model improvements.

---

## 13. Test Plan

1. **ABI contract tests**
   - Required/optional symbol behavior.
   - Handle lifecycle correctness.
   - Memory ownership/release callbacks.
   - Configure-before-parse failure contract.

2. **Behavior parity tests**
   - Same input through baseline legacy fixtures and new path, compare output series and timestamps.

3. **Performance tests**
   - Parser throughput.
   - Transform latency for large series.
   - Memory overhead under sustained streams.

4. **UI flow tests**
   - CSV delimiter/preview.
   - MQTT connect/discover/select.
   - MCAP topic filtering and schema change handling.

---

## 14. Risks and Mitigations

1. **Risk:** ABI churn.
   - **Mitigation:** strict major/minor policy, optional symbol probing.

2. **Risk:** migration breakage for external plugins.
   - **Mitigation:** early SDK release, beta compatibility sprint, reference ports, migration docs.

3. **Risk:** transform performance regressions.
   - **Mitigation:** mandatory perf gates before broad migration.

4. **Risk:** UI protocol overreach.
   - **Mitigation:** compute-first rollout; dialog protocol non-blocking.

---

## 15. Final Recommendation

Proceed with the original architecture direction, but execute it as a **compute-first, compatibility-first migration**:

1. Fix C ABI with instance handles immediately.
2. Add explicit discovery/versioning/timestamp/metadata contracts.
3. Enforce required/optional symbol contracts and configure-before-parse behavior.
4. Coordinate one-shot built-in migration with strict benchmark and parity gates.
5. Enforce phase gates with measurable tests and benchmarks.

This yields marketplace-ready portability without forcing a high-risk all-at-once rewrite.
