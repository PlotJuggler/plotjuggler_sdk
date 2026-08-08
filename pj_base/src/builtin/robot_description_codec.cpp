// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/robot_description_codec.hpp"

#include <string>
#include <vector>

#include "geometry_codec.hpp"
#include "protobuf_wire.hpp"

namespace PJ {

std::vector<uint8_t> serializeRobotDescription(const sdk::RobotDescription& description) {
  std::vector<uint8_t> out;
  builtin_wire::Writer writer(out);
  writer.message(
      1, [&](builtin_wire::Writer& nested) { builtin_wire::writeTimestamp(nested, description.timestamp_ns); });
  writer.string(2, description.topic);
  writer.string(3, description.format);
  writer.string(4, description.text);
  return out;
}

Expected<sdk::RobotDescription> deserializeRobotDescription(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("RobotDescription wire: empty buffer"));
  }

  builtin_wire::Reader reader(data, size);
  sdk::RobotDescription description;
  const bool ok = builtin_wire::parseFields(reader, [&](builtin_wire::Tag tag, builtin_wire::Reader& field) {
    switch (tag.field) {
      case 1:
        return tag.type == builtin_wire::WireType::kLengthDelimited &&
               builtin_wire::readTimestampMessage(field, description.timestamp_ns);
      case 2:
        return tag.type == builtin_wire::WireType::kLengthDelimited && field.readString(description.topic);
      case 3:
        return tag.type == builtin_wire::WireType::kLengthDelimited && field.readString(description.format);
      case 4:
        return tag.type == builtin_wire::WireType::kLengthDelimited && field.readString(description.text);
      default:
        return false;
    }
  });

  if (!ok) {
    return unexpected(std::string("RobotDescription wire: decode failed"));
  }
  return description;
}

}  // namespace PJ
