// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <pj_base/sdk/data_source_patterns.hpp>
#include <pj_base/sdk/semver.hpp>
#include <pj_base/sdk/version.hpp>

namespace {

class MinimalDataSource : public PJ::FileSourceBase {
 public:
  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest;
  }

  PJ::Status importData() override {
    if (!PJ::SemVer::isValid(PJ::sdkVersion())) {
      return PJ::unexpected("configured SDK version is not valid SemVer");
    }
    return PJ::okStatus();
  }
};

}  // namespace

PJ_DATA_SOURCE_PLUGIN(MinimalDataSource, R"({"id":"minimal-data-source","name":"Minimal","version":"0.1.0"})")
