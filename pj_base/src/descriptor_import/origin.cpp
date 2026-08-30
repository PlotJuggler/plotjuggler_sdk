// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/descriptor_import/origin.hpp"

#include <algorithm>
#include <cctype>

#include "pj_base/sdk/text_utils.hpp"

namespace PJ {
namespace sdk {
namespace descriptor_import {

namespace {

// The only bytes a HOST may contain (checked after ASCII-lowering): letters,
// digits, dot, hyphen, underscore. Anything else — a colon left over from the
// last-colon split (an unbracketed IPv6 literal), brackets, userinfo residue,
// spaces, percent-escapes — is a malformed authority and fails closed.
bool validHostBytes(const std::string& host) {
  if (host.empty()) {
    return false;
  }
  return std::all_of(host.begin(), host.end(), [](const char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
  });
}

}  // namespace

std::optional<Origin> parseOrigin(std::string_view uri, const OriginPolicy& policy) {
  const std::size_t scheme_end = uri.find("://");
  if (scheme_end == std::string_view::npos || scheme_end == 0) {
    return std::nullopt;
  }
  const std::string scheme = lowerAscii(std::string(uri.substr(0, scheme_end)));
  if (std::find(policy.allowed_schemes.begin(), policy.allowed_schemes.end(), scheme) == policy.allowed_schemes.end()) {
    return std::nullopt;
  }
  if (uri.find('?') != std::string_view::npos || uri.find('#') != std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view rest = uri.substr(scheme_end + 3);
  const std::size_t path_start = rest.find('/');
  const std::string_view authority = path_start == std::string_view::npos ? rest : rest.substr(0, path_start);
  if (authority.empty()) {
    return std::nullopt;
  }
  const std::size_t colon = authority.rfind(':');
  Origin origin;
  origin.scheme = scheme;
  if (colon == std::string_view::npos) {
    const auto default_port = policy.default_ports.find(scheme);
    if (default_port == policy.default_ports.end()) {
      return std::nullopt;  // no explicit port and none implied by the scheme
    }
    origin.host = lowerAscii(std::string(authority));
    origin.port = default_port->second;
  } else {
    const auto port = parsePort(authority.substr(colon + 1));
    if (colon == 0 || !port.has_value()) {
      return std::nullopt;  // empty host, or a port that is not 1..65535 digits
    }
    origin.host = lowerAscii(std::string(authority.substr(0, colon)));
    origin.port = *port;
  }
  if (!validHostBytes(origin.host)) {
    return std::nullopt;  // malformed authority (userinfo, brackets, IPv6, junk)
  }
  return origin;
}

std::string originKey(const Origin& origin) {
  return origin.scheme + "://" + origin.host + ":" + std::to_string(origin.port);
}

bool sameOrigin(std::string_view a, std::string_view b, const OriginPolicy& policy) {
  const auto origin_a = parseOrigin(a, policy);
  const auto origin_b = parseOrigin(b, policy);
  return origin_a.has_value() && origin_b.has_value() && *origin_a == *origin_b;
}

std::vector<Origin> parseOriginList(std::string_view text, const OriginPolicy& policy) {
  std::vector<Origin> origins;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = start;
    while (end < text.size() && text[end] != ',' && text[end] != ';' &&
           !std::isspace(static_cast<unsigned char>(text[end]))) {
      ++end;
    }
    if (end > start) {
      if (auto origin = parseOrigin(text.substr(start, end - start), policy)) {
        origins.push_back(std::move(*origin));
      }
    }
    start = end + 1;
  }
  return origins;
}

bool originAllowed(std::string_view uri, const std::vector<Origin>& allowlist, const OriginPolicy& policy) {
  const auto origin = parseOrigin(uri, policy);
  return origin.has_value() && std::find(allowlist.begin(), allowlist.end(), *origin) != allowlist.end();
}

}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
