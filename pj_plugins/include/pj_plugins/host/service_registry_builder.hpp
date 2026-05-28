#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "pj_base/expected.hpp"
#include "pj_base/plugin_data_api.h"

namespace PJ {

/// Host-side assembler for `PJ_service_registry_t`.
///
/// The host creates a builder, registers named services, and hands the
/// resulting `PJ_service_registry_t` view to each plugin via the v4
/// `bind()` call. The builder owns an internal lookup table; the emitted
/// registry is a thin fat pointer whose lifetime is tied to the builder.
///
/// Thread-safety: builder mutation (registerService) and plugin-side
/// lookup share no locks. Register all services before binding plugins,
/// or serialize access externally if late registration is needed.
class ServiceRegistryBuilder {
 public:
  ServiceRegistryBuilder() = default;
  ServiceRegistryBuilder(const ServiceRegistryBuilder&) = delete;
  ServiceRegistryBuilder& operator=(const ServiceRegistryBuilder&) = delete;
  ServiceRegistryBuilder(ServiceRegistryBuilder&&) = delete;
  ServiceRegistryBuilder& operator=(ServiceRegistryBuilder&&) = delete;
  ~ServiceRegistryBuilder() = default;

  /// Register a service under @p name. Rejects null pointers and duplicate
  /// names (silent overwrite would mask configuration bugs).
  ///
  /// @param name              Canonical service name (e.g. "pj.colormap.v1").
  /// @param protocol_version  The version of the service as implemented here.
  ///                          Consumers may request any version <= this value.
  /// @param service           Fat pointer to the service. The builder does
  ///                          not take ownership of ctx/vtable.
  /// @return `Expected<void>`: ok on success, error string on duplicate or
  ///         null fat-pointer field.
  [[nodiscard]] ::PJ::Status tryRegisterService(
      std::string_view name, uint32_t protocol_version, PJ_service_t service) {
    const std::string service_name(name);
    if (service.ctx == nullptr || service.vtable == nullptr) {
      return ::PJ::unexpected("registerService: null ctx or vtable for '" + service_name + "'");
    }
    std::string key(service_name);
    if (entries_.find(key) != entries_.end()) {
      return ::PJ::unexpected("registerService: duplicate name '" + service_name + "'");
    }
    entries_[std::move(key)] = Entry{protocol_version, service};
    return {};
  }

  /// Non-returning convenience overload for callers that know the inputs are
  /// valid (mocks, tests). Asserts in debug builds; no-op on failure in
  /// release (i.e. do NOT rely on this for untrusted inputs — use
  /// tryRegisterService instead).
  void registerService(std::string_view name, uint32_t protocol_version, PJ_service_t service) {
    auto status = tryRegisterService(name, protocol_version, service);
    (void)status;
  }

  /// Typed overload using a service-traits class (see sdk/service_traits.hpp).
  /// The traits provide the canonical name and a default protocol version.
  template <class Traits>
  void registerService(typename Traits::Raw service) {
    registerService(
        Traits::kName, Traits::kMinVersion, PJ_service_t{service.ctx, static_cast<const void*>(service.vtable)});
  }

  /// Remove a service by name. Silently does nothing if not present.
  void unregisterService(std::string_view name) {
    entries_.erase(std::string(name));
  }

  /// Return a fat pointer that plugins can pass through the v4 `bind()`.
  /// The returned pointer is valid as long as the builder instance lives.
  [[nodiscard]] PJ_service_registry_t view() noexcept {
    return PJ_service_registry_t{this, &kVtable};
  }

  /// Count of currently registered services — useful for host-side tests.
  [[nodiscard]] std::size_t size() const noexcept {
    return entries_.size();
  }

 private:
  struct Entry {
    uint32_t protocol_version;
    PJ_service_t service;
  };

  static bool dispatchGetService(
      void* ctx, PJ_string_view_t name, uint32_t min_version, PJ_service_t* out_service,
      PJ_error_t* out_error) noexcept {
    auto* self = static_cast<ServiceRegistryBuilder*>(ctx);
    if (out_service == nullptr) {
      if (out_error != nullptr) {
        *out_error = makeError(3, "out_service pointer is null");
      }
      return false;
    }
    std::string key(name.data == nullptr ? "" : name.data, name.size);
    auto it = self->entries_.find(key);
    if (it == self->entries_.end()) {
      if (out_error != nullptr) {
        *out_error = makeError(1, "unknown service name");
      }
      return false;
    }
    if (it->second.protocol_version < min_version) {
      if (out_error != nullptr) {
        *out_error = makeError(2, "registered service version is lower than requested minimum");
      }
      return false;
    }
    if (it->second.service.ctx == nullptr || it->second.service.vtable == nullptr) {
      if (out_error != nullptr) {
        *out_error = makeError(4, "registered service has null ctx or vtable");
      }
      return false;
    }
    *out_service = it->second.service;
    return true;
  }

  static PJ_error_t makeError(int32_t code, const char* message) noexcept {
    PJ_error_t err{};
    err.code = code;
    writeField(err.domain, sizeof(err.domain), "registry");
    writeField(err.message, sizeof(err.message), message);
    return err;
  }

  static void writeField(char* dest, std::size_t dest_size, const char* src) noexcept {
    if (dest == nullptr || dest_size == 0) {
      return;
    }
    std::size_t n = std::strlen(src);
    if (n >= dest_size) {
      n = dest_size - 1;
    }
    std::memcpy(dest, src, n);
    dest[n] = '\0';
  }

  // ReSharper disable once CppDeclaratorNeverUsed — linked into constexpr kVtable
  static constexpr PJ_service_registry_vtable_t kVtable = {
      /* protocol_version = */ 1,
      /* struct_size      = */ sizeof(PJ_service_registry_vtable_t),
      /* get_service      = */ &ServiceRegistryBuilder::dispatchGetService,
  };

  std::unordered_map<std::string, Entry> entries_;
};

}  // namespace PJ
