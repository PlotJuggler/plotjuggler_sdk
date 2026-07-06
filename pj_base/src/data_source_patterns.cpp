// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/data_source_patterns.hpp"

namespace PJ {

Status FileSourceBase::start() {
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

Status StreamSourceBase::start() {
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

void StreamSourceBase::stop() {
  onStop();
  state_ = DataSourceState::kStopped;
  runtimeHost().notifyState(state_);
}

}  // namespace PJ
