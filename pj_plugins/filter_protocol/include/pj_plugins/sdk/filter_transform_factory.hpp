// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0
#pragma once

// Registry singleton for FilterTransform implementations. Plugins register
// their concrete classes at load time; the host looks them up by id. Order
// of registration is preserved (mirrors PJ3's dropdown order).

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
  using CreateFn = std::function<std::unique_ptr<FilterTransform>()>;

  [[nodiscard]] static FilterTransformFactory& instance() {
    static FilterTransformFactory inst;
    return inst;
  }

  /// Re-registering an existing `id` replaces the previous factory entry.
  void registerTransform(const char* id, CreateFn fn) {
    for (auto& e : entries_) {
      if (e.id == id) {
        e.fn = std::move(fn);
        return;
      }
    }
    entries_.push_back({id, std::move(fn)});
  }

  [[nodiscard]] std::vector<std::string> registeredIds() const {
    std::vector<std::string> ids;
    ids.reserve(entries_.size());
    for (const auto& e : entries_) {
      ids.push_back(e.id);
    }
    return ids;
  }

  /// Returns nullptr if `id` is not registered.
  [[nodiscard]] std::unique_ptr<FilterTransform> create(std::string_view id) const {
    for (const auto& e : entries_) {
      if (e.id == id) {
        return e.fn();
      }
    }
    return nullptr;
  }

 private:
  struct Entry {
    std::string id;
    CreateFn fn;
  };
  std::vector<Entry> entries_;
};

}  // namespace PJ::sdk

/// Self-register `Class` at static-init. `Class{}` must be default-constructible
/// and its `id()` must be unique.
#define PJ_REGISTER_FILTER_TRANSFORM(Class)                                                                    \
  namespace {                                                                                                  \
  [[maybe_unused]] const bool _pj_register_##Class = ([] {                                                   \
    PJ::sdk::FilterTransformFactory::instance().registerTransform(                                           \
        (Class){}.id(), [] { return std::unique_ptr<PJ::sdk::FilterTransform>(new (Class)()); });            \
    return true;                                                                                             \
  }(), true); \
  }
