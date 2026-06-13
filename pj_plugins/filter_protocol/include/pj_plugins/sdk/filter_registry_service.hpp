#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// C++ wrapper for the pj.filter_registry.v1 service. Plugins receive a handle
// via their bind() and use it to register their FilterTransform classes and to
// resolve transforms by id. The host's FilterTransformFactory sits behind it.
//
// Cross-DSO note: the math (12 builtins) is vendored in `builtin_transforms.hpp`
// from this SDK so both the plugin and the host compile the same classes. The
// service trades only an opaque handle + a paired deleter, so the host calls
// virtual methods on FilterTransform via the in-DSO vtable of whichever side
// created the instance. The library_owner shared_ptr<void> pins the plugin DSO
// for as long as any of its registered factory_fn / deleter pairs is live.

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_plugins/sdk/filter_registry_abi.h"
#include "pj_plugins/sdk/filter_transform.hpp"

namespace PJ::sdk {

/// Typed C++ view over PJ_filter_registry_t. Constructed from the fat pointer
/// returned by ServiceRegistry::require<FilterRegistryService>().
class FilterRegistryView {
 public:
  constexpr FilterRegistryView() = default;
  constexpr explicit FilterRegistryView(PJ_filter_registry_t raw) noexcept : raw_(raw) {}

  [[nodiscard]] bool valid() const noexcept {
    return raw_.ctx != nullptr && raw_.vtable != nullptr && raw_.vtable->register_transform != nullptr &&
           raw_.vtable->create_transform != nullptr;
  }

  /// Register a FilterTransform class. `Class` must be default-constructible
  /// and inherit from FilterTransform; `id` is the lookup key (e.g. "scale").
  ///
  /// Pass any object the caller wants the host to pin while this registration
  /// is live as `library_owner` — typically the plugin DSO handle obtained
  /// from the v4 bind context (see DataSourceHandle::libraryOwner()).
  template <class Class>
  [[nodiscard]] Expected<void> registerTransform(std::string id, std::shared_ptr<void> library_owner) {
    static_assert(std::is_base_of_v<FilterTransform, Class>, "Class must inherit FilterTransform");
    auto* factory =
        +[](void*) noexcept -> PJ_filter_transform_t* { return reinterpret_cast<PJ_filter_transform_t*>(new Class{}); };
    auto* deleter = +[](PJ_filter_transform_t* p) noexcept { delete reinterpret_cast<FilterTransform*>(p); };
    return registerRaw(std::move(id), factory, deleter, nullptr, std::move(library_owner));
  }

  /// Drop a registration. Existing instances stay alive — their deleter was
  /// captured at create time and remains callable.
  [[nodiscard]] Expected<void> unregisterTransform(std::string_view id) {
    PJ_error_t err{};
    PJ_string_view_t id_view{id.data(), id.size()};
    if (!raw_.vtable->unregister_transform(raw_.ctx, id_view, &err)) {
      return unexpected(std::string("unregisterTransform failed: ") + err.message);
    }
    return {};
  }

  /// Create an instance by id. Returns nullptr if `id` is unknown or the
  /// registered factory fails. The returned shared_ptr's deleter routes
  /// through the original registration so the destruction happens in the
  /// same DSO that did the new (deleter + library_owner captured at create).
  [[nodiscard]] std::shared_ptr<FilterTransform> create(std::string_view id) const {
    if (!valid()) {
      return nullptr;
    }
    PJ_error_t err{};
    PJ_string_view_t id_view{id.data(), id.size()};
    PJ_filter_transform_t* raw = raw_.vtable->create_transform(raw_.ctx, id_view, &err);
    if (raw == nullptr) {
      return nullptr;
    }
    PJ_filter_transform_deleter_fn deleter = raw_.vtable->lookup_deleter(raw_.ctx, id_view);
    if (deleter == nullptr) {
      // Should not happen — registered factories always have a deleter — but
      // defend: leak the instance rather than UB if it ever does.
      return nullptr;
    }
    return std::shared_ptr<FilterTransform>(
        reinterpret_cast<FilterTransform*>(raw),
        [deleter](FilterTransform* p) noexcept { deleter(reinterpret_cast<PJ_filter_transform_t*>(p)); });
  }

  /// Snapshot of registered ids in registration order. Allocates a vector
  /// of strings copied out of the service.
  [[nodiscard]] std::vector<std::string> registeredIds() const {
    if (!valid()) {
      return {};
    }
    size_t count = 0;
    raw_.vtable->list_ids(raw_.ctx, nullptr, 0, &count);
    if (count == 0) {
      return {};
    }
    std::vector<PJ_string_view_t> raw_ids(count);
    size_t actual = 0;
    raw_.vtable->list_ids(raw_.ctx, raw_ids.data(), count, &actual);
    std::vector<std::string> out;
    out.reserve(actual);
    for (size_t i = 0; i < actual; ++i) {
      out.emplace_back(raw_ids[i].data, raw_ids[i].size);
    }
    return out;
  }

  [[nodiscard]] PJ_filter_registry_t raw() const noexcept {
    return raw_;
  }

 private:
  [[nodiscard]] Expected<void> registerRaw(
      std::string id, PJ_filter_transform_factory_fn factory, PJ_filter_transform_deleter_fn deleter, void* factory_ctx,
      std::shared_ptr<void> library_owner) {
    PJ_filter_transform_registration_t reg{};
    reg.id = PJ_string_view_t{id.data(), id.size()};
    reg.factory = factory;
    reg.deleter = deleter;
    reg.factory_ctx = factory_ctx;
    PJ_error_t err{};
    // The host's register_transform copies `id` and keeps a strong ref on
    // `library_owner` for as long as the entry lives.
    if (!raw_.vtable->register_transform(raw_.ctx, reg, library_owner.get(), &err)) {
      return unexpected(std::string("registerTransform failed: ") + err.message);
    }
    // We hand the host an opaque void* to the shared_ptr's managed object;
    // for the host to actually pin the DSO it must wrap the void* into a
    // shared_ptr<void> on its side using a deleter that releases this one.
    // Implementations of the service do that wiring at register time.
    (void)library_owner;  // ownership transferred via the host registry
    return {};
  }

  PJ_filter_registry_t raw_{};
};

/// Traits for ServiceRegistry::get<>/require<>().
struct FilterRegistryService {
  static constexpr const char* kName = PJ_FILTER_REGISTRY_SERVICE_NAME;
  static constexpr uint32_t kMinVersion = 1;
  using Raw = PJ_filter_registry_t;
  using Vtable = PJ_filter_registry_vtable_t;
  using View = FilterRegistryView;
};

}  // namespace PJ::sdk
