# SDK backlog

Deliberate deferrals with an agreed landing slot. SDK releases are infrequent
and batched: items here ride the next natural version bump rather than
triggering one. Each entry names its consumers so the release that ships it
can also collect the deletions.

## 0.31 riders — the cloud-provider promotion batch

Agreed 2026-09-05 from the toolbox_mosaico / toolbox_mcap_cloud sharing
analysis (the mcap-cloud plugin, in the pj-mcap-server repo, is pinned to SDK
0.20 and predates `descriptor_import_support`, which was originally harvested
from it). The natural vehicle is the mcap-cloud 0.20 → 0.30+ migration; these
four items land first so that migration is deletion plus thin wiring instead
of a second hand-rolled copy of provider choreography.

1. **`envLimit(name, fallback)` + `minNonzero(a, b)`** — beside
   `provider_job.hpp`. Fail-safe env ceiling parsing: strict full-string
   unsigned decimal, unparsable → safe default (never "off"), saturating
   unit conversion. Today byte-identical copies exist in
   `toolbox_mosaico/src/descriptor_import_provider.cpp` and
   `toolbox_mcap_cloud/src/descriptor_import_provider.cpp`; both delete
   theirs on adoption.

2. **Duration ceilings via `JobControl::armWatchdog`** — no new code; the
   mcap-cloud plugin replaces its hand-rolled job-thread deadline with the
   existing API during its migration. Listed here so the migration checklist
   carries it.

3. **`source_presentation`** (promote from
   `toolbox_mosaico/src/source_presentation.{hpp,cpp}`) — unpadded
   base64url(identity) settings-group naming plus display-string
   sanitization (C0/C1, zero-width, bidi, ALM, word-joiners; UTF-16-unit cap
   without splitting code points). This encodes a HOST contract: PlotJuggler's
   Load-Layout dialog reads that settings group and owns the display cap, so
   every descriptor-import provider needs it verbatim. Belongs next to
   `provider_job.hpp`. Consumers: toolbox_mosaico (deletes its copy),
   toolbox_mcap_cloud (gains it — it has no presentation module today).

4. **Capture/completion tracker** — a small helper encapsulating the
   "truthful provider" choreography currently open-coded in
   `toolbox_mosaico/src/fetch_worker.cpp` (~200 lines, hardened over three
   review rounds): attach the canonical source record once per batch after
   ingest-context creation; per-topic outcome ledger
   (ok / empty-ok / failed / pending); whole-request terminal computation
   (any cancel — including a live `isStopRequested()` read at decision time —
   → CANCELLED; any failed/pending → FAILED; else COMPLETED with
   `ATTESTS_EMPTY_TOPICS` iff any empty window), reported before the ingest
   bracket closes; dataset-survival signaling so the provider terminal can
   refuse success when the batch's provisional dataset was discarded
   (all-empty rollback). This logic is security-adjacent — a wrong terminal
   poisons the host request cache — which is why it should be reviewed once
   here rather than per plugin. Consumers: toolbox_mosaico (swaps to the
   helper), toolbox_mcap_cloud (adopts M3 with it).

### Additions from the deep toolbox_mcap_cloud audit (2026-09-05, second pass)

5. **The `attach_source_record` acceptance envelope as an SDK contract** —
   today `{kind, v, request, label}` + object-valued `request` is a PRIVATE
   convention inside PJ4's `DataSourceRuntimeHost::sourceRecordEnvelope()`.
   mcap_cloud's flat v1 descriptor shape (all six frozen vectors) FAILS that
   envelope — each provider currently has to discover the host convention by
   trial. Promote the acceptance policy/validator + conformance cases so the
   contract is public and versioned. mcap_cloud's migration then needs an
   explicit decision: reshape (vector-breaking) or a versioned adapter the
   restore path understands — never silent re-wrapping of canonical bytes.

6. **Host-Stop ↔ transport-cancel bridge** — both providers independently
   built the same two directions: a poller observing the live ingest
   context's stop flag (mcap_cloud `HostStopWatchdog`, Mosaico's 50 ms
   poller + `StoppableThread`), and provider-cancel calling `requestStop`
   BEFORE joining producers via synchronization independent of a blocked
   push (Mosaico's `stop_view_` lesson). Small helper; distinct from
   `armWatchdog` (elapsed-time vs host-button are different triggers).

7. **`time_format` + checked slider/range math** — `core/time_format.{h,cpp}`
   is byte-identical in both plugins (286 lines: ISO parse, UTC/duration
   formatting). Promote hardened (the fractional-seconds addition can
   overflow; `time_math.hpp::combineSecondsAndNanos` fixes it), together
   with mcap_cloud's overflow-safe slider-to-time conversion + its
   regression test (Mosaico's `mosaico_dialog.cpp` still carries the
   overflowing multiplication — a real bug to fix on adoption).

8. **Delegated-ingest test fixture** (`pj_plugins` testing support) — both
   plugins hand-build the same toolbox/runtime vtable fakes (mcap_cloud
   `parser_ingest_test_support.hpp`, Mosaico `FakeIngestHost`). Merge their
   strongest checks (double-fetch + anchor lifetime; attachment/completion/
   Stop/discard evidence; truncated vtables; cancellation during blocked
   ingest). A third provider otherwise rebuilds the ABI plumbing.

9. **(Optional, dialog support) metadata-query library** — the six `query/`
   headers are byte-identical across the plugins (1,505 lines: tokenizer,
   AST, shorthand expansion, completion, cursor-aware editing, Lua eval) +
   the combined name/date/query filter. The single largest proven
   duplication. Move language/editing + tests into optional dialog support
   with Lua kept optional; provider vocabularies and UI orchestration stay
   local.

10. **(Optional, needs host + dialog-protocol work) stable table-row
    identity** — an opaque row key echoed in selection events (like the
    tree's selected-ID semantics). mcap_cloud reverses presentation into
    identity with a provably non-injective label fallback; Mosaico couples
    selection to display strings and survives only while display == backend
    name. Do NOT promote mcap_cloud's collision workaround.

11. **(Small, lower priority) rolling transfer-rate accumulator** —
    duplicated 5-second cumulative-byte window in both dialogs; fix to a
    monotonic clock on promotion.

### Migration gates the plan must respect (mcap_cloud side)

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
