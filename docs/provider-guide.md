# Writing a source provider

A descriptor-import provider turns a saved request into a finite dataset. Link
`plotjuggler_sdk::source` and use `PJ::sdk::source`; the component owns the common
C++ machinery, while your plugin owns request semantics, transport, trust,
credentials, parser selection and UI. This guide uses the existing C ABI. It
adds no protocol slots.

`plotjuggler_sdk::descriptor_import_support`, its Conan component, and headers
under `pj_base/sdk/descriptor_import/` forward to `source` for one release.
Existing source consumers still build; new code uses `pj_base/sdk/source/`.
The consumer extension views remain in `pj_base/sdk/descriptor_import.hpp`.

## Define the request before implementing transport

Declare a `SourceDescriptorPolicy` with identity fields, presentation fields,
resource bounds and an `IdentityScheme`. Reject unknown fields. The shared
parser validates the top-level allowlist; your typed parser must validate required
fields, exact version **values**, nested allowlists and provider semantics.
Never narrow a JSON version to `int` before comparing it. An unsigned version of
4294967297 is a different value from 1.

Canonical serialization includes only identity fields, sorted alphabetically,
with compact UTF-8 JSON and recursively sorted object keys. Array ordering is
preserved. Decide whether topic order is meaningful in your descriptor and pin
that decision; canonicalization does not silently sort arrays. Presentation
changes must not change identity. Producers should round-trip through the same
typed parser as restored descriptors, rejecting invalid UTF-8 rather than
substituting bytes in a request identity.

Keep frozen vectors of input, canonical bytes, identity and rejected shapes.
Changed accepted bytes, origin normalization, field layout or identity rules
require an explicit descriptor version cut and restore strategy. Do not regenerate
vectors to conceal a change. SDK version and descriptor version are separate.
`parseDecimalNs` accepts at most 20 decimal digits, rejects signed/whitespace/junk
input and checks INT64_MAX. Zero has no built-in unset meaning.

Use `validateSchemelessOrigin` when a descriptor must contain an exact lowercase
`host:port`: host bytes `[a-z0-9._-]`, port 1..65535 without leading zeros. It
intentionally rejects schemes, userinfo, paths, query/fragment and IPv6. Widening
that grammar changes the contract of descriptors embedding it and requires a
version bump. `parseOrigin` and `OriginPolicy` serve transport URIs instead;
they support explicitly allowed schemes/default ports. Do not substitute one
validator for the other during a migration without replaying identity vectors.

## Meet the host acceptance envelope

Before attaching a record, run `parseSourceRecordEnvelope(bytes)`. Envelope v1
allows exactly:

```json
{"kind":"example.pull","v":1,"request":{"origin":"example.org:443","topics":["/imu"]},"label":"Run 42"}
```

`kind` is a non-empty string, `v` is an unsigned JSON integer, `request` is an
object, and optional `label` is a string. The generic host accepts any unsigned
version without narrowing it; your provider selects supported versions by exact
value. The envelope version constant describes this host grammar, not `v`.
The validator returns the parsed object without rewriting the supplied bytes.
Attachment/cache identity remains byte-exact over the supplied descriptor.

The shared policy limits descriptors to 64 KiB, string values and keys to 4096
bytes, containers to 4096 entries and nesting to 16 levels. Every object and
array is checked recursively for these exact credential-shaped keys:
`api_key`, `apikey`, `token`, `password`, `secret`, `credentials`,
`authorization`, `cert_path`. This case-sensitive denylist matches PJ4 and is
only defense in depth: your typed request allowlist must still account for all
fields. Resolve secrets and machine-local transport/security configuration
outside the descriptor.

The SDK preserves PJ4's empty/parse/bounds/unknown-field/required-field/credential
refusal categories. One intentional tightening is optional `label` type checking:
PJ4's pre-promotion private implementation did not check it. Flat mcap_cloud v1
is an explicitly **rejected** conformance vector. Pre-envelope descriptors need
a versioned adapter understood by the restore path; never silently wrap frozen
canonical bytes in a new `request` object.

## Attach once, finish truthfully

1. Resolve the entire request, including any provider shorthand such as “all
   topics”, before declaring it or computing canonical bytes.
