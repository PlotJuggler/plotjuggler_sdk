# Open SDK items and consumer migration gates

Track open deferrals and consumer migrations here. For shipped helpers,
start with [Existing SDK utilities](sdk-utilities.md), then follow
[the provider contract](provider-guide.md). Shipping a helper in the SDK does
not imply that consumer repositories have adopted it.

## Open items

- **Stable table-row identity.** Deferred: requires dialog-protocol and host
  work; do not promote label-to-identity collision workarounds.

## Consumer migration gates

The following mcap-cloud constraints (from a sharing audit of its implementation
and toolbox_mosaico) apply when it adopts the SDK source helpers. Recheck them
against the consumer branch; SDK delivery does not resolve them.

### Preserve behavior while deleting provider copies

- Not every wait is cancellable: reconnect-Hello uses
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
