#include <pj_base/sdk/data_source_patterns.hpp>

#include <string>

namespace {

class MockFileSource : public PJ::FileSourceBase {
 public:
  uint64_t extraCapabilities() const override { return PJ::kCapabilityDirectIngest; }

  std::string saveConfig() const override { return config_; }

  PJ::Status loadConfig(std::string_view config_json) override {
    config_ = std::string(config_json);
    return PJ::okStatus();
  }

  PJ::Status importData() override {
    if (!runtimeHost().progressStart("Importing", 3, true)) {
      return PJ::unexpected(std::string("progress unavailable"));
    }

    auto topic = writeHost().ensureTopic("mock/file_data");
    if (!topic) {
      return PJ::unexpected(topic.error());
    }

    for (uint64_t i = 1; i <= 3; ++i) {
      if (runtimeHost().isStopRequested()) {
        return PJ::unexpected(std::string("import cancelled"));
      }

      auto status = writeHost().appendRecord(
          *topic, PJ::Timestamp{static_cast<int64_t>(i * 100)},
          {{.name = "value", .value = static_cast<double>(i) * 10.0}});
      if (!status) {
        return PJ::unexpected(status.error());
      }

      if (!runtimeHost().progressUpdate(i)) {
        return PJ::unexpected(std::string("import cancelled via progress"));
      }
    }

    runtimeHost().reportMessage(PJ::DataSourceMessageLevel::kInfo, "imported 3 records");
    return PJ::okStatus();
  }

 private:
  std::string config_ = "{}";
};

}  // namespace

PJ_DATA_SOURCE_PLUGIN(MockFileSource,
                      R"({"name":"Mock File Source","version":"1.0.0",)"
                      R"("description":"Test FileSourceBase lifecycle and progress",)"
                      R"("file_extensions":[".mock"]})")
