// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/toolbox_plugin_base.hpp"

namespace PJ {

Expected<ParserIngestHostView> ToolboxRuntimeHostView::createParserIngest(uint32_t data_source_id) const {
  if (!valid()) {
    return unexpected("toolbox runtime host is not bound");
  }
  if (!PJ_HAS_TAIL_SLOT(PJ_toolbox_runtime_host_vtable_t, host_.vtable, create_parser_ingest)) {
    return unexpected("toolbox runtime host does not support create_parser_ingest (older host)");
  }
  PJ_data_source_runtime_host_t raw{};
  PJ_error_t err{};
  if (!host_.vtable->create_parser_ingest(host_.ctx, data_source_id, &raw, &err)) {
    return unexpected(errorToString(err));
  }
  return ParserIngestHostView{raw};
}

Status ToolboxRuntimeHostView::releaseParserIngest(uint32_t data_source_id) const {
  if (!valid()) {
    return unexpected("toolbox runtime host is not bound");
  }
  if (!PJ_HAS_TAIL_SLOT(PJ_toolbox_runtime_host_vtable_t, host_.vtable, release_parser_ingest)) {
    return unexpected("toolbox runtime host does not support release_parser_ingest (older host)");
  }
  PJ_error_t err{};
  if (!host_.vtable->release_parser_ingest(host_.ctx, data_source_id, &err)) {
    return unexpected(errorToString(err));
  }
  return okStatus();
}

Status ToolboxPluginBase::bind(sdk::ServiceRegistry services) {
  auto host = services.require<sdk::ToolboxHostService>();
  if (!host) {
    return unexpected(std::move(host).error());
  }
  toolbox_host_view_ = *host;

  auto runtime = services.require<sdk::ToolboxRuntimeHostService>();
  if (!runtime) {
    return unexpected(std::move(runtime).error());
  }
  runtime_host_view_ = *runtime;

  // Colormap is optional — acquire opportunistically.
  if (auto cm = services.get<sdk::ColorMapRegistryService>()) {
    colormap_view_ = *cm;
  }

  // Object read is optional — transformer-style toolboxes resolve it.
  if (auto obj = services.get<sdk::ToolboxObjectReadHostService>()) {
    object_read_host_view_ = *obj;
  }

  service_registry_ = services;
  return okStatus();
}

}  // namespace PJ
