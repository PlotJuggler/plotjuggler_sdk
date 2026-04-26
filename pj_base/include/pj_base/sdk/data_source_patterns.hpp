/**
 * @file data_source_patterns.hpp
 * @brief **Start here** for most DataSource plugins.
 *
 * Two base classes covering the dominant DataSource patterns:
 *
 *  - **FileSourceBase** — one-shot file/snapshot importers.
 *    Override: extraCapabilities(), importData(), loadConfig/saveConfig.
 *
 *  - **StreamSourceBase** — long-lived streaming sources.
 *    Override: extraCapabilities(), onStart(), onPoll(), onStop().
 *
 * Both manage the lifecycle state machine automatically — derived classes
 * only implement the domain-specific work.
 *
 * Minimal file-importer plugin (complete):
 * @code
 *   #include <pj_base/sdk/data_source_patterns.hpp>
 *   class MyImporter : public PJ::FileSourceBase {
 *    public:
 *     uint64_t extraCapabilities() const override { return PJ::kCapabilityDirectIngest; }
 *     PJ::Status importData() override {
 *       auto topic = writeHost().ensureTopic("my/data");
 *       if (!topic) return PJ::unexpected(topic.error());
 *       // ... appendRecord() calls ...
 *       return PJ::okStatus();
 *     }
 *   };
 *   PJ_DATA_SOURCE_PLUGIN(MyImporter, R"({"id":"my-importer","name":"My Importer","version":"1.0.0"})")
 * @endcode
 *
 * @see examples/sdk_consumer/minimal_data_source.cpp for the smallest possible plugin.
 * @see pj_plugins/examples/mock_file_source.cpp for a FileSourceBase with progress.
 * @see pj_plugins/examples/mock_source_with_dialog.cpp for StreamSourceBase + Dialog.
 */
#pragma once

#include "pj_base/sdk/data_source_plugin_base.hpp"

namespace PJ {

/**
 * Base class for one-shot file/snapshot importers.
 *
 * Manages the full lifecycle state machine. The derived class implements
 * importData() — all file I/O, parsing, and write-host calls happen there.
 *
 * ## Config convention
 *
 * The host passes configuration via loadConfig() as a JSON string.
 * By convention, file importers receive an object containing a `"filepath"` key:
 *
 * @code
 *   // In loadConfig():
 *   auto cfg = nlohmann::json::parse(config_json, nullptr, false);
 *   if (cfg.is_discarded()) return PJ::unexpected("invalid config JSON");
 *   filepath_ = cfg.value("filepath", std::string{});
 *
 *   // In saveConfig():
 *   return nlohmann::json{{"filepath", filepath_}}.dump();
 * @endcode
 *
 * The host uses `"file_extensions"` from the manifest JSON to build
 * file-dialog filters (e.g. `[".csv", ".tsv"]`).
 */
class FileSourceBase : public DataSourcePluginBase {
 public:
  uint64_t capabilities() const final {
    return kCapabilityFiniteImport | extraCapabilities();
  }

  /// Return additional capability flags (e.g. kCapabilityDirectIngest).
  virtual uint64_t extraCapabilities() const = 0;

  /// Implement this to do the actual import work.
  /// writeHost() and runtimeHost() are available.
  virtual Status importData() = 0;

  Status start() final {
    state_ = DataSourceState::kStarting;
    runtimeHost().notifyState(state_);

    auto status = importData();
    runtimeHost().progressFinish();  // safe no-op if no progress was started
    if (!status) {
      state_ = DataSourceState::kFailed;
      runtimeHost().notifyState(state_);
      return status;
    }

    state_ = DataSourceState::kStopped;
    runtimeHost().notifyState(state_);
    runtimeHost().requestStop(DataSourceState::kStopped, "import complete");
    return okStatus();
  }

  void stop() final {
    state_ = DataSourceState::kStopped;
  }

  DataSourceState currentState() const final {
    return state_;
  }

 private:
  DataSourceState state_ = DataSourceState::kIdle;
};

/**
 * Base class for long-lived streaming sources.
 *
 * Manages the state machine; derived class implements connection, polling,
 * and teardown.
 *
 * Pause/resume are NOT wired by this class. Derived classes that want pause
 * support should override pause()/resume() directly (from
 * DataSourcePluginBase) and add kCapabilitySupportsPause to
 * extraCapabilities().
 */
class StreamSourceBase : public DataSourcePluginBase {
 public:
  uint64_t capabilities() const final {
    return kCapabilityContinuousStream | extraCapabilities();
  }

  /// Return additional capability flags (e.g. kCapabilityDirectIngest).
  virtual uint64_t extraCapabilities() const = 0;

  /// Called from start(). Open connections, allocate resources.
  virtual Status onStart() = 0;

  /// Called periodically by the host's polling thread.
  /// MUST NOT BLOCK — drain buffered data and return immediately.
  /// Do not call recv(), read(), or any syscall that may wait.
  /// If your source has a receive thread, swap-drain a buffer here.
  /// Host methods (appendRecord, pushRawMessage) may only be called from this method.
  virtual Status onPoll() = 0;

  /// Called from stop(). Close connections, free resources.
  /// Must be idempotent.
  virtual void onStop() = 0;

  Status start() final {
    state_ = DataSourceState::kStarting;
    runtimeHost().notifyState(state_);

    auto status = onStart();
    if (!status) {
      state_ = DataSourceState::kFailed;
      runtimeHost().notifyState(state_);
      return status;
    }

    state_ = DataSourceState::kRunning;
    runtimeHost().notifyState(state_);
    return okStatus();
  }

  Status poll() final {
    return onPoll();
  }

  void stop() final {
    onStop();
    state_ = DataSourceState::kStopped;
    runtimeHost().notifyState(state_);
  }

  DataSourceState currentState() const final {
    return state_;
  }

 private:
  DataSourceState state_ = DataSourceState::kIdle;
};

}  // namespace PJ
