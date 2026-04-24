#pragma once

#include <cstring>
#include <optional>
#include <string>

#include "pj_base/expected.hpp"
#include "pj_base/plugin_data_api.h"

namespace PJ::sdk {

// Forward declare the fillError / errorToString helpers defined in
// plugin_data_api.hpp. Using raw strncpy here to avoid the dependency cycle.

/// Typed C++ wrapper around `PJ_service_registry_t`.
///
/// Plugins receive a registry via their v3 `bind()` virtual. Two lookup
/// styles:
///   - `get<Traits>()` — `std::optional<View>`; miss yields `nullopt`.
///   - `require<Traits>()` — `Expected<View>`; miss yields an error string.
///
/// The underlying `PJ_service_registry_t` is owned by the host and must
/// outlive any plugin that caches it. Hosts typically keep the registry
/// alive for the entire plugin-session lifetime.
class ServiceRegistry {
 public:
  constexpr ServiceRegistry() = default;
  constexpr explicit ServiceRegistry(PJ_service_registry_t raw) noexcept : raw_(raw) {}

  [[nodiscard]] bool valid() const noexcept {
    return raw_.vtable != nullptr && raw_.ctx != nullptr && raw_.vtable->get_service != nullptr;
  }

  [[nodiscard]] PJ_service_registry_t raw() const noexcept {
    return raw_;
  }

  /// Optional lookup.
  template <class Traits>
  [[nodiscard]] std::optional<typename Traits::View> get() const {
    PJ_service_t svc{};
    if (!lookup(Traits::kName, Traits::kMinVersion, svc, nullptr)) {
      return std::nullopt;
    }
    if (!validateService(svc)) {
      return std::nullopt;
    }
    return makeView<Traits>(svc);
  }

  /// Required lookup.
  template <class Traits>
  [[nodiscard]] Expected<typename Traits::View> require() const {
    PJ_service_t svc{};
    PJ_error_t err{};
    if (!lookup(Traits::kName, Traits::kMinVersion, svc, &err)) {
      std::string msg = "service unavailable: ";
      msg.append(Traits::kName);
      if (err.message[0] != '\0') {
        msg.append(" (");
        msg.append(err.message);
        msg.append(")");
      }
      return unexpected(std::move(msg));
    }
    if (!validateService(svc)) {
      std::string msg = "service returned invalid fat pointer: ";
      msg.append(Traits::kName);
      return unexpected(std::move(msg));
    }
    return makeView<Traits>(svc);
  }

 private:
  PJ_service_registry_t raw_{};

  static void writeField(char* dest, std::size_t dest_size, const char* src) {
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

  [[nodiscard]] bool lookup(
      const char* name, uint32_t min_version, PJ_service_t& out_service, PJ_error_t* out_error) const {
    if (!valid()) {
      if (out_error != nullptr) {
        out_error->code = 1;
        writeField(out_error->domain, sizeof(out_error->domain), "registry");
        writeField(out_error->message, sizeof(out_error->message), "service registry not bound");
      }
      return false;
    }
    PJ_string_view_t sv{name, std::strlen(name)};
    return raw_.vtable->get_service(raw_.ctx, sv, min_version, &out_service, out_error);
  }

  /// Validate a freshly-looked-up service: must have both ctx and vtable
  /// non-null. Ensures `require()` refuses silently-broken registrations.
  static bool validateService(const PJ_service_t& svc) noexcept {
    return svc.ctx != nullptr && svc.vtable != nullptr;
  }

  template <class Traits>
  [[nodiscard]] static typename Traits::View makeView(PJ_service_t svc) {
    typename Traits::Raw fat{};
    fat.ctx = svc.ctx;
    fat.vtable = static_cast<const typename Traits::Vtable*>(svc.vtable);
    return typename Traits::View{fat};
  }
};

}  // namespace PJ::sdk