2. Create the provisional dataset and its ingest context. Notify the provider
   job's dataset callback before first publication. Attach the canonical source
   record once for the batch, before the first push. A refused attachment means
   uncacheable ingest; it does not justify claiming the bytes were attached.
3. Initialize `IngestOutcomeLedger` with the entire declared topic set. Bind and
   push each topic through the host. Record `kOk` only after its full range has
   completed; `kEmptyOk` means a successfully fetched, empty window. Failed and
   unvisited topics remain failure/pending evidence. Undeclared topics cannot
   extend the ledger's request.
4. Stop producers, then compute completion with provider cancellation **and a
   live `isStopRequested()` read at that decision**. Cancellation wins; any
   failed/pending topic yields FAILED; otherwise the result is COMPLETED, with
   `ATTESTS_EMPTY_TOPICS` exactly when at least one topic was empty.
5. Pass the declared topic list and result to `completeIngest` before closing
   the ingest/progress bracket and releasing the context. The ledger is pure
   logic: the caller owns synchronization and attachment/lifecycle operations.
6. Decide the provider terminal from actual dataset survival. COMPLETED ingest
   does not guarantee a published artifact, or even a surviving dataset. If an
   all-empty provisional dataset was discarded, the provider terminal must not
   claim success. Explicitly discard aborted provisional ingests; normal release
   can commit and is not a substitute for rollback.

For example, after producers quiesce:

```cpp
const auto completion = ledger.computeCompletion(job.isCancelled() || ingest.isStopRequested());
const auto accepted = ingest.completeIngest(completion.outcome, declared_topics, completion.flags);
```

Check the returned status according to your caching policy. Older hosts without
`complete_ingest` still ingest data, but cannot provide successful capture
attestation. A structurally empty topic list can pass the completion validator;
host cacheability policy still refuses empty requested sets and entirely empty
captures. The ledger does not infer successful coverage from pushed messages.

## Connect both cancellation directions

Construct `StopPoller` after the context and destroy it, or call `stopAndJoin()`,
**before** releasing that context. Supply a live stop check and a transport-cancel
action. It checks immediately and then every 50 ms by default; a positive custom
period is supported. Its join ensures no poller callback can access the released
context. Callbacks must not throw or destroy/join their own poller.

Provider cancel must call the host's `requestStop` **before joining producers**.
Use a Stop view with synchronization independent of a blocked push/progress
mutex; otherwise a push waiting for Stop can prevent Stop itself from running.
The poller's callback must be able to cancel transport without joining a producer
that owns the poller. Do not use a last-polled stop flag as terminal evidence.

`JobControl::armWatchdog` already provides elapsed-time ceilings. It and
`StopPoller` have different triggers. Keep reconnect, admission and transport
waits cancellable as well; a stop bridge cannot interrupt a wait whose transport
ignores cancellation. `ProviderJob` supplies gated startup, exactly-once terminal,
join and cancellation-hook ownership; it does not own the plugin's transport.

## Presentation, limits and dialog helpers

`SourcePresentation` carries display name, fallback name and origin.
`recordSourcePresentation` writes through the host settings view on the main
thread, skipping unchanged values. It uses `source_presentation/v1/` plus
unpadded base64url of the identity bytes as the settings group. This is a host
contract: `/` inside an identity must not become a settings separator.
Presentation strips C0/C1, zero-width and bidi controls, including U+061C,
U+2060..U+2064 and U+FEFF, and caps text at 200 UTF-16 units without splitting a
code point. Pass valid UTF-8. Presentation persistence is best-effort and does
not determine whether a download succeeds.

Merge caller and machine ceilings with `minNonzero`: zero on one side imposes no
ceiling. `envLimit(name, fallback)` accepts a literal zero, but missing, empty,
signed, whitespace, junk and overflowing values use the safe fallback. In
particular `0junk` never turns a ceiling off. Saturate unit conversions at the
caller before passing a duration to `armWatchdog`; unchecked seconds-to-milliseconds
multiplication can otherwise defeat the ceiling.

