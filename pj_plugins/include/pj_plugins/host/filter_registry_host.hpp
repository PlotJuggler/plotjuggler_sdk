// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Host-side adapter: exposes a FilterTransformFactory as the
// pj.filter_registry.v1 C-ABI service so plugins can register their transform
// classes during loaderInit and resolve them later. The factory itself owns
// the entries; this header just wires the C trampolines and shared_ptr<void>
// library_owner plumbing that the cross-DSO contract needs.
//
// Usage (host side):
//   FilterTransformFactory factory;
//   FilterRegistryHost host(factory);
//   service_registry_builder.registerService<sdk::FilterRegistryService>(host.service());
//
// Usage (plugin side, via the standard ServiceRegistry):
//   auto view = services.require<sdk::FilterRegistryService>();
//   view.registerTransform<MovingAverageTransform>("moving_average", libraryOwner());

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pj_plugins/sdk/filter_registry_abi.h"
#include "pj_plugins/sdk/filter_registry_service.hpp"
#include "pj_plugins/sdk/filter_transform_factory.hpp"

namespace PJ {

/// Wraps a FilterTransformFactory in a PJ_filter_registry_t fat pointer so it
/// can be advertised through the standard service registry. Non-copyable;
/// must outlive the service registry it is published into.
class FilterRegistryHost {
 public:
  explicit FilterRegistryHost(sdk::FilterTransformFactory& factory) : factory_(&factory) {
    vtable_.protocol_version = 1;
    vtable_.struct_size = sizeof(PJ_filter_registry_vtable_t);
    vtable_.register_transform = &thunkRegister;
    vtable_.unregister_transform = &thunkUnregister;
    vtable_.create_transform = &thunkCreate;
    vtable_.lookup_deleter = &thunkLookupDeleter;
    vtable_.list_ids = &thunkListIds;
  }
  FilterRegistryHost(const FilterRegistryHost&) = delete;
  FilterRegistryHost& operator=(const FilterRegistryHost&) = delete;
  FilterRegistryHost(FilterRegistryHost&&) = delete;
  FilterRegistryHost& operator=(FilterRegistryHost&&) = delete;
  ~FilterRegistryHost() = default;

  [[nodiscard]] PJ_filter_registry_t service() noexcept {
    return PJ_filter_registry_t{static_cast<void*>(this), &vtable_};
  }

  [[nodiscard]] sdk::FilterTransformFactory& factory() noexcept {
    return *factory_;
  }
  [[nodiscard]] const sdk::FilterTransformFactory& factory() const noexcept {
    return *factory_;
  }

 private:
  static FilterRegistryHost& selfOf(void* ctx) noexcept {
    return *static_cast<FilterRegistryHost*>(ctx);
  }

  static void writeError(PJ_error_t* out, const char* msg) noexcept {
    if (out == nullptr) {
      return;
    }
    std::memset(out, 0, sizeof(*out));
    if (msg != nullptr) {
      std::strncpy(out->message, msg, sizeof(out->message) - 1);
    }
  }

  /// The plugin hands us a raw void* aliasing whatever it wants pinned (the
  /// libraryOwner of its DataSource/Toolbox handle). We need a shared_ptr
  /// that releases when the entry is dropped — but the caller's shared_ptr
  /// is on the plugin side. Trick: the View::registerRaw passes `library_owner.get()`
  /// stripped of its control block; we accept that we can only pin by raw
  /// pointer identity (the host's own factory_handles_ table holds the
  /// shared_ptr it constructs internally from the original plugin handle
  /// the host knows about). For the v1 cut, store the raw pointer as an
  /// opaque token and rely on the host knowing how to map it back via the
  /// PluginRuntimeCatalog's library handles.
  ///
  /// Concretely: the host caller is expected to call `setLibraryOwnerMap`
  /// with a function that turns a void* token into a shared_ptr<void> at
  /// register time. If not set, registrations succeed but library_owner is
  /// empty (test mode / in-process).
  using LibraryOwnerResolver = std::function<std::shared_ptr<void>(void* token)>;

 public:
  void setLibraryOwnerResolver(LibraryOwnerResolver resolver) {
    std::lock_guard<std::mutex> lock(mutex_);
    resolver_ = std::move(resolver);
  }

 private:
  static bool thunkRegister(
      void* ctx, PJ_filter_transform_registration_t reg, void* library_owner_token, PJ_error_t* out_error) noexcept {
    auto& self = selfOf(ctx);
    if (reg.factory == nullptr || reg.deleter == nullptr || reg.id.data == nullptr || reg.id.size == 0) {
      writeError(out_error, "register_transform: null factory/deleter or empty id");
      return false;
    }
    std::string id(reg.id.data, reg.id.size);
    auto factory_fn = reg.factory;
    auto factory_ctx = reg.factory_ctx;
    auto deleter_fn = reg.deleter;

    sdk::FilterTransformFactory::CreateFn create_wrapper = [factory_fn, factory_ctx]() {
      auto* raw = factory_fn(factory_ctx);
      return reinterpret_cast<sdk::FilterTransform*>(raw);
    };
    sdk::FilterTransformFactory::DeleterFn delete_wrapper = [deleter_fn](sdk::FilterTransform* p) noexcept {
      deleter_fn(reinterpret_cast<PJ_filter_transform_t*>(p));
    };

    std::shared_ptr<void> owner;
    {
      std::lock_guard<std::mutex> lock(self.mutex_);
      if (self.resolver_ && library_owner_token != nullptr) {
        owner = self.resolver_(library_owner_token);
      }
    }
    self.factory_->registerTransform(
        std::move(id), std::move(create_wrapper), std::move(delete_wrapper), std::move(owner));
    return true;
  }

