// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Fixture for plugin_cmake_helpers_test: a DataSource plus a Dialog whose
// manifest and .ui XML come from headers generated at configure time by
// pj_configure_plugin(MANIFEST_HEADER ...) and pj_embed_file(). The same
// source is also built through the deprecated pj_emit_plugin_manifest() alias.

#include <pj_base/sdk/data_source_patterns.hpp>
#include <pj_plugins/sdk/dialog_plugin_typed.hpp>
#include <pj_plugins/sdk/widget_data.hpp>
#include <string>
#include <string_view>

#include "cmake_helpers_fixture_dialog_ui.hpp"  // generated: kFixtureDialogUi
#include "cmake_helpers_fixture_manifest.hpp"   // generated: kFixtureManifest

#if defined(_WIN32)
#define PJ_FIXTURE_EXPORT __declspec(dllexport)
#else
#define PJ_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

// Vague-linkage static with default visibility: GCC emits it as STB_GNU_UNIQUE.
// Without an export allowlist it leaks into .dynsym as a 'u' symbol, which is
// exactly what the gate's negative test looks for on the un-hardened build.
template <class T>
struct PJ_FIXTURE_EXPORT UniqueHolder {
  static int value;
};
template <class T>
int UniqueHolder<T>::value = 0;

class FixtureDialog : public PJ::DialogPluginTyped {
 public:
  std::string manifest() const override {
    return kFixtureManifest;
  }
  std::string ui_content() const override {
    return kFixtureDialogUi;
  }
  std::string widget_data() override {
    PJ::WidgetData wd;
    wd.setText("label_input", label_);
    return wd.toJson();
  }
  bool onTextChanged(std::string_view widget_name, std::string_view text) override {
    if (widget_name == "label_input") {
      label_ = std::string(text);
    }
    return false;
  }

 private:
  std::string label_ = "fixture";
};

class FixtureSource : public PJ::FileSourceBase {
 public:
  uint64_t extraCapabilities() const override {
    return PJ::kCapabilityDirectIngest;
  }
  PJ::Status importData() override {
    return PJ::okStatus();
  }
};

}  // namespace

// Default-visibility symbol that only the version-script allowlist can
// localize; plugin_cmake_helpers_test dlsym()s it to prove the allowlist held.
extern "C" PJ_FIXTURE_EXPORT int pj_cmake_fixture_probe() {
  return ++UniqueHolder<int>::value;
}

PJ_DATA_SOURCE_PLUGIN(FixtureSource, kFixtureManifest)
PJ_DIALOG_PLUGIN(FixtureDialog, kFixtureManifest)
