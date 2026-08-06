#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <string_view>

namespace PJ::sdk::detail {

/// Append the escaped contents of a JSON string, without the surrounding quotes.
inline void appendJsonEscaped(std::string& out, std::string_view value) {
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    switch (c) {
      case '"':
        out.append("\\\"");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '\b':
        out.append("\\b");
        break;
      case '\f':
        out.append("\\f");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      default:
        if (byte < 0x20U) {
          static constexpr char kHex[] = "0123456789abcdef";
          out.append("\\u00");
          out.push_back(kHex[(byte >> 4U) & 0xFU]);
          out.push_back(kHex[byte & 0xFU]);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
}

/// Append a complete quoted and escaped JSON string.
inline void appendJsonString(std::string& out, std::string_view value) {
  out.push_back('"');
  appendJsonEscaped(out, value);
  out.push_back('"');
}

}  // namespace PJ::sdk::detail
