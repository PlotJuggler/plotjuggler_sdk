/**
 * @file streaming_source.hpp
 * @brief Building blocks for StreamSourceBase implementations: the
 *        receive-thread -> onPoll() handoff containers and the delegated-ingest
 *        parser-binding cache.
 *
 * StreamSourceBase::onPoll() must never block, so a source with its own
 * receive thread buffers what arrives and swap-drains it from onPoll().
 * DrainQueue and LatestValueSlot are the two shapes that handoff takes;
 * DelegatedIngestCache wraps the ensureParserBinding + pushMessage pair that
 * kCapabilityDelegatedIngest sources repeat per topic.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pj_base/sdk/data_source_host_views.hpp"

namespace PJ {
namespace sdk {

/// Mutex-protected producer/consumer FIFO for poll-loop consumers: drain()
/// swaps the complete pending batch out while holding the lock only briefly.
template <typename T>
class DrainQueue {
 public:
  void push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(value));
  }

  [[nodiscard]] std::queue<T> drain() {
    std::queue<T> result;
    std::lock_guard<std::mutex> lock(mutex_);
    std::swap(result, queue_);
    return result;
  }

  void clear() {
    (void)drain();
  }

 private:
  std::mutex mutex_;
  std::queue<T> queue_;
};

/// Mutex-protected, self-coalescing handoff. Writers replace the pending value;
/// the reader atomically takes the latest value and empties the slot.
template <typename T>
class LatestValueSlot {
 public:
  void set(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ = std::move(value);
  }

  [[nodiscard]] std::optional<T> take() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<T> result;
    result.swap(value_);
    return result;
  }

 private:
  std::mutex mutex_;
  std::optional<T> value_;
};

/// Convert ABI-style string views (members `data` and `size`) into the full
/// declarative topic set delivered by pj.topic_subscription.v1.
template <typename StringView, typename Size>
[[nodiscard]] std::set<std::string> stringSetFromViews(const StringView* values, Size count) {
  std::set<std::string> result;
  for (Size i = 0; i < count; ++i) {
    result.emplace(values[i].data, values[i].size);
  }
  return result;
}

/// Safely read the parser-specific config the host injects into a delegated
/// source's config JSON under "_parser_config". Wrong-typed values degrade to
/// empty instead of throwing out of a plugin load callback.
[[nodiscard]] inline std::string parserConfigOverride(std::string_view config_json) {
  const auto cfg = nlohmann::json::parse(config_json, nullptr, false);
  if (cfg.is_discarded() || !cfg.is_object()) {
    return {};
  }
  const auto it = cfg.find("_parser_config");
  return it != cfg.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

enum class DelegatedIngestDisposition {
  kPushed,
  kBindingUnavailable,
};

/// Caches delegated-ingest parser bindings per cache key and anchors owned
/// payload bytes across the host ABI. A binding lookup failure is returned as a
/// non-error disposition so sources keep their skip-and-retry behavior; only a
/// failed push is an error subject to the source's own error policy.
class DelegatedIngestCache {
 public:
  [[nodiscard]] Expected<DelegatedIngestDisposition> push(
      const DataSourceRuntimeHostView& host, std::string_view cache_key, const ParserBindingRequest& request,
      Timestamp timestamp, std::vector<uint8_t> payload) {
    auto binding = bindings_.find(cache_key);
    if (binding == bindings_.end()) {
      auto created = host.ensureParserBinding(request);
      if (!created) {
        return DelegatedIngestDisposition::kBindingUnavailable;
      }
      binding = bindings_.emplace(std::string(cache_key), *created).first;
    }

    auto owned = std::make_shared<std::vector<uint8_t>>(std::move(payload));
    auto status = host.pushMessage(binding->second, timestamp, [owned]() -> PayloadView { return PayloadView{owned}; });
    if (!status) {
      return unexpected(status.error());
    }
    return DelegatedIngestDisposition::kPushed;
  }

  void clear() {
    bindings_.clear();
  }

 private:
  struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
      return std::hash<std::string_view>{}(value);
    }

    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
      return (*this)(std::string_view(value));
    }
  };

  std::unordered_map<std::string, ParserBindingHandle, TransparentStringHash, std::equal_to<>> bindings_;
};

}  // namespace sdk
}  // namespace PJ
