#include <pj_base/sdk/toolbox_plugin_base.hpp>
#include <string>

namespace {

class MockToolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return PJ::kToolboxCapabilityHasDialog;
  }

  std::string saveConfig() const override {
    return config_;
  }

  PJ::Status loadConfig(std::string_view config_json) override {
    config_ = std::string(config_json);

    // If config requests a transform, exercise the data-plane
    if (toolboxHostBound() && runtimeHostBound() && config_.find("apply_transform") != std::string::npos) {
      applyTransform();
    }
    return PJ::okStatus();
  }

  void* dialogContext() override {
    return this;
  }

  void onDataChanged() override {
    ++data_changed_count_;
    if (runtimeHostBound()) {
      runtimeHost().notifyDataChanged();
    }
  }

 private:
  void applyTransform() {
    auto host = toolboxHost();

    auto source = host.createDataSource("mock_output");
    if (!source) {
      return;
    }

    auto topic = host.ensureTopic(*source, "transformed");
    if (!topic) {
      return;
    }

    const PJ::sdk::NamedFieldValue fields[] = {{.name = "result", .value = 99.0}};
    auto status = host.appendRecord(*topic, PJ::Timestamp{1000}, PJ::Span(fields));
    (void)status;

    runtimeHost().notifyDataChanged();
  }

  std::string config_ = "{}";
  int data_changed_count_ = 0;
};

}  // namespace

PJ_TOOLBOX_PLUGIN(
    MockToolbox, R"({"name":"Mock Toolbox","version":"1.0.0",)"
                 R"("description":"Test toolbox for protocol and host integration"})")
