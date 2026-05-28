// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/log_codec.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "geometry_codec.hpp"
#include "protobuf_wire.hpp"

namespace PJ {
namespace {

using builtin_wire::parseFields;
using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::Log;

Log::Level levelFromWire(uint64_t value) {
  switch (value) {
    case 1:
      return Log::Level::kDebug;
    case 2:
      return Log::Level::kInfo;
    case 3:
      return Log::Level::kWarning;
    case 4:
      return Log::Level::kError;
    case 5:
      return Log::Level::kFatal;
    default:
      return Log::Level::kUnknown;
  }
}

}  // namespace

std::vector<uint8_t> serializeLog(const Log& log) {
  std::vector<uint8_t> out;
  Writer writer(out);

  writer.message(1, [&](Writer& nested) { builtin_wire::writeTimestamp(nested, log.timestamp_ns); });
  writer.varint(2, static_cast<uint64_t>(log.level));
  writer.string(3, log.message);
  writer.string(4, log.name);

  return out;
}

Expected<sdk::Log> deserializeLog(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("Log wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::Log log;

  const bool ok = parseFields(reader, [&](Tag tag, Reader& r) {
    switch (tag.field) {
      case 1:
        return tag.type == WireType::kLengthDelimited && builtin_wire::readTimestampMessage(r, log.timestamp_ns);
      case 2: {
        if (tag.type != WireType::kVarint) {
          return false;
        }
        uint64_t v = 0;
        if (!r.readVarint(v)) {
          return false;
        }
        log.level = levelFromWire(v);
        return true;
      }
      case 3:
        return tag.type == WireType::kLengthDelimited && r.readString(log.message);
      case 4:
        return tag.type == WireType::kLengthDelimited && r.readString(log.name);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("Log wire: decode failed"));
  }

  return log;
}

}  // namespace PJ
