#include <pj_base/sdk/data_source_patterns.hpp>

namespace {

class MinimalDataSource : public PJ::FileSourceBase {
 public:
  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest;
  }

  PJ::Status importData() override {
    return PJ::okStatus();
  }
};

}  // namespace

PJ_DATA_SOURCE_PLUGIN(MinimalDataSource, R"({"id":"minimal-data-source","name":"Minimal","version":"0.1.0"})")
