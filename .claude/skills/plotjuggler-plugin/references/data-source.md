# DataSource plugin

A DataSource turns a file or a live source into topics + fields in PlotJuggler.
It is **write-only** (it cannot read existing host data). Pick the base class that
matches how your data arrives; each is a thin specialization of
`DataSourcePluginBase` that pre-declares the right capabilities and lifecycle.

Full reference: `pj_plugins/docs/data-source-guide.md`.

## Pick the base class

| Data arrives as | Base class | You override |
|---|---|---|
| A **file**, imported once (CSV, Parquet, MCAP) | `PJ::FileSourceBase` | `extraCapabilities()`, `importData()` |
| A **live stream** you decode yourself | `PJ::StreamSourceBase` + `kCapabilityDirectIngest` | `extraCapabilities()`, `onStart/onPoll/onStop()` |
| A **transport** whose payload a MessageParser should decode (MQTT/ZMQ/UDP) | `PJ::StreamSourceBase` + `kCapabilityDelegatedIngest` | same, plus bind a parser and `pushMessage()` |
| None of the above | `PJ::DataSourcePluginBase` | `capabilities()`, `start/stop()`, `currentState()`, own state machine |

Header: `pj_base/sdk/data_source_patterns.hpp` (the `*SourceBase` helpers) or
`pj_base/sdk/data_source_plugin_base.hpp` (the raw base).

## Minimal file source

```cpp
#include <pj_base/sdk/data_source_patterns.hpp>

namespace {
class MyCsvSource : public PJ::FileSourceBase {
 public:
  uint64_t extraCapabilities() const override { return PJ::kCapabilityDirectIngest; }

  std::string saveConfig() const override { return config_; }
  PJ::Status  loadConfig(std::string_view json) override {
    config_ = std::string(json);       // host injects {"filepath":"/path", ...} here
    return PJ::okStatus();
  }

  PJ::Status importData() override {
    if (!writeHostBound() || !runtimeHostBound()) return PJ::unexpected("hosts not bound");
    auto topic = writeHost().ensureTopic("my/topic");
    if (!topic) return PJ::unexpected(topic.error());

    // progressStart() is [[nodiscard]]; failure just means "host can't show it".
    const bool progress = bool(runtimeHost().progressStart("Importing", total_rows_, /*cancellable=*/true));
    for (uint64_t i = 0; i < total_rows_; ++i) {
      const PJ::sdk::NamedFieldValue fields[] = {{.name = "value", .value = rows_[i].value}};
      auto st = writeHost().appendRecord(*topic, rows_[i].timestamp_ns, fields);
      if (!st) return PJ::unexpected(st.error());
      if (progress && !runtimeHost().progressUpdate(i)) {
        return PJ::unexpected("import canceled by user");   // cancel is a real outcome, not ok
      }
    }
    return PJ::okStatus();   // FileSourceBase calls progressFinish() for you
  }

 private:
  std::string config_ = "{}";
  // ... total_rows_, rows_ populated from the file named in config_ ...
};
}  // namespace

PJ_DATA_SOURCE_PLUGIN(MyCsvSource,
    R"({"id":"my-csv","name":"My CSV","version":"1.0.0","file_extensions":[".csv"]})")
```

## Minimal live stream (self-decoding)

```cpp
#include <pj_base/sdk/data_source_patterns.hpp>
#include <pj_plugins/sdk/streaming_source.hpp>   // DrainQueue: the receive-thread → onPoll() handoff
#include <atomic>
#include <thread>

class MyUdpSource : public PJ::StreamSourceBase {
 public:
  uint64_t extraCapabilities() const override { return PJ::kCapabilityDirectIngest; }

  std::string saveConfig() const override { return config_; }
  PJ::Status  loadConfig(std::string_view j) override { config_ = std::string(j); return PJ::okStatus(); }

  PJ::Status onStart() override {
    fd_ = openSocket();                 // your I/O
    if (fd_ < 0) return PJ::unexpected("failed to open socket");
    running_.store(true);
    io_ = std::thread([this] { recvLoop(); });   // background thread ONLY buffers
    return PJ::okStatus();
  }

  PJ::Status onPoll() override {        // host thread — the only place you touch the host
    auto batch = pending_.drain();      // swaps the whole queue out under a brief lock
    auto topic = writeHost().ensureTopic("udp/data");
    if (!topic) return PJ::unexpected(topic.error());
    for (; !batch.empty(); batch.pop()) {
      const Sample& s = batch.front();
      const PJ::sdk::NamedFieldValue f[] = {{.name = "value", .value = s.value}};
      auto st = writeHost().appendRecord(*topic, s.timestamp_ns, f);
      if (!st) return PJ::unexpected(st.error());
    }
    return PJ::okStatus();
  }

  void onStop() override {              // must be idempotent
    running_.store(false);
    if (fd_ >= 0) shutdownSocket(fd_);  // unblock a blocking recv BEFORE join, or join() hangs forever
    if (io_.joinable()) io_.join();
    if (fd_ >= 0) { closeSocket(fd_); fd_ = -1; }
    pending_.clear();
  }

 private:
  struct Sample { PJ::Timestamp timestamp_ns; double value; };
  void recvLoop() {
    while (running_.load()) {
      pending_.push(receiveOne(fd_));   // do NOT call writeHost() here
    }
  }
  int fd_ = -1; std::string config_ = "{}";
  std::atomic<bool> running_{false}; std::thread io_;
  PJ::sdk::DrainQueue<Sample> pending_;   // NOT a hand-rolled mutex + vector
};

PJ_DATA_SOURCE_PLUGIN(MyUdpSource, R"({"id":"my-udp","name":"My UDP","version":"1.0.0"})")
```