  static bool thunkUnregister(void* ctx, PJ_string_view_t id, PJ_error_t* out_error) noexcept {
    auto& self = selfOf(ctx);
    if (id.data == nullptr || id.size == 0) {
      writeError(out_error, "unregister_transform: empty id");
      return false;
    }
    self.factory_->unregisterTransform(std::string_view{id.data, id.size});
    return true;
  }

  static PJ_filter_transform_t* thunkCreate(void* ctx, PJ_string_view_t id, PJ_error_t* out_error) noexcept {
    auto& self = selfOf(ctx);
    if (id.data == nullptr || id.size == 0) {
      writeError(out_error, "create_transform: empty id");
      return nullptr;
    }
    auto sp = self.factory_->create(std::string_view{id.data, id.size});
    if (!sp) {
      writeError(out_error, "create_transform: id not registered");
      return nullptr;
    }
    // The cross-DSO contract owns the raw pointer by handle. Stash the
    // shared_ptr so its deleter (which carries library_owner) survives until
    // the caller releases the handle via the per-id deleter. Must route via
    // catalogue() — the static map that thunkDeleteInstance erases from —
    // because the per-id deleter handed back by thunkLookupDeleter is a free
    // function and cannot capture `self`. An earlier draft of this file
    // populated a per-instance map (self.live_instances_) that thunkDelete
    // never erased from, leaking every instance forever.
    auto* raw = sp.get();
    {
      std::lock_guard<std::mutex> lock(self.mutex_);
      catalogue().emplace(raw, std::move(sp));
    }
    return reinterpret_cast<PJ_filter_transform_t*>(raw);
  }

  // The plugin asks for a deleter by id, then later invokes it on the raw
  // pointer. We hand it a uniform host-side trampoline that releases the
  // live_instances_ entry (which dispatches to the entry's real deleter +
  // drops the library_owner ref).
  static PJ_filter_transform_deleter_fn thunkLookupDeleter(void* /*ctx*/, PJ_string_view_t /*id*/) noexcept {
    return &thunkDeleteInstance;
  }

  static void thunkDeleteInstance(PJ_filter_transform_t* p) noexcept {
    if (p == nullptr) {
      return;
    }
    auto* raw = reinterpret_cast<sdk::FilterTransform*>(p);
    // The destruction happens here when the shared_ptr is dropped from
    // live_instances_; that runs the factory entry's real deleter + drops
    // the library_owner. We need access to `self` — but the deleter_fn
    // signature can't carry context. Resolve via the static catalogue.
    catalogue().erase(raw);
  }

  using LiveMap = std::unordered_map<sdk::FilterTransform*, std::shared_ptr<sdk::FilterTransform>>;

  // Process-global instance catalogue. The thunkDeleteInstance handler is
  // referenced by raw function pointer (so plugins can call it after the
  // FilterRegistryHost-owning host shuts down), so it cannot capture the
  // FilterRegistryHost*. Routing through this static keeps the contract
  // clean. Single host per process is the expected configuration.
  static LiveMap& catalogue() {
    static LiveMap inst;
    return inst;
  }

  static void thunkListIds(void* ctx, PJ_string_view_t* out_ids, size_t capacity, size_t* out_count) noexcept {
    auto& self = selfOf(ctx);
    const auto ids = self.factory_->registeredIds();
    if (out_count != nullptr) {
      *out_count = ids.size();
    }
    if (out_ids != nullptr) {
      // The strings live in factory entries (whose `id` is std::string with
      // stable storage between calls); hand back views into them. Caller must
      // not retain past the next factory mutation.
      std::lock_guard<std::mutex> lock(self.mutex_);
      self.cached_id_views_.clear();
      self.cached_id_views_.reserve(ids.size());
      for (const auto& s : ids) {
        self.cached_id_views_.push_back(s);
      }
      size_t emitted = std::min(capacity, self.cached_id_views_.size());
      for (size_t i = 0; i < emitted; ++i) {
        out_ids[i] = PJ_string_view_t{self.cached_id_views_[i].data(), self.cached_id_views_[i].size()};
      }
    }
  }

 private:
  sdk::FilterTransformFactory* factory_;
  PJ_filter_registry_vtable_t vtable_{};
  std::mutex mutex_;
  LibraryOwnerResolver resolver_;
  std::vector<std::string> cached_id_views_;
  // Live-instance bridge from raw FilterTransform* to the shared_ptr the
  // factory minted is stored in the static catalogue() above, not here — the
  // per-id deleter handed back by thunkLookupDeleter is a free function and
  // can only reach the static.
};

}  // namespace PJ
