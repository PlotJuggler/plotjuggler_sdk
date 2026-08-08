// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/parser_module_manifest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace PJ::parser_module {
namespace {

constexpr std::array<uint8_t, 8> kWasmPreamble{0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
constexpr std::string_view kManifestSectionName = PJ_PARSER_MODULE_MANIFEST_SECTION_NAME;

struct ManifestScan {
  size_t count = 0;
  Span<const uint8_t> bytes;
};

[[nodiscard]] Expected<uint32_t> readVarUint32(Span<const uint8_t> bytes, size_t* position) {
  uint32_t value = 0;
  for (size_t index = 0; index < 5; ++index) {
    if (*position >= bytes.size()) {
      return unexpected(std::string("truncated wasm varuint32"));
    }
    const uint8_t byte = bytes[(*position)++];
    if (index == 4 && (byte & UINT8_C(0xF0)) != 0) {
      return unexpected(std::string("wasm varuint32 overflows uint32"));
    }
    value |= static_cast<uint32_t>(byte & UINT8_C(0x7F)) << (index * 7U);
    if ((byte & UINT8_C(0x80)) == 0) {
      return value;
    }
  }
  return unexpected(std::string("wasm varuint32 exceeds five bytes"));
}

void appendVarUint32(std::vector<uint8_t>* output, uint32_t value) {
  do {
    uint8_t byte = static_cast<uint8_t>(value & UINT32_C(0x7F));
    value >>= 7U;
    if (value != 0) {
      byte |= UINT8_C(0x80);
    }
    output->push_back(byte);
  } while (value != 0);
}

[[nodiscard]] size_t varUint32Size(uint32_t value) {
  size_t size = 1;
  while (value >= UINT32_C(0x80)) {
    value >>= 7U;
    ++size;
  }
  return size;
}

[[nodiscard]] Expected<ManifestScan> scanManifestSections(Span<const uint8_t> wasm) {
  if ((wasm.data() == nullptr && !wasm.empty()) || wasm.size() < kWasmPreamble.size() ||
      !std::equal(kWasmPreamble.begin(), kWasmPreamble.end(), wasm.begin())) {
    return unexpected(std::string("invalid or truncated wasm preamble"));
  }

  ManifestScan result;
  size_t position = kWasmPreamble.size();
  while (position < wasm.size()) {
    const uint8_t section_id = wasm[position++];
    auto section_size = readVarUint32(wasm, &position);
    if (!section_size) {
      return unexpected(section_size.error());
    }
    if (static_cast<size_t>(*section_size) > wasm.size() - position) {
      return unexpected(std::string("wasm section exceeds the remaining module bytes"));
    }
    const size_t section_end = position + *section_size;
    if (section_id == 0) {
      size_t custom_position = position;
      auto name_size = readVarUint32(wasm.first(section_end), &custom_position);
      if (!name_size) {
        return unexpected(std::string("malformed wasm custom-section name: ") + name_size.error());
      }
      if (static_cast<size_t>(*name_size) > section_end - custom_position) {
        return unexpected(std::string("wasm custom-section name exceeds its section"));
      }
      const auto name = wasm.subspan(custom_position, *name_size);
      if (name.size() == kManifestSectionName.size() &&
          std::equal(name.begin(), name.end(), reinterpret_cast<const uint8_t*>(kManifestSectionName.data()))) {
        ++result.count;
        const size_t manifest_begin = custom_position + *name_size;
        result.bytes = wasm.subspan(manifest_begin, section_end - manifest_begin);
      }
    }
    position = section_end;
  }
  return result;
}

}  // namespace

Expected<std::vector<uint8_t>> appendManifestSection(Span<const uint8_t> wasm, Span<const uint8_t> manifest) {
  auto scanned = scanManifestSections(wasm);
  if (!scanned) {
    return unexpected(scanned.error());
  }
  if (scanned->count != 0) {
    return unexpected(std::string("wasm already contains a parser-module manifest section"));
  }
  if (manifest.data() == nullptr && !manifest.empty()) {
    return unexpected(std::string("manifest storage is null"));
  }
  if (manifest.size() > std::numeric_limits<uint32_t>::max()) {
    return unexpected(std::string("manifest exceeds the wasm custom-section size range"));
  }

  const auto name_size = static_cast<uint32_t>(kManifestSectionName.size());
  const uint64_t payload_size_64 =
      varUint32Size(name_size) + static_cast<uint64_t>(name_size) + static_cast<uint64_t>(manifest.size());
  if (payload_size_64 > std::numeric_limits<uint32_t>::max() || payload_size_64 > std::numeric_limits<size_t>::max()) {
    return unexpected(std::string("embedded wasm manifest size overflows the output range"));
  }
  const size_t payload_size = static_cast<size_t>(payload_size_64);
  const size_t section_header_size = 1 + varUint32Size(static_cast<uint32_t>(payload_size));
  if (wasm.size() > std::numeric_limits<size_t>::max() - section_header_size ||
      payload_size > std::numeric_limits<size_t>::max() - wasm.size() - section_header_size) {
    return unexpected(std::string("embedded wasm manifest size overflows the output range"));
  }

  try {
    std::vector<uint8_t> output;
    output.reserve(wasm.size() + section_header_size + payload_size);
    output.insert(output.end(), wasm.begin(), wasm.end());
    output.push_back(0);
    appendVarUint32(&output, static_cast<uint32_t>(payload_size));
    appendVarUint32(&output, name_size);
    output.insert(output.end(), kManifestSectionName.begin(), kManifestSectionName.end());
    if (!manifest.empty()) {
      output.insert(output.end(), manifest.begin(), manifest.end());
    }
    return output;
  } catch (const std::bad_alloc&) {
    return unexpected(std::string("allocation failed while embedding the wasm manifest"));
  } catch (...) {
    return unexpected(std::string("unexpected failure while embedding the wasm manifest"));
  }
}

Expected<Span<const uint8_t>> readManifestSection(Span<const uint8_t> wasm) {
  auto scanned = scanManifestSections(wasm);
  if (!scanned) {
    return unexpected(scanned.error());
  }
  if (scanned->count == 0) {
    return unexpected(std::string("wasm has no parser-module manifest section"));
  }
  if (scanned->count != 1) {
    return unexpected(std::string("wasm has multiple parser-module manifest sections"));
  }
  return scanned->bytes;
}

}  // namespace PJ::parser_module