When only the newest value matters (a status snapshot, a "latest frame"), use
`PJ::sdk::LatestValueSlot<T>` (`set()` / `take()`) instead of a queue — it coalesces
for you. Both live in `pj_plugins/sdk/streaming_source.hpp`; do not write your own.

For **delegated** ingest (let a MessageParser decode the bytes): declare
`kCapabilityDelegatedIngest` and hold a `PJ::sdk::DelegatedIngestCache` (same
header). In `onPoll()`:

```cpp
PJ::sdk::ParserBindingRequest req{.topic_name = topic, .parser_encoding = encoding_,
                                  .parser_config_json = parser_config_};
auto r = ingest_.push(runtimeHost(), /*cache_key=*/topic, req, ts, std::move(bytes));
if (!r) return PJ::unexpected(r.error());               // a failed push IS an error
// r == kBindingUnavailable: no parser for that encoding yet — skipped, retried next poll
```

The cache owns the `ensureParserBinding` + `pushMessage` pair per topic and anchors
the payload bytes so the host's fetch callable is idempotent and by-value (the host
may invoke it zero, one, or many times, from consumer threads, and releases it once
even when the push fails). The config-envelope that ties source⇆parser is managed
by the host; you never see the parser directly. Two delegated-ingest traps the
helpers already encode — know them anyway:

- **Forward `_parser_config`.** The host injects the parser's saved options into
  *your* `loadConfig()` JSON under the key `"_parser_config"`. Read it with
  `parser_config_ = PJ::sdk::parserConfigOverride(json)` and pass it as
  `ParserBindingRequest.parser_config_json` — otherwise schema-based parsers bind
  unconfigured and silently drop every message.
- **Binding-unavailable is not an error.** `DelegatedIngestCache::push` returns the
  `kBindingUnavailable` disposition, not an error, for exactly this reason; only a
  failed `pushMessage` on an established binding is a real error.

**Alternative receive shape — no thread at all:** if your transport offers a
non-blocking read (e.g. ZMQ `dontwait`), you can skip the background thread and
drain directly in `onPoll()` with a bounded loop (cap it, e.g. ≤100 messages per
poll, so a burst can't starve the host thread). Use the background-thread+buffer
pattern above when reads block or arrive faster than the poll cadence.

## Capabilities

Return them from `capabilities()` (raw base) or `extraCapabilities()` (`*SourceBase`
OR-adds them to the family default). Common flags:
`kCapabilityFiniteImport`, `kCapabilityContinuousStream`, `kCapabilityDirectIngest`,
`kCapabilityDelegatedIngest`, `kCapabilitySupportsPause`, `kCapabilityHasDialog`,
`kCapabilityPerTopicPause`. Declare only what you implement — flags gate what the
host lets you do. `kCapabilityPerTopicPause` in particular is not just a flag: it
also requires advertising available topics and implementing the topic-subscription
extension (`pluginExtension(PJ_TOPIC_SUBSCRIPTION_EXTENSION_V1)` returning a live
`PJ_topic_subscription_v1_t`) — see the per-topic-pause section of
`pj_plugins/docs/data-source-guide.md`.

## Traps specific to DataSource

- **Background threads never touch the host.** Buffer in plugin memory with
  `PJ::sdk::DrainQueue` / `LatestValueSlot`; flush in `onPoll()`/`poll()`. Calling
  `writeHost()` from your I/O thread races the host and crashes.
- **`onStop()` must be idempotent** — it can be called more than once. Null out
  handles after closing.
- **Streaming loses data if you block in `onPoll()`.** `onPoll()` runs at the
  host's cadence; do the receiving on your own thread and use `onPoll()` only as
  the hand-off point.
- **`FileSourceBase` calls `progressFinish()` for you.** Do not call it yourself
  from `importData()`; a manual `DataSourcePluginBase` must call it itself.
- **Notify state transitions.** With the raw `DataSourcePluginBase`, call
  `runtimeHost().notifyState(state)` on every transition you make, and never
  `resume()` from a terminal (`stopped`/`failed`) state — the host makes a new
  instance instead.
- **Timestamps: ns since epoch** (see SKILL.md rule 3). A row counter is not a
  timestamp.

## Embedding a configuration dialog

Make the dialog a member and expose it via `getDialog()` returning
`PJ::borrowDialog(dialog_)`; add `kCapabilityHasDialog`; emit both macros
(`PJ_DATA_SOURCE_PLUGIN` and `PJ_DIALOG_PLUGIN`) in the same file. The source reads
the dialog member's state directly. The borrowed dialog must not outlive the
source. See `references/dialog.md` and `pj_plugins/examples/mock_source_with_dialog.cpp`.
