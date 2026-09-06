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
