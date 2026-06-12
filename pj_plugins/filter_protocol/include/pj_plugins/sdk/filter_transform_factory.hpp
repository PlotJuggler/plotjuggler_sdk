// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0
#pragma once

// FilterTransform registry. The host owns a single instance and exposes it to
// plugins via the pj.filter_registry.v1 service (filter_registry_service.hpp);
// the plugin registers its classes at loaderInit and resolves them through the
// same registry for preview / saveParams / loadParams. Order of registration
// is preserved (mirrors PJ3's dropdown order).
//
// Each entry pins a `library_owner` shared_ptr<void> so the host keeps the
// plugin DSO loaded for as long as any of its registered create_fn /
// deleter_fn pair could still run.

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_plugins/sdk/filter_transform.hpp"

namespace PJ::sdk {

class FilterTransformFactory {
 public:
  using CreateFn = std::function<FilterTransform*()>;
  // No `noexcept` qualifier: std::function cannot specialise on a noexcept
  // function type in C++20. The contract still requires the deleter not to
  // throw — the host wraps any registered deleter so the noexcept guarantee
  // is enforced at the C-ABI boundary instead.
  using DeleterFn = std::function<void(FilterTransform*)>;

  /// Register a transform class under @p id. The caller passes a paired
  /// `create_fn` / `delete_fn` so destruction happens on the same side that
  /// allocated the instance (cross-DSO safety). `library_owner` keeps the
  /// owning DSO loaded while the entry is live.
  ///
  /// Re-registering an existing id replaces the previous entry.
  void registerTransform(
      std::string id, CreateFn create_fn, DeleterFn delete_fn, std::shared_ptr<void> library_owner) {
    for (auto& e : entries_) {
      if (e.id == id) {
        e.create_fn = std::move(create_fn);
        e.delete_fn = std::move(delete_fn);
        e.library_owner = std::move(library_owner);
        return;
      }
    }
    entries_.push_back({std::move(id), std::move(create_fn), std::move(delete_fn), std::move(library_owner)});
  }

  /// Drop the entry under @p id. Existing instances stay alive because their
  /// destruction path captured the deleter + library_owner at create time.
  void unregisterTransform(std::string_view id) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->id == id) {
        entries_.erase(it);
        return;
      }
    }
  }

  [[nodiscard]] std::vector<std::string> registeredIds() const {
    std::vector<std::string> ids;
    ids.reserve(entries_.size());
    for (const auto& e : entries_) {
      ids.push_back(e.id);
    }
    return ids;
  }

  /// Create an instance under @p id. Returns a shared_ptr whose deleter routes
  /// through the entry's deleter_fn so destruction happens in the same DSO
  /// that allocated it. Captures the library_owner shared_ptr in the deleter
  /// so the DSO outlives the instance even if the entry is later unregistered
  /// or replaced. Returns nullptr if @p id is not registered.
  [[nodiscard]] std::shared_ptr<FilterTransform> create(std::string_view id) const {
    for (const auto& e : entries_) {
      if (e.id == id) {
        FilterTransform* raw = e.create_fn();
        if (raw == nullptr) {
          return nullptr;
        }
        auto deleter = e.delete_fn;
        auto owner = e.library_owner;
        return std::shared_ptr<FilterTransform>(raw, [deleter, owner](FilterTransform* p) {
          deleter(p);
          (void)owner;  // owner ref drops here, after deleter — keeps DSO loaded
        });
      }
    }
    return nullptr;
  }

  /// Snapshot the deleter for an id. Used by the C-ABI service wrapper so it
  /// can return a deleter to the cross-DSO caller (the in-process C++ API
  /// uses create() directly and never needs this).
  [[nodiscard]] DeleterFn lookupDeleter(std::string_view id) const {
    for (const auto& e : entries_) {
      if (e.id == id) {
        return e.delete_fn;
      }
    }
    return {};
  }

 private:
  struct Entry {
    std::string id;
    CreateFn create_fn;
    DeleterFn delete_fn;
    std::shared_ptr<void> library_owner;
  };
  std::vector<Entry> entries_;
};

}  // namespace PJ::sdk
