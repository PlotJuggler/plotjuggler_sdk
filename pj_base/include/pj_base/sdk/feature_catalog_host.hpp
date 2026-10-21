#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Host-side helper for providing the "pj.feature_catalog.v1" service (see
// `FeatureCatalogView` in plugin_data_api.hpp for the plugin-facing side). A
// host supplies a provider returning the catalog JSON and registers
// `FeatureCatalogHost(provider).view()` into the plugin service registry.
// Header-only, Qt-free.

#include <exception>
#include <functional>
#include <string>
#include <utility>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ::sdk {

/// Adapts a JSON provider to the C ABI `PJ_feature_catalog_t`. The provider is
/// called on every slot call (the host decides what to cache). Owns the scratch
/// string backing the "valid until the next call" contract, so it must outlive
/// every plugin bound to its `view()`. Main-thread use.
class FeatureCatalogHost {
 public:
  using Provider = std::function<std::string()>;

  explicit FeatureCatalogHost(Provider provider) : provider_(std::move(provider)) {}

  [[nodiscard]] PJ_feature_catalog_t view() noexcept {
    static constexpr PJ_feature_catalog_vtable_t kVtable = {
        PJ_PLUGIN_DATA_API_VERSION,
        sizeof(PJ_feature_catalog_vtable_t),
        &FeatureCatalogHost::tGetFeatureCatalog,
    };
    return PJ_feature_catalog_t{this, &kVtable};
  }

 private:
  // C ABI trampoline: cast ctx, call the provider, translate C++ exceptions to
  // PJ_error_t so nothing propagates across the ABI.
  static bool tGetFeatureCatalog(void* ctx, PJ_string_view_t* out_json, PJ_error_t* out_error) noexcept {
    if (ctx == nullptr || out_json == nullptr) {
      fillError(out_error, 2, "feature_catalog", "null ctx or out_json");
      return false;
    }
    auto* self = static_cast<FeatureCatalogHost*>(ctx);
    try {
      self->scratch_ = self->provider_();
      *out_json = toAbiString(self->scratch_);
      return true;
    } catch (const std::exception& e) {
      fillError(out_error, 1, "feature_catalog", std::string("provider threw: ") + e.what());
      return false;
    } catch (...) {
      fillError(out_error, 1, "feature_catalog", "provider threw unknown exception");
      return false;
    }
  }

  Provider provider_;
  std::string scratch_;
};

}  // namespace PJ::sdk
