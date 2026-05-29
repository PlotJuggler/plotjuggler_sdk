#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Host-side helpers for providing the "pj.settings.v1" service (see
// `SettingsView` in plugin_data_api.hpp for the plugin-facing side). A host
// implements `SettingsBackend` over its own store (e.g. QSettings) and
// registers `SettingsStoreHost(backend).view()` into the plugin service
// registry. Header-only, Qt-free; part of the plugin SDK so the service is
// self-contained (plugin view + host adapter together).

#include <exception>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ::sdk {

/// Backend a host implements to provide the settings service. The GUI app backs
/// this with QSettings; a headless host can use a JSON file or the
/// `InMemorySettingsBackend` below. Scalars are stored/retrieved as strings
/// (the plugin-side `SettingsView` serializes int/double/bool to strings);
/// string lists are stored natively. All calls are main-thread.
class SettingsBackend {
 public:
  virtual ~SettingsBackend() = default;

  virtual std::optional<std::string> getString(std::string_view key) = 0;
  virtual void setString(std::string_view key, std::string_view value) = 0;
  virtual std::optional<std::vector<std::string>> getStringList(std::string_view key) = 0;
  virtual void setStringList(std::string_view key, const std::vector<std::string>& values) = 0;
  virtual bool contains(std::string_view key) = 0;
  virtual void remove(std::string_view key) = 0;
};

/// Adapts a `SettingsBackend` to the C ABI `PJ_settings_store_t`. Owns the
/// scratch buffers that back the getters' "valid until the next call" contract,
/// so it must outlive every plugin bound to its `view()`. Main-thread use.
class SettingsStoreHost {
 public:
  explicit SettingsStoreHost(SettingsBackend& backend) : backend_(backend) {}

  [[nodiscard]] PJ_settings_store_t view() noexcept {
    static constexpr PJ_settings_store_vtable_t kVtable = {
        PJ_PLUGIN_DATA_API_VERSION,     sizeof(PJ_settings_store_vtable_t), &SettingsStoreHost::tGetString,
        &SettingsStoreHost::tSetString, &SettingsStoreHost::tGetStringList, &SettingsStoreHost::tSetStringList,
        &SettingsStoreHost::tContains,  &SettingsStoreHost::tRemove,
    };
    return PJ_settings_store_t{this, &kVtable};
  }

 private:
  // C ABI trampolines: cast ctx, delegate to the backend, translate C++
  // exceptions to PJ_error_t. A missing key is *out_found == false with a
  // `true` return (not an error).
  static bool tGetString(
      void* ctx, PJ_string_view_t key, PJ_string_view_t* out_value, bool* out_found, PJ_error_t* out_error) noexcept {
    if (ctx == nullptr || out_value == nullptr || out_found == nullptr) {
      fillError(out_error, 2, "settings", "null ctx or out-param");
      return false;
    }
    auto* self = static_cast<SettingsStoreHost*>(ctx);
    try {
      auto value = self->backend_.getString(toStringView(key));
      if (!value) {
        *out_found = false;
        *out_value = PJ_string_view_t{nullptr, 0};
        return true;
      }
      self->scratch_value_ = std::move(*value);
      *out_found = true;
      *out_value = toAbiString(self->scratch_value_);
      return true;
    } catch (const std::exception& e) {
      fillError(out_error, 1, "settings", std::string("getString threw: ") + e.what());
      return false;
    } catch (...) {
      fillError(out_error, 1, "settings", "getString threw unknown exception");
      return false;
    }
  }

  static bool tSetString(void* ctx, PJ_string_view_t key, PJ_string_view_t value, PJ_error_t* out_error) noexcept {
    if (ctx == nullptr) {
      fillError(out_error, 2, "settings", "null ctx");
      return false;
    }
    auto* self = static_cast<SettingsStoreHost*>(ctx);
    try {
      self->backend_.setString(toStringView(key), toStringView(value));
      return true;
    } catch (const std::exception& e) {
      fillError(out_error, 1, "settings", std::string("setString threw: ") + e.what());
      return false;
    } catch (...) {
      fillError(out_error, 1, "settings", "setString threw unknown exception");
      return false;
    }
  }

