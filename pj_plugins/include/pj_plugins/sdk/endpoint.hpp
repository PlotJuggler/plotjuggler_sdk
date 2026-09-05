/**
 * @file endpoint.hpp
 * @brief Endpoint text helpers for transport plugins: scheme://host[:port][/path]
 *        composition with IPv6 bracketing. The strict port parse lives in
 *        pj_base/sdk/text_utils.hpp (PJ::sdk::parsePort).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "pj_base/sdk/text_utils.hpp"

namespace PJ {
namespace sdk {

/// Bracket an IPv6 literal when it is used as a URI authority. Already-
/// bracketed hosts and ordinary DNS/IPv4 addresses pass through unchanged.
[[nodiscard]] inline std::string authorityHost(std::string_view host) {
  if (host.find(':') != std::string_view::npos && !(host.starts_with('[') && host.ends_with(']'))) {
    return "[" + std::string(host) + "]";
  }
  return std::string(host);
}

[[nodiscard]] inline std::string composeEndpoint(
    std::string_view scheme, std::string_view host, std::string_view port = {}, std::string_view path = {}) {
  std::string result(scheme);
  if (!result.ends_with("://")) {
    result += "://";
  }
  result += authorityHost(host);
  if (!port.empty()) {
    result += ':';
    result += port;
  }
  if (!path.empty()) {
    if (!path.starts_with('/')) {
      result += '/';
    }
    result += path;
  }
  return result;
}

[[nodiscard]] inline std::string composeEndpoint(
    std::string_view scheme, std::string_view host, uint16_t port, std::string_view path = {}) {
  return composeEndpoint(scheme, host, std::to_string(port), path);
}

[[nodiscard]] inline std::string composeHostPort(std::string_view host, uint16_t port) {
  return authorityHost(host) + ":" + std::to_string(port);
}

}  // namespace sdk
}  // namespace PJ
