#include <pj_base/sdk/toolbox_plugin_base.hpp>
#include <string>
#include <string_view>

namespace pj_mock {
/// Canonical id for the diagnostics extension exposed by this mock —
/// shared with the corresponding test. Experimental namespace per the v3
/// service-naming rule.
inline constexpr std::string_view kMockDiagnosticsExtensionId = "pj.experimental.mock_diagnostics/draft-1";
}  // namespace pj_mock

namespace {

class MockToolbox : public PJ::ToolboxPluginBase {
 public:
  uint64_t capabilities() const override {
    return 0;  // no dialog — this mock exercises the data plane only
  }

  std::string saveConfig() const override {
    return config_;
  }

  PJ::Status loadConfig(std::string_view config_json) override {
    config_ = std::string(config_json);
    if (toolboxHostBound() && runtimeHostBound() && config_.find("apply_transform") != std::string::npos) {
      applyTransform();
    }
    return PJ::okStatus();
  }

  void onDataChanged() override {
    ++data_changed_count_;
    ++diagnostics_.data_changed_count;
    if (runtimeHostBound()) {
      runtimeHost().notifyDataChanged();
    }
  }

  /// Exercise the E2 plugin-extension path by exposing a tiny diagnostics
  /// POD under the experimental namespace. Hosts that know the id can cast
  /// the returned pointer to read this plugin's diagnostic counters.
  const void* pluginExtension(std::string_view id) override {
    if (id == pj_mock::kMockDiagnosticsExtensionId) {
      return &diagnostics_;
    }
    return nullptr;
  }

 private:
  struct Diagnostics {
    int data_changed_count;
  };
  Diagnostics diagnostics_{0};
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
    auto status = host.appendRecord(*topic, PJ::Timestamp{1000}, PJ::Span<const PJ::sdk::NamedFieldValue>(fields, 1));
    (void)status;

    runtimeHost().notifyDataChanged();
  }

  std::string config_ = "{}";
  int data_changed_count_ = 0;
};

}  // namespace

PJ_TOOLBOX_PLUGIN(
    MockToolbox, R"({"id":"mock-toolbox","name":"Mock Toolbox","version":"1.0.0",)"
                 R"("description":"Test toolbox for protocol and host integration"})")
