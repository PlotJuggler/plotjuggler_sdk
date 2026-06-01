// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <pj_base/sdk/data_source_plugin_base.hpp>
#include <string>

namespace {

class MockDataSource : public PJ::DataSourcePluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kCapabilityContinuousStream | PJ::kCapabilityDirectIngest | PJ::kCapabilityDelegatedIngest |
           PJ::kCapabilitySupportsPause;
  }

  std::string saveConfig() const override {
    return config_;
  }

  PJ::Status loadConfig(std::string_view config_json) override {
    config_ = std::string(config_json);
    return PJ::okStatus();
  }

  PJ::Status start() override {
    if (!writeHostBound()) {
      state_ = PJ::DataSourceState::kFailed;
      return PJ::unexpected("write host not bound");
    }
    if (!runtimeHostBound()) {
      state_ = PJ::DataSourceState::kFailed;
      return PJ::unexpected("runtime host not bound");
    }

    state_ = PJ::DataSourceState::kStarting;
    runtimeHost().notifyState(state_);
    runtimeHost().reportMessage(PJ::DataSourceMessageLevel::kInfo, "mock start");

    if (config_.find("progress") != std::string::npos) {
      if (runtimeHost().progressStart("Mock Import", 3, true)) {
        for (uint64_t step = 1; step <= 3; ++step) {
          if (!runtimeHost().progressUpdate(step)) {
            runtimeHost().progressFinish();
            state_ = PJ::DataSourceState::kFailed;
            runtimeHost().notifyState(state_);
            return PJ::unexpected("progress canceled");
          }
        }
        runtimeHost().progressFinish();
      }
    }

    auto topic = writeHost().ensureTopic("mock/topic");
    if (!topic) {
      state_ = PJ::DataSourceState::kFailed;
      runtimeHost().notifyState(state_);
      return PJ::unexpected(topic.error());
    }

    auto write_status = writeHost().appendRecord(*topic, PJ::Timestamp{123}, {{.name = "value", .value = 42.0}});
    if (!write_status) {
      state_ = PJ::DataSourceState::kFailed;
      runtimeHost().notifyState(state_);
      return PJ::unexpected(write_status.error());
    }

    if (config_.find("delegated") != std::string::npos) {
      const uint8_t schema[] = {'s', 'c', 'h'};
      auto binding = runtimeHost().ensureParserBinding(
          PJ::ParserBindingRequest{
              .topic_name = "mock/topic",
              .parser_encoding = "json",
              .type_name = "mock_type",
              .schema = PJ::Span<const uint8_t>(schema, sizeof(schema)),
              .parser_config_json = R"({"mode":"test"})",
          });
      if (!binding) {
        state_ = PJ::DataSourceState::kFailed;
        runtimeHost().notifyState(state_);
        return PJ::unexpected(binding.error());
      }

      auto push_status =
          runtimeHost().pushMessage(*binding, PJ::Timestamp{456}, []() -> std::vector<uint8_t> { return {'{', '}'}; });
      if (!push_status) {
        state_ = PJ::DataSourceState::kFailed;
        runtimeHost().notifyState(state_);
        return PJ::unexpected(push_status.error());
      }
    }

    state_ = PJ::DataSourceState::kRunning;
    runtimeHost().notifyState(state_);
    return PJ::okStatus();
  }

  void stop() override {
    state_ = PJ::DataSourceState::kStopped;
    runtimeHost().notifyState(state_);
  }

  PJ::Status pause() override {
    if (state_ != PJ::DataSourceState::kRunning) {
      return PJ::unexpected("pause requires running state");
    }
    state_ = PJ::DataSourceState::kPaused;
    runtimeHost().notifyState(state_);
    return PJ::okStatus();
  }

  PJ::Status resume() override {
    if (state_ != PJ::DataSourceState::kPaused) {
      return PJ::unexpected("resume requires paused state");
    }
    state_ = PJ::DataSourceState::kRunning;
    runtimeHost().notifyState(state_);
    return PJ::okStatus();
  }

  PJ::Status poll() override {
    ++poll_count_;
    return PJ::okStatus();
  }

  PJ::DataSourceState currentState() const override {
    return state_;
  }

 private:
  std::string config_ = "{}";
  PJ::DataSourceState state_ = PJ::DataSourceState::kIdle;
  int poll_count_ = 0;
};

}  // namespace

PJ_DATA_SOURCE_PLUGIN(
    MockDataSource, R"({"id":"mock-data-source","name":"Mock DataSource","version":"1.0.0",)"
                    R"("description":"Test data source for protocol and host integration"})")
