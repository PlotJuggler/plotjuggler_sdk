/**
 * @file fake_mcp_endpoint_host.hpp
 * @brief Scriptable "pj.mcp_endpoint.v1" host for plugin unit tests.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ::sdk::testing {

/// In-memory "pj.mcp_endpoint.v1" host for single-threaded tests; nothing is
/// served over the network. Models the held/released contract: acquire while
/// held returns the same token, and each acquire after a release (or the first
/// one) issues the next token, "token-1", "token-2", ... deterministically.
/// The fake must outlive the handle from makeHost().
///
/// @since 0.40.0
class FakeMcpEndpointHost {
 public:
  /// URL every acquire hands out.
  std::string url = "http://127.0.0.1:8765/mcp";
  /// Non-empty: acquire fails with PJ_ERROR_CODE_INTERNAL and this message, as a
  /// host whose listener cannot start, and drops any endpoint held.
  std::string fail_with;

  FakeMcpEndpointHost() = default;
  // makeHost() hands out `this` as the service ctx.
  FakeMcpEndpointHost(const FakeMcpEndpointHost&) = delete;
  FakeMcpEndpointHost& operator=(const FakeMcpEndpointHost&) = delete;

  // Scrubs the bytes it handed out, so a caller that kept the host's pointers
  // instead of copying reads X's (or trips ASAN) once the fake is gone.
  ~FakeMcpEndpointHost() {
    url.assign(url.size(), 'X');
    token_.assign(token_.size(), 'X');
  }

  [[nodiscard]] PJ_mcp_endpoint_host_t makeHost() noexcept {
    static constexpr PJ_mcp_endpoint_host_vtable_t kVtable = {
        1,
        sizeof(PJ_mcp_endpoint_host_vtable_t),
        &FakeMcpEndpointHost::tAcquire,
        &FakeMcpEndpointHost::tRelease,
    };
    return PJ_mcp_endpoint_host_t{this, &kVtable};
  }

  [[nodiscard]] bool held() const noexcept {
    return held_;
  }
  /// The token currently held; empty when none is.
  [[nodiscard]] const std::string& token() const noexcept {
    return token_;
  }
  [[nodiscard]] int acquireCalls() const noexcept {
    return acquire_calls_;
  }
  [[nodiscard]] int releaseCalls() const noexcept {
    return release_calls_;
  }

 private:
  static bool tAcquire(void* ctx, PJ_mcp_endpoint_t* out_endpoint, PJ_error_t* out_error) noexcept {
    auto* self = static_cast<FakeMcpEndpointHost*>(ctx);
    ++self->acquire_calls_;
    if (!self->fail_with.empty()) {
      self->drop();
      fillError(out_error, PJ_ERROR_CODE_INTERNAL, "mcp_endpoint", self->fail_with);
      return false;
    }
    if (!self->held_) {
      try {
        self->token_ = "token-" + std::to_string(++self->issued_);
      } catch (...) {
        fillError(out_error, PJ_ERROR_CODE_INTERNAL, "mcp_endpoint", "out of memory");
        return false;
      }
      self->held_ = true;
    }
    *out_endpoint = PJ_mcp_endpoint_t{toAbiString(self->url), toAbiString(self->token_)};
    return true;
  }

  static void tRelease(void* ctx) noexcept {
    auto* self = static_cast<FakeMcpEndpointHost*>(ctx);
    ++self->release_calls_;
    self->drop();
  }

  void drop() noexcept {
    held_ = false;
    token_.clear();
  }

  bool held_ = false;
  std::string token_;
  int issued_ = 0;
  int acquire_calls_ = 0;
  int release_calls_ = 0;
};

}  // namespace PJ::sdk::testing
