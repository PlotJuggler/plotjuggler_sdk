/**
 * @file origin.hpp
 * @brief Strict, fail-closed server-origin parsing for descriptor-import
 *        providers: trust decisions and credential release are keyed by
 *        (scheme, host, port), never by a lossy display string.
 *
 * A provider fixes the OriginPolicy once (which schemes its transport speaks,
 * which of them imply a default port) and then parses every URI it is about
 * to trust, compare, or hand a credential to through it. Anything the policy
 * does not explicitly accept is rejected rather than normalized: a scheme
 * not in the policy, userinfo, query, fragment, an empty host, any IPv6
 * literal (bracketed or not), any host byte outside [a-z0-9._-], a missing
 * port without a scheme default, or a port outside 1..65535.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/expected.hpp"

namespace PJ {
namespace sdk {
namespace source {

/// Strict lowercase host:port, host bytes [a-z0-9._-], decimal port 1..65535.
/// Rejects schemes, userinfo, paths/query/fragment, IPv6 and leading-zero ports;
/// performs no normalization. Widening requires a version bump for any
/// descriptor embedding this validator, because accepted bytes define identity.
[[nodiscard]] Expected<void> validateSchemelessOrigin(std::string_view origin);

/// A parsed server origin. `scheme` and `host` are lowercase; no IDNA /
/// punycode normalization is attempted (a host is compared byte-wise).
struct Origin {
  std::string scheme;
  std::string host;
  std::uint16_t port = 0;

  [[nodiscard]] bool operator==(const Origin&) const = default;
};

/// What parseOrigin() accepts. Schemes are matched case-insensitively and must
/// be listed lowercase. Fail-closed: an EMPTY `allowed_schemes` accepts no
/// URI at all — a provider must name the schemes its transport speaks.
struct OriginPolicy {
  /// Accepted schemes, e.g. {"grpc", "grpc+tls"}.
  std::vector<std::string> allowed_schemes;
  /// Port implied by a scheme when the URI carries none, e.g. {"https", 443}.
  /// A scheme absent from this map REQUIRES an explicit port.
  std::map<std::string, std::uint16_t> default_ports;
};

/// Strict parse of `uri` under `policy`; nullopt for every rejected shape (see
/// the file comment). Paths are allowed and ignored.
[[nodiscard]] std::optional<Origin> parseOrigin(std::string_view uri, const OriginPolicy& policy);

/// The canonical "scheme://host:port" key of an origin — the string an
/// allowlist or ledger stores and compares.
[[nodiscard]] std::string originKey(const Origin& origin);

/// True iff both URIs parse under `policy` and are the same origin. Rejected
/// shapes never match, not even themselves.
[[nodiscard]] bool sameOrigin(std::string_view a, std::string_view b, const OriginPolicy& policy);

/// Parse a list of origins separated by commas, semicolons or whitespace (the
/// shape of an environment-variable allowlist). Entries that fail to parse
/// are dropped silently — an allowlist can only ever widen by being correct.
[[nodiscard]] std::vector<Origin> parseOriginList(std::string_view text, const OriginPolicy& policy);

/// True iff `uri` parses under `policy` and its origin is in `allowlist`.
[[nodiscard]] bool originAllowed(
    std::string_view uri, const std::vector<Origin>& allowlist, const OriginPolicy& policy);

}  // namespace source
}  // namespace sdk
}  // namespace PJ
