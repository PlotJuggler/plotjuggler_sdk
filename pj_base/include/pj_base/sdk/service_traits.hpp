#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <string_view>

#include "pj_base/data_source_protocol.h"
#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/data_source_host_views.hpp"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ::sdk {

/// Traits mapping canonical service names to their ABI vtable types and
/// corresponding C++ view wrappers. Each trait gives ServiceRegistry a
/// typed path from `get_service("name")` to `View{fat_pointer}`.
///
/// Naming rule (enforced at compile time by `isValidServiceName` below):
///
///   Stable:       "pj.<name>.v<N>"                      e.g. "pj.source_write.v1"
///   Experimental: "pj.experimental.<name>/draft-<N>"    e.g. "pj.experimental.diagnostics/draft-1"
///
/// Stable services are frozen for at least three releases before deprecation.
/// Experimental services carry no compatibility guarantees — the host may
/// warn, reject, or require a manifest opt-in to use them. When an
/// experimental service graduates, it gets a new stable name and version;
/// both may coexist during a migration window.
///
/// A registered service with a higher vtable protocol_version is still a
/// valid match for a consumer that requests a lower kMinVersion.

namespace detail {

constexpr bool isDigitRun(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

/// Returns true iff @p name matches `"pj.<x>.v<N>"` (stable) or
/// `"pj.experimental.<x>/draft-<N>"` (unstable). Empty components, missing
/// version suffixes, and non-prefixed names all fail.
constexpr bool isValidServiceName(std::string_view name) {
  if (!name.starts_with("pj.")) {
    return false;
  }
  if (name.starts_with("pj.experimental.")) {
    auto slash = name.find('/');
    if (slash == std::string_view::npos) {
      return false;
    }
    auto after = name.substr(slash + 1);
    if (!after.starts_with("draft-")) {
      return false;
    }
    return isDigitRun(after.substr(6));  // strlen("draft-")
  }
  // Stable: must end with ".v<N>".
  auto last_dot = name.rfind('.');
  if (last_dot == std::string_view::npos || last_dot <= 3) {
    return false;
  }
  auto tail = name.substr(last_dot + 1);
  if (tail.size() < 2 || tail[0] != 'v') {
    return false;
  }
  return isDigitRun(tail.substr(1));
}

}  // namespace detail

struct SourceWriteHostService {
  static constexpr const char* kName = "pj.source_write.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_source_write_host_t;
  using Vtable = PJ_source_write_host_vtable_t;
  using View = SourceWriteHostView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

/// Object write host for DataSource plugins — writes into ObjectStore
/// (peer to DataEngine) for topics carrying opaque payloads (markers,
/// images, point clouds, scene primitives). Optional: plugins that
/// publish only scalar data never resolve this.
struct SourceObjectWriteHostService {
  static constexpr const char* kName = "pj.source_object_write.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_object_write_host_t;
  using Vtable = PJ_object_write_host_vtable_t;
  using View = SourceObjectWriteHostView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

struct ParserWriteHostService {
  static constexpr const char* kName = "pj.parser_write.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_parser_write_host_t;
  using Vtable = PJ_parser_write_host_vtable_t;
  using View = ParserWriteHostView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

/// Parser-scoped object write host. Optional: registered by the host only
/// when the parser is bound to a media topic. A media-capable parser
/// resolves both ParserWriteHostService (scalars) and this one (object
/// payload) at bind time and writes both from a single parse() call.
struct ParserObjectWriteHostService {
  static constexpr const char* kName = "pj.parser_object_write.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_parser_object_write_host_t;
  using Vtable = PJ_parser_object_write_host_vtable_t;
  using View = ParserObjectWriteHostView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

/// Object read host for Toolbox plugins — reads from ObjectStore. Optional:
/// toolboxes that consume only scalar data (via ToolboxHostService) never
/// resolve this. Transformer-style toolboxes that process bytes from object
/// topics (object-detection on images, point-cloud filtering, etc.) resolve
/// it alongside the scalar host.
struct ToolboxObjectReadHostService {
  static constexpr const char* kName = "pj.toolbox_object_read.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_object_read_host_t;
  using Vtable = PJ_object_read_host_vtable_t;
  using View = ToolboxObjectReadHostView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

struct ToolboxHostService {
  // "pj.toolbox_write.v1" for symmetry with "pj.source_write.v1" and
  // "pj.parser_write.v1" — this service IS the toolbox write surface
  // (create_data_source / ensure_topic / ensure_field / append_record /
  // acquire_catalog_snapshot / read_series). The C++ trait is named
  // ToolboxHostService for historical reasons (the vtable type is
  // PJ_toolbox_host_t); the canonical service id uses the _write suffix.
  static constexpr const char* kName = "pj.toolbox_write.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_toolbox_host_t;
  using Vtable = PJ_toolbox_host_vtable_t;
  using View = ToolboxHostView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

struct ColorMapRegistryService {
  static constexpr const char* kName = "pj.colormap.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_colormap_registry_t;
  using Vtable = PJ_colormap_registry_vtable_t;
  using View = ColorMapRegistryView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

/// Optional QSettings-like key/value persistence exposed to any plugin family.
/// Host-backed (QSettings in the GUI app, JSON in a headless host); keys are
/// namespaced per plugin by the host.
struct SettingsStoreService {
  static constexpr const char* kName = "pj.settings.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_settings_store_t;
  using Vtable = PJ_settings_store_vtable_t;
  using View = SettingsView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

/// Runtime host exposed to DataSource plugins — progress, diagnostics,
/// state notification, parser binding, modal message boxes.
struct DataSourceRuntimeHostService {
  static constexpr const char* kName = "pj.runtime.v1";
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_data_source_runtime_host_t;
  using Vtable = PJ_data_source_runtime_host_vtable_t;
  using View = ::PJ::DataSourceRuntimeHostView;
  static_assert(detail::isValidServiceName(kName), "kName must match the pj naming rule");
};

}  // namespace PJ::sdk
