// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/data_source_plugin_base.hpp"

namespace PJ {

Status DataSourcePluginBase::bind(sdk::ServiceRegistry services) {
  auto write = services.require<sdk::SourceWriteHostService>();
  if (!write) {
    return unexpected(std::move(write).error());
  }
  write_host_view_ = *write;

  auto runtime = services.require<sdk::DataSourceRuntimeHostService>();
  if (!runtime) {
    return unexpected(std::move(runtime).error());
  }
  runtime_host_view_ = *runtime;

  if (auto object_write = services.get<sdk::SourceObjectWriteHostService>()) {
    object_write_host_view_ = *object_write;
  }

  service_registry_ = services;
  return okStatus();
}

}  // namespace PJ
