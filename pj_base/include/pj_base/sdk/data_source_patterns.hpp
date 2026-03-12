/**
 * @file data_source_patterns.hpp
 * @brief Derived base classes for the two dominant DataSource patterns.
 *
 * FileSourceBase — for one-shot file/snapshot importers.
 * StreamSourceBase — for long-lived streaming sources.
 *
 * Both manage the lifecycle state machine so that derived classes only need
 * to implement the domain-specific work.
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
 * The host passes configuration via loadConfig(). By convention, file
 * importers receive a JSON object containing a "filepath" key. The derived
 * class should extract this in its loadConfig() override and preserve it
 * in saveConfig().
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

  void stop() final { state_ = DataSourceState::kStopped; }

  DataSourceState currentState() const final { return state_; }

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

  /// Called from poll(). Read available data, write to writeHost().
  /// Must not block — drain what is available and return.
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

  Status poll() final { return onPoll(); }

  void stop() final {
    onStop();
    state_ = DataSourceState::kStopped;
    runtimeHost().notifyState(state_);
  }

  DataSourceState currentState() const final { return state_; }

 private:
  DataSourceState state_ = DataSourceState::kIdle;
};

}  // namespace PJ
