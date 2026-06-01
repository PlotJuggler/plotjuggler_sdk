#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file config_envelope.hpp
 * @brief Versioned wrapper pairing a DataSource's plugin-owned config with
 *        host-owned parser-binding state for delegated-ingest sources.
 */

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "pj_base/expected.hpp"

namespace PJ {

/// Versioned envelope that wraps a DataSource's config alongside host-owned
/// parser binding state. The source never sees `parser_binding`.
struct ConfigEnvelope {
  static std::string pack(std::string_view source_config, std::string_view parser_binding = "{}") {
    nlohmann::json envelope;
    envelope["version"] = 1;
    envelope["source_config"] = std::string(source_config);
    envelope["parser_binding"] = std::string(parser_binding);
    return envelope.dump();
  }

  struct Unpacked {
    std::string source_config;
    std::string parser_binding;
  };

  static Expected<Unpacked> unpack(std::string_view envelope_json) {
    auto j = nlohmann::json::parse(envelope_json, nullptr, false);
    if (j.is_discarded()) {
      return unexpected("invalid envelope JSON");
    }
    if (!j.contains("version") || !j["version"].is_number_integer()) {
      return unexpected("missing or invalid envelope version");
    }
    if (j["version"].get<int>() != 1) {
      return unexpected("unsupported envelope version");
    }
    if (!j.contains("source_config") || !j["source_config"].is_string()) {
      return unexpected("missing source_config");
    }

    Unpacked result;
    result.source_config = j["source_config"].get<std::string>();
    result.parser_binding =
        j.contains("parser_binding") && j["parser_binding"].is_string() ? j["parser_binding"].get<std::string>() : "{}";
    return result;
  }
};

}  // namespace PJ