  static bool tGetStringList(
      void* ctx, PJ_string_view_t key, const PJ_string_view_t** out_items, uint64_t* out_count, bool* out_found,
      PJ_error_t* out_error) noexcept {
    if (ctx == nullptr || out_items == nullptr || out_count == nullptr || out_found == nullptr) {
      fillError(out_error, 2, "settings", "null ctx or out-param");
      return false;
    }
    auto* self = static_cast<SettingsStoreHost*>(ctx);
    try {
      auto values = self->backend_.getStringList(toStringView(key));
      if (!values) {
        *out_found = false;
        *out_items = nullptr;
        *out_count = 0;
        return true;
      }
      self->scratch_list_ = std::move(*values);
      self->scratch_list_views_.clear();
      self->scratch_list_views_.reserve(self->scratch_list_.size());
      for (const auto& item : self->scratch_list_) {
        self->scratch_list_views_.push_back(toAbiString(item));
      }
      *out_found = true;
      *out_items = self->scratch_list_views_.data();
      *out_count = self->scratch_list_views_.size();
      return true;
    } catch (const std::exception& e) {
      fillError(out_error, 1, "settings", std::string("getStringList threw: ") + e.what());
      return false;
    } catch (...) {
      fillError(out_error, 1, "settings", "getStringList threw unknown exception");
      return false;
    }
  }

  static bool tSetStringList(
      void* ctx, PJ_string_view_t key, const PJ_string_view_t* items, uint64_t count, PJ_error_t* out_error) noexcept {
    if (ctx == nullptr || (items == nullptr && count != 0)) {
      fillError(out_error, 2, "settings", "null ctx or items");
      return false;
    }
    auto* self = static_cast<SettingsStoreHost*>(ctx);
    try {
      std::vector<std::string> values;
      values.reserve(count);
      for (size_t i = 0; i < count; ++i) {
        values.emplace_back(toStringView(items[i]));
      }
      self->backend_.setStringList(toStringView(key), values);
      return true;
    } catch (const std::exception& e) {
      fillError(out_error, 1, "settings", std::string("setStringList threw: ") + e.what());
      return false;
    } catch (...) {
      fillError(out_error, 1, "settings", "setStringList threw unknown exception");
      return false;
    }
  }

  static bool tContains(void* ctx, PJ_string_view_t key, bool* out_present, PJ_error_t* out_error) noexcept {
    if (ctx == nullptr || out_present == nullptr) {
      fillError(out_error, 2, "settings", "null ctx or out_present");
      return false;
    }
    auto* self = static_cast<SettingsStoreHost*>(ctx);
    try {
      *out_present = self->backend_.contains(toStringView(key));
      return true;
    } catch (const std::exception& e) {
      fillError(out_error, 1, "settings", std::string("contains threw: ") + e.what());
      return false;
    } catch (...) {
      fillError(out_error, 1, "settings", "contains threw unknown exception");
      return false;
    }
  }

  static bool tRemove(void* ctx, PJ_string_view_t key, PJ_error_t* out_error) noexcept {
    if (ctx == nullptr) {
      fillError(out_error, 2, "settings", "null ctx");
      return false;
    }
    auto* self = static_cast<SettingsStoreHost*>(ctx);
    try {
      self->backend_.remove(toStringView(key));
      return true;
    } catch (const std::exception& e) {
      fillError(out_error, 1, "settings", std::string("remove threw: ") + e.what());
      return false;
    } catch (...) {
      fillError(out_error, 1, "settings", "remove threw unknown exception");
      return false;
    }
  }

  SettingsBackend& backend_;
  std::string scratch_value_;
  std::vector<std::string> scratch_list_;
  std::vector<PJ_string_view_t> scratch_list_views_;
};

/// Trivial std::map-backed `SettingsBackend` for tests and headless hosts.
class InMemorySettingsBackend : public SettingsBackend {
 public:
  std::optional<std::string> getString(std::string_view key) override {
    auto it = strings_.find(key);
    if (it == strings_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void setString(std::string_view key, std::string_view value) override {
    strings_[std::string(key)] = std::string(value);
  }

  std::optional<std::vector<std::string>> getStringList(std::string_view key) override {
    auto it = lists_.find(key);
    if (it == lists_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void setStringList(std::string_view key, const std::vector<std::string>& values) override {
    lists_[std::string(key)] = values;
  }

  bool contains(std::string_view key) override {
    return strings_.find(key) != strings_.end() || lists_.find(key) != lists_.end();
  }

  void remove(std::string_view key) override {
    if (auto it = strings_.find(key); it != strings_.end()) {
      strings_.erase(it);
    }
    if (auto it = lists_.find(key); it != lists_.end()) {
      lists_.erase(it);
    }
  }

 private:
  std::map<std::string, std::string, std::less<>> strings_;
  std::map<std::string, std::vector<std::string>, std::less<>> lists_;
};

}  // namespace PJ::sdk
