/**
 * @file encoding_utils.hpp
 * @brief Utility functions for parsing encoding lists from the runtime host.
 *
 * This header provides helpers to convert the JSON string returned by
 * runtimeHost().listAvailableEncodings() into a std::vector<std::string>.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace PJ::sdk {

/**
 * Parse a JSON array of encoding names into a vector.
 *
 * @param json_str JSON array string, e.g. ["json","cbor","protobuf"]
 * @return Vector of encoding names, or empty vector on parse error.
 *
 * Usage:
 * @code
 *   auto json = runtimeHost().listAvailableEncodings();
 *   auto encodings = PJ::sdk::parseEncodingsJson(json);
 *   dialog_.setAvailableEncodings(encodings);
 * @endcode
 */
inline std::vector<std::string> parseEncodingsJson(std::string_view json_str) {
  if (json_str.empty()) {
    return {};
  }

  auto j = nlohmann::json::parse(json_str, nullptr, false);
  if (j.is_discarded() || !j.is_array()) {
    return {};
  }

  std::vector<std::string> result;
  result.reserve(j.size());
  for (const auto& item : j) {
    if (item.is_string()) {
      result.push_back(item.get<std::string>());
    }
  }
  return result;
}

}  // namespace PJ::sdk
