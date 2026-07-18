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
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

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
    std::vector<Sample> batch;
    { std::lock_guard lk(mu_); batch.swap(buffer_); }
    auto topic = writeHost().ensureTopic("udp/data");
    if (!topic) return PJ::unexpected(topic.error());
    for (auto& s : batch) {
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
  }

 private:
  struct Sample { PJ::Timestamp timestamp_ns; double value; };
  void recvLoop() {
    while (running_.load()) {
      Sample s = receiveOne(fd_);       // do NOT call writeHost() here
      std::lock_guard lk(mu_); buffer_.push_back(s);
    }
  }
  int fd_ = -1; std::string config_ = "{}";
  std::atomic<bool> running_{false}; std::thread io_; std::mutex mu_;
  std::vector<Sample> buffer_;
};

PJ_DATA_SOURCE_PLUGIN(MyUdpSource, R"({"id":"my-udp","name":"My UDP","version":"1.0.0"})")
```

For **delegated** ingest (let a MessageParser decode the bytes): declare
`kCapabilityDelegatedIngest`, in `onStart()` call
`runtimeHost().ensureParserBinding({.topic_name=..., .parser_encoding="json", ...})`,
and in `onPoll()` push raw payloads with
`runtimeHost().pushMessage(*binding, ts, [bytes = std::move(bytes)] { return bytes; })`.
The fetch callable must capture its payload **by value** and be idempotent — the
host may invoke it zero, one, or many times, possibly from consumer threads, and
releases it exactly once even when the push fails. The config-envelope that ties
source⇆parser is managed by the host; you never see the parser directly.
Two delegated-ingest traps:

- **Forward `_parser_config`.** The host injects the parser's saved options into
  *your* `loadConfig()` JSON under the key `"_parser_config"`. Extract that string
  and pass it as `ParserBindingRequest.parser_config_json` — otherwise
  schema-based parsers bind unconfigured and silently drop every message.
- **Binding-unavailable is not an error.** If `ensureParserBinding` fails (e.g. no
  parser installed for that encoding yet), skip the message and retry on a later
  poll; only a failed `pushMessage` on an established binding is a real error.

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

- **Background threads never touch the host.** Buffer in plugin memory under a
  mutex; flush in `onPoll()`/`poll()`. Calling `writeHost()` from your I/O thread
  races the host and crashes.
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
