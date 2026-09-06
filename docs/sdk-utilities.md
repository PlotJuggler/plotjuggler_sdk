# Existing SDK utilities

Before implementing a helper, find the existing symbol here and read its
header. Reuse its contract; keep provider-specific semantics in caller adapters.

Headers below use installed include paths. In this repository, find them
under pj_base/include/ or pj_plugins/include/.
Unqualified symbols are in PJ::sdk::source and require
plotjuggler_sdk::source. PJ utilities require plotjuggler_sdk::base,
also supplied by plugin_sdk.

A namespace prefix applies to every symbol in its cell. Dialog headers come
from `pj_plugins/dialog_protocol/include/`. Packaged guides are installed in
`share/plotjuggler_sdk/docs/`; resolve header paths against the package's `include/`.

| Symbol | Use it for | Header |
|---|---|---|
| `PJ::parseNumber<T>` | Whole-string numeric parsing; locale-independent floats; rejects overflow/trailing junk | `pj_base/number_parse.hpp` |
| `PJ::combineSecondsAndNanos, scaleToNanoseconds, secondsToNanoseconds` | Checked timestamp arithmetic and unit conversion | `pj_base/time_math.hpp` |
| `PJ::formatTimestamp, formatDuration, needsLongFormat` | UTC timestamp and duration display | `pj_base/time_format.hpp` |
| `PJ::parseIso8601Utc, formatIso8601Utc, formatDateTimeUtc, formatDateDDMMYYYY, formatDateOnlyIso` | Parse ISO timestamps or format UTC dates; read precision/timezone rules | `pj_base/time_format.hpp` |
| `PJ::sliderToWindow` | Checked slider-to-nanosecond window, including the final frame | `pj_base/slider_window.hpp` |
| `parseDecimalNs` | Digits-only decimal nanoseconds; checked INT64_MAX; zero is a value | `pj_base/sdk/source/source_descriptor.hpp` |
| `validateSchemelessOrigin` | Exact lowercase host:port descriptor validation; no normalization | `pj_base/sdk/source/origin.hpp` |
| `OriginPolicy, parseOrigin, sameOrigin, parseOriginList, originAllowed` | Transport-origin parsing/comparison and allowlists | `pj_base/sdk/source/origin.hpp` |
| `envLimit, minNonzero` | Strict env ceiling parsing and merging; invalid input uses fallback | `pj_base/sdk/source/limits.hpp` |
| `SourceDescriptorPolicy, parseSourceDescriptor, canonicalSourceDescriptorJson, sourceDescriptorIdentity` | Bounded descriptors, canonical identity bytes and hashing | `pj_base/sdk/source/source_descriptor.hpp` |
| `parseSourceRecordEnvelope` | Validate the host attachment envelope without rewriting bytes | `pj_base/sdk/source/record_envelope.hpp` |
| `SourcePresentation, recordSourcePresentation, sourcePresentationSettingsGroup` | Host-compatible sanitized presentation and settings keys | `pj_base/sdk/source/presentation.hpp` |
| `IngestOutcomeLedger` | Whole-request completion and empty-topic attestation | `pj_base/sdk/source/outcome_ledger.hpp` |
| `ProviderJob, JobControl::armWatchdog, SettlementLatch` | Import-job lifecycle, elapsed deadlines and asynchronous settlement | `pj_base/sdk/source/provider_job.hpp` |
| `StopPoller` | Host Stop to transport cancellation; caller wires reverse Stop | `pj_base/sdk/source/stop_bridge.hpp` |
| `RequestArtifactCache` | Validated artifact publication, read leases and cleanup | `pj_base/sdk/source/request_cache.hpp` |
| `PJ::readDescriptorImportStartRequest, PJ::writeDescriptorQueryResult` | Caller-sized descriptor-import ABI structs | `pj_base/sdk/descriptor_import.hpp` |
| `PJ::sdk::testing::DelegatedIngestFixture` | Delegated-ingest ownership, completion, Stop and discard tests; use plugin_sdk | `pj_plugins/testing/delegated_ingest_fixture.hpp` |
| `pj_add_sdk_test_fixture(name out_target)` | Build packaged mock plugins; no second SDK source download | `cmake/PjSdkTestFixtures.cmake` |

## Plugin helpers

