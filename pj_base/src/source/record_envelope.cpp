// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/source/record_envelope.hpp"

namespace PJ::sdk::source {

Expected<nlohmann::json> parseSourceRecordEnvelope(std::string_view bytes) {
  if (bytes.empty()) {
    return unexpected(std::string("source record descriptor is empty"));
  }
  static const SourceDescriptorPolicy kPolicy{
      .identity_fields = {"kind", "request", "v"},
      .presentation_fields = {"label"},
      .denied_keys = {"api_key", "apikey", "token", "password", "secret", "credentials", "authorization", "cert_path"},
      .identity = {}};
  auto parsed = parseSourceDescriptor(bytes, kPolicy);
  if (!parsed) {
    // A denied-key hit keeps its established message shape:
    // "source record carries credential-shaped key: <key>".
    if (parsed.error().starts_with(kDeniedKeyViolationPrefix)) {
      return unexpected("source record " + parsed.error());
    }
    return unexpected("source record refused: " + parsed.error());
  }
  if (!parsed->contains("kind") || !(*parsed)["kind"].is_string() ||
      (*parsed)["kind"].get_ref<const std::string&>().empty()) {
    return unexpected(std::string("source record needs a non-empty string 'kind'"));
  }
  if (!parsed->contains("v") || !(*parsed)["v"].is_number_unsigned()) {
    return unexpected(std::string("source record needs an unsigned integer 'v'"));
  }
  if (!parsed->contains("request") || !(*parsed)["request"].is_object()) {
    return unexpected(std::string("source record needs an object 'request'"));
  }
  if (parsed->contains("label") && !(*parsed)["label"].is_string()) {
    return unexpected(std::string("source record needs a string 'label'"));
  }
  return parsed;
}

}  // namespace PJ::sdk::source
