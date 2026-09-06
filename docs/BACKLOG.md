# SDK backlog

Track consumer migrations and deliberate deferrals here. For shipped helpers,
start with [Existing SDK utilities](sdk-utilities.md), then follow
[the provider contract](provider-guide.md). Delivery in this branch does not
imply that consumer repositories have adopted it.

## 0.31 delivery status

The 2026-09-05 promotion plan is implemented below, with the 2026-09-06
maintainer decisions on query/rate ownership. `pj_source` replaces the compiled
`descriptor_import_support` component; the old target and includes forward for
one release. New consumers use `plotjuggler_sdk::source` and `PJ::sdk::source`.

| Original item | Status and reuse destination |
|---|---|
| 1. Environment ceilings | Shipped: `envLimit` / `minNonzero` in `source/limits.hpp`; unit conversion and saturation remain caller policy. |
| 2. Duration ceilings | Already shipped: `JobControl::armWatchdog`; consumers replace their deadline loops. |
| 3. Source presentation | Shipped: `SourcePresentation`, `recordSourcePresentation`, `sourcePresentationSettingsGroup` in `source/presentation.hpp`. |
| 4. Capture/completion tracker | Shipped: `IngestOutcomeLedger` in `source/outcome_ledger.hpp`; attachment ordering, synchronization and dataset-survival decisions remain provider responsibilities. |
| 5. Source-record envelope | Shipped: `parseSourceRecordEnvelope` in `source/record_envelope.hpp`, with conformance cases; flat mcap_cloud v1 still requires a versioned restore adapter. |
| 6. Host Stop bridge | Shipped: `StopPoller` in `source/stop_bridge.hpp`; wire provider cancellation to host `requestStop` before joining producers. |
| 7. Time and range math | Shipped: `pj_base/time_format.hpp`, `time_math.hpp`, `slider_window.hpp`; replace consumer copies. |
| 8. Delegated-ingest fixture | Shipped: `PJ::sdk::testing::DelegatedIngestFixture` in `pj_plugins/testing/delegated_ingest_fixture.hpp`. |
| 9. Metadata query language | Relocated to pj-official-plugins `common/query/` (`pj_common_query`, `PJ::query`); reuse its tokenizer/parser/completion/filter, not an SDK copy. |
| 10. Stable table-row identity | Deferred: requires dialog-protocol and host work; do not promote label-to-identity collision workarounds. |
| 11. Rolling transfer rate | Relocated to pj-official-plugins `common/transfer_rate/` (`pj_transfer_rate`, `PJ::common::RollingTransferRate`). |
| 12. Packaged mock plugins | Shipped: four fixture sources under `share/plotjuggler_sdk/test_fixtures/`, built with `pj_add_sdk_test_fixture(name out_target)`. PJ4 can remove its second SDK FetchContent during its pin bump. |

`source/*.hpp` above means `pj_base/sdk/source/*.hpp`. `time_format` belongs
to pj_base; fixtures belong to pj_plugins/testing. `pj_cloud` remains reserved
for future connection/reconnect code.

## Consumer migration gates

The following mcap-cloud constraints come from the 2026-09-05 sharing audit of
its SDK 0.20 implementation and toolbox_mosaico. Recheck them against the
consumer branch when adopting 0.31; SDK delivery does not resolve them.

### Preserve behavior while deleting provider copies

- Not every wait is cancellable yet: reconnect-Hello uses
  `sendAndWait(wake_on_cancel=false)` — deleting the deadline mechanism
  without fixing this adds up to 10 s after watchdog expiry; add a
  cancellation-during-reconnect-Hello test.
- `ImportRuntime` is not only a cache: it owns shared host-write
  serialization (SDK threading still forbids concurrent calls on one
  toolbox host), FIFO admission, and durable trust write-through — retain
  or explicitly replace each.
- SDK `file_lock`/`fs_durability` are in `detail` (not public); the
  surviving trusted-origins ledger needs them — either a supported public
  surface or keep the ancestor copies until trust storage changes.
- `SessionMcapWriter` also backs the export feature and the always-built
  CLI, and preserves BOTH log and publish timestamps (delegated ingest
  passes only log time) — keep it until a concrete replacement exists.
- Raw-session preservation is stronger than capture: a failed parser
  binding prevents the push M3 records, but mcap_cloud's cache tee kept
  the bytes anyway — adding attach/complete calls does not preserve this.
- Completion must precede release and describe the RESOLVED full request:
  mcap_cloud's `topics=[]` means "all" and must expand (incl. forced
  topics) before attaching; the host rejects empty requested sets and
  entirely-empty captures regardless of attestation.
- Its v1 descriptor arrays preserve order (SessionKey sorts separately) —
  preserve identity semantics while changing cache ownership; both E2E
  harnesses assume provider-digest cache paths that stop existing.

### Deliberately NOT promoted (product differences, not duplication)

- Per-plugin descriptor shapes and their frozen identity vector files
  (`mosaico:v1`, `mcap-cloud:v1`) — only the pattern (thin typed wrapper
  over `SourceDescriptorPolicy`, producers round-trip through the parser)
  transfers.
- Trust sources: mcap-cloud's durable Hello ledger vs Mosaico's env
  allowlist.
- Credential stores: 0600 token file vs settings-only.
- `tls_utils` (ws-TLS vs grpc-TLS — deliberately diverged) and the two E2E
  scripts.
- Leaf UI utilities duplicated across the two plugins (`date_filter`,
  `name_filter`, `table_sort`, `settings_store`, `server_history`, …):
  stable, header-mostly, cheaper to re-copy than to version across repos.

### Migration prerequisite (mcap-cloud side, not an SDK change)

Before the mcap-cloud plugin adopts the SDK canonicalizer: a vectors-replay
test proving the SDK reproduces its frozen
`mcap_server/docs/source-descriptor-vectors.json` byte-exactly — its cached
identities must survive the rewrite, or its descriptor version bumps. Its
descriptor also carries the scheme inside `server_uri`; adopting Mosaico's
scheme-less posture (transport security decided per-machine, never by a
layout) is a vector-breaking choice best made during that migration.