These helpers use `plotjuggler_sdk::plugin_sdk`; namespaces are explicit.
`pj_base/sdk/version.hpp` is generated at build time and installed with the SDK.
CMake helpers become available through `find_package(plotjuggler_sdk)`.

| Symbol | Use it for | Header |
|---|---|---|
| `PJ::sdk::DrainQueue<T>` | Receive-thread to onPoll handoff: push on I/O thread, drain on host thread | `pj_plugins/sdk/streaming_source.hpp` |
| `PJ::sdk::LatestValueSlot<T>` | Coalesced status/snapshot handoff: set/take the latest value | `pj_plugins/sdk/streaming_source.hpp` |
| `PJ::sdk::DelegatedIngestCache::push` | Cache parser bindings and anchor payloads; binding-unavailable is a non-error disposition | `pj_plugins/sdk/streaming_source.hpp` |
| `PJ::sdk::parserConfigOverride` | Read the host-injected _parser_config without inventing a config key | `pj_plugins/sdk/streaming_source.hpp` |
| `PJ::sdk::stringSetFromViews` | Copy topic-subscription ABI string views into a set | `pj_plugins/sdk/streaming_source.hpp` |
| `PJ::sdk::ArrayLimit, arrayLimitFromJson, arrayLimitToJson, kMaxArraySizeKey, kArrayPolicyKey` | Shared parser array clamp/skip config contract | `pj_plugins/sdk/parser_array_policy.hpp` |
| `PJ::sdk::writeEncodingSelector, encodingAt, parseEncodingsJson` | Populate a streaming dialog's parser-encoding selector | `pj_plugins/sdk/streaming_dialog.hpp; pj_plugins/sdk/encoding_utils.hpp` |
| `PJ::sdk::mergeVisibleSelection, passesSelectionFilter` | Preserve hidden selections when the host reports only visible rows | `pj_plugins/sdk/streaming_dialog.hpp` |
| `PJ::sdk::composeEndpoint, composeHostPort, authorityHost` | Compose an IPv6-safe transport endpoint from dialog fields | `pj_plugins/sdk/endpoint.hpp` |
| `PJ::sdk::parsePort, lowerAscii` | Strict port validation and ASCII token normalization | `pj_base/sdk/text_utils.hpp` |
| `PJ::sdk::getEnv, getSharedLibDir, userDataDir` | Portable environment, plugin directory and per-user data paths | `pj_base/sdk/platform.hpp` |
| `PJ::SemVer::parse, PJ::SemVer::isValid, PJ::SemVer::operator<=>; PJ::sdkVersion` | Parse/compare SDK and manifest versions | `pj_base/sdk/semver.hpp; pj_base/sdk/version.hpp` |
| `PJ::WidgetEvent` | Decode raw widget events; DialogPluginTyped already dispatches typed handlers | `pj_plugins/sdk/widget_event.hpp` |
| `PJ::FilePickerOptions, FilePickerResult, TreeItem, TreeCell` | Canonical picker/tree payloads and wire-value spellings | `pj_plugins/sdk/file_picker_types.hpp; pj_plugins/sdk/tree_types.hpp` |
| `PJ::sdk::MediaMetadataBuilder, ObjectTopicMetadataBuilder` | Build media hints and canonical renderer metadata | `pj_base/sdk/media_metadata.hpp; pj_base/sdk/object_topic_metadata.hpp` |
| `PJ::sdk::ObjectBytes` | Own object-read bytes through move-only RAII | `pj_base/sdk/object_bytes.hpp` |
| `PJ::sdk::ArrowSchemaHolder, ArrowArrayHolder, ArrowStreamHolder` | Own Arrow out-params; successful writes transfer ownership, failed writes retain it | `pj_base/sdk/arrow.hpp` |
| `pj_embed_file, pj_configure_plugin` | Embed UI/manifest assets and configure plugin exports/build rules | `cmake/PjPlugin.cmake` |

Outside this SDK, reuse pj-official-plugins common/:

- Rate display: PJ::common::RollingTransferRate,
  common/transfer_rate/include/pj_transfer_rate/transfer_rate.hpp;
  target pj_transfer_rate.
- Metadata query tokenizer/parser/completion/editing/filter:
  common/query/include/pj_query/; namespace PJ::query;
  target pj_common_query. Do not create a second tokenizer.

For provider implementation, follow [the provider contract](provider-guide.md).