`PJ::parseIso8601Utc` and UTC/duration formatters live in pj_base proper.
`PJ::sliderToWindow` returns a checked half-open range, extending the upper
endpoint one tick past the final frame. Degenerate ranges, invalid slider
positions and unrepresentable `max-min`/`max+1` return nullopt. Caller adapters
own “zero means unset/unbounded”. `RollingTransferRate` uses steady_clock and a
five-second cumulative-byte window; use one per stream and sum rates. Add an
unchanged count to update an idle rate; counter reset restarts the window.

The metadata query language (tokenizer, AST, completion, cursor editing,
combined filter) deliberately lives OUTSIDE the SDK — in pj-official-plugins'
`common/` — so its syntax can evolve per release without an SDK compatibility
event (maintainer decision, 2026-09-06). Lua execution, provider vocabularies,
UI orchestration, stable row identity and `pj_cloud` connection code are also
outside this release.

## Test against the contract

Use `pj_plugins/testing/delegated_ingest_fixture.hpp` through `plugin_sdk`. Its
`DelegatedIngestFixture` supplies toolbox write/runtime views and an independent
DataSource Stop view, failure injection, truncated-vtable emulation, a synchronized
recording snapshot, and a blocked-push wait that Stop can wake. It fetches payloads
twice, checks byte identity, releases the fetcher before copying anchored data,
and runs completions through the SDK's `copyIngestCompletion`. Check recorded
notification/attachment/push/completion/release ordering and dataset survival.
Configure knobs before starting threads and join before release/destruction.
Use `pj_base/sdk/testing/provider_job_probe.hpp` for the startup callback gate
and `request_cache_probe.hpp` for cleanup races.

The installed package also contains the four fixture sources under
`share/plotjuggler_sdk/test_fixtures/`. No second source-archive download is needed:

```cmake
find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk source)
pj_add_sdk_test_fixture(mock_data_source fixture_target)
# fixture_target now names pj_sdk_fixture_mock_data_source, a SHARED target.
```

Names are `mock_data_source`, `mock_file_source`, `mock_toolbox` and
`missing_id_data_source`. The helper returns an existing target on repeated calls;
the missing-ID fixture deliberately retains its invalid manifest. Internal SDK
examples continue to build under their existing targets.

## Header map

Paths below are relative to `pj_base/include/pj_base/sdk/source/`:

| Header | Purpose |
|---|---|
| [source_descriptor.hpp](../pj_base/include/pj_base/sdk/source/source_descriptor.hpp) | Policy, canonical bytes, identity scheme, decimal-nanosecond parsing |
| [origin.hpp](../pj_base/include/pj_base/sdk/source/origin.hpp) | Transport origin policy and strict schemeless origin validation |
| [limits.hpp](../pj_base/include/pj_base/sdk/source/limits.hpp) | Environment ceilings and min-nonzero merge |
| [presentation.hpp](../pj_base/include/pj_base/sdk/source/presentation.hpp) | Host settings group and sanitized presentation |
| [outcome_ledger.hpp](../pj_base/include/pj_base/sdk/source/outcome_ledger.hpp) | Whole-request outcome/empty-topic attestation |
| [record_envelope.hpp](../pj_base/include/pj_base/sdk/source/record_envelope.hpp) | Versioned source-record acceptance grammar |
| [stop_bridge.hpp](../pj_base/include/pj_base/sdk/source/stop_bridge.hpp) | Host Stop polling and reverse cancellation contract |
| [provider_job.hpp](../pj_base/include/pj_base/sdk/source/provider_job.hpp) | Job, cancel hook, elapsed watchdog, settlement latch |
| [request_cache.hpp](../pj_base/include/pj_base/sdk/source/request_cache.hpp) | Request-addressed artifacts, leases, publication and cleanup |
| [transfer_rate.hpp](../pj_base/include/pj_base/sdk/source/transfer_rate.hpp) | Monotonic rolling transfer rate |

Related: [time_format.hpp](../pj_base/include/pj_base/time_format.hpp),
[slider_window.hpp](../pj_base/include/pj_base/slider_window.hpp),
[delegated-ingest fixture](../pj_plugins/include/pj_plugins/testing/delegated_ingest_fixture.hpp).
