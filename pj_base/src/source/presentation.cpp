// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/source/presentation.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace PJ::sdk::source {

namespace {

constexpr std::size_t kMaxPresentationChars = 200;
constexpr std::string_view kSettingsPrefix = "source_presentation/v1/";
constexpr char kBase64UrlAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64UrlUnpadded(std::string_view bytes) {
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);

  std::size_t index = 0;
  for (; index + 3 <= bytes.size(); index += 3) {
    const std::uint32_t value = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16) |
                                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1])) << 8) |
                                static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 2]));
    out.push_back(kBase64UrlAlphabet[(value >> 18) & 0x3f]);
    out.push_back(kBase64UrlAlphabet[(value >> 12) & 0x3f]);
    out.push_back(kBase64UrlAlphabet[(value >> 6) & 0x3f]);
    out.push_back(kBase64UrlAlphabet[value & 0x3f]);
  }

  const std::size_t remaining = bytes.size() - index;
  if (remaining == 1) {
    const std::uint32_t value = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16;
    out.push_back(kBase64UrlAlphabet[(value >> 18) & 0x3f]);
    out.push_back(kBase64UrlAlphabet[(value >> 12) & 0x3f]);
  } else if (remaining == 2) {
    const std::uint32_t value = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16) |
                                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1])) << 8);
    out.push_back(kBase64UrlAlphabet[(value >> 18) & 0x3f]);
    out.push_back(kBase64UrlAlphabet[(value >> 12) & 0x3f]);
    out.push_back(kBase64UrlAlphabet[(value >> 6) & 0x3f]);
  }
  return out;
}

// A presentation code point that must never reach the UI: C0/C1 controls,
// zero-width and bidi/format controls. The text comes from a layout file and
// is shown beside trust decisions, so it must not be able to fake a row.
bool isControlOrFormat(std::uint32_t code_point) {
  return code_point < 0x20 || code_point == 0x7f || (code_point >= 0x80 && code_point <= 0x9f) ||
         code_point == 0x061c || (code_point >= 0x200b && code_point <= 0x200f) ||
         (code_point >= 0x2028 && code_point <= 0x202e) || (code_point >= 0x2060 && code_point <= 0x2064) ||
         (code_point >= 0x2066 && code_point <= 0x2069) || code_point == 0xfeff;
}

// QSettings stores QString values and the host applies QString::left(200).
// Bound UTF-8 by the corresponding UTF-16 code-unit count without splitting a
// multi-byte code point (notably, an astral code point consumes two QChars),
// dropping control/format code points on the way.
std::string capPresentation(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  std::size_t bytes = 0;
  std::size_t utf16_units = 0;
  while (bytes < text.size()) {
    const auto lead = static_cast<unsigned char>(text[bytes]);
    std::size_t width = 1;
    std::size_t units = 1;
    if (lead >= 0xc2 && lead <= 0xdf) {
      width = 2;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      width = 3;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      width = 4;
      units = 2;
    }
    bool complete = bytes + width <= text.size();
    for (std::size_t offset = 1; complete && offset < width; ++offset) {
      const auto continuation = static_cast<unsigned char>(text[bytes + offset]);
      complete = (continuation & 0xc0) == 0x80;
    }
    if (!complete) {
      width = 1;
      units = 1;
    }
    std::uint32_t code_point = lead;
    if (complete && width == 2) {
      code_point = ((lead & 0x1fu) << 6) | (static_cast<unsigned char>(text[bytes + 1]) & 0x3fu);
    } else if (complete && width == 3) {
      code_point = ((lead & 0x0fu) << 12) | ((static_cast<unsigned char>(text[bytes + 1]) & 0x3fu) << 6) |
                   (static_cast<unsigned char>(text[bytes + 2]) & 0x3fu);
    } else if (complete && width == 4) {
      code_point = 0x10000;  // astral plane: never a control/format code point
    }
    if (isControlOrFormat(code_point)) {
      bytes += width;
      continue;
    }
    if (utf16_units + units > kMaxPresentationChars) {
      break;
    }
    out.append(text.substr(bytes, width));
    bytes += width;
    utf16_units += units;
  }
  return out;
}

void setIfChanged(PJ::sdk::SettingsView settings, const std::string& key, const std::string& value) {
  const auto current = settings.value(key);
  // A backend read fault cannot establish equality, so attempt the write; all
  // presentation persistence is best-effort and never affects the Download.
  if (!current || current->toString() != value) {
    (void)settings.setValue(key, value);
  }
}

}  // namespace

std::string sourcePresentationSettingsGroup(std::string_view identity) {
  return std::string(kSettingsPrefix) + base64UrlUnpadded(identity);
}

void recordSourcePresentation(
    PJ::sdk::SettingsView settings_view, std::string_view identity, const SourcePresentation& presentation) {
  if (identity.empty()) {
    return;
  }

  const std::string group = sourcePresentationSettingsGroup(identity);
  const std::string display_name =
      capPresentation(presentation.display_name.empty() ? presentation.fallback_name : presentation.display_name);

  setIfChanged(settings_view, group + "/display_name", display_name);
  // The descriptor's origin is already the strict lowercase host:port the
  // parser enforced; the cap only guards the settings-value length.
  if (!presentation.origin.empty()) {
    setIfChanged(settings_view, group + "/origin", capPresentation(presentation.origin));
  }
}

}  // namespace PJ::sdk::source
