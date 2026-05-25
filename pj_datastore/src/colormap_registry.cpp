// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MPL-2.0

#include "pj_datastore/colormap_registry.hpp"

namespace PJ {

void ColorMapRegistry::registerMap(std::string_view name, ColorMapEvalFn eval_fn, void* user_ctx) {
  std::string key(name);
  maps_[key] = Entry{eval_fn, user_ctx};
  active_ = std::move(key);
}

void ColorMapRegistry::unregisterMap(std::string_view name) {
  std::string key(name);
  maps_.erase(key);
  if (active_ == key) {
    active_.clear();
  }
}

void ColorMapRegistry::setActive(std::string_view name) {
  std::string key(name);
  if (maps_.find(key) != maps_.end()) {
    active_ = std::move(key);
  }
}

std::string ColorMapRegistry::evaluate(double value) const {
  if (active_.empty()) {
    return {};
  }
  auto it = maps_.find(active_);
  if (it == maps_.end()) {
    return {};
  }
  const char* result = it->second.eval_fn(value, it->second.user_ctx);
  return result ? std::string{result} : std::string{};
}

bool ColorMapRegistry::hasActive() const {
  return !active_.empty() && maps_.find(active_) != maps_.end();
}

}  // namespace PJ
