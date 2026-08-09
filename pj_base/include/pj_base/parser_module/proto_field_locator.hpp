#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file proto_field_locator.hpp
 * @brief FileDescriptorSet field-path compiler for protobuf messages.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/parser_module/proto_reader.hpp"

namespace pj {

using ProtoFieldId = size_t;

namespace detail {

struct ProtoSchemaField {
  std::string name;
  uint32_t number = 0;
  uint32_t type = 0;
  std::string type_name;
  bool repeated = false;
};

struct ProtoSchemaMessage {
  std::string full_name;
  std::vector<ProtoSchemaField> fields;
  bool top_level = false;
};

struct ProtoRequestedField {
  std::string path;
  std::vector<uint32_t> numbers;
};

inline std::string protoString(ByteView bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data), bytes.size);
}

inline std::string normalizeProtoTypeName(std::string_view name) {
  if (!name.empty() && name.front() == '.') {
    name.remove_prefix(1);
  }
  return std::string(name);
}

}  // namespace detail

class ProtoTraversalPlan {
 public:
  [[nodiscard]] size_t size() const noexcept {
    return requested_.size();
  }

  [[nodiscard]] Expected<ProtoFieldId> field(std::string_view path) const {
    for (size_t index = 0; index < requested_.size(); ++index) {
      if (requested_[index].path == path) {
        return index;
      }
    }
    return Status::error("field path is not present in the protobuf traversal plan");
  }

  [[nodiscard]] Expected<std::vector<uint32_t>> numberPath(ProtoFieldId field_id) const {
    if (field_id >= requested_.size()) {
      return Status::error("protobuf field id is outside the traversal plan");
    }
    PJ_PARSER_MODULE_TRY {
      return requested_[field_id].numbers;
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return Status::error("allocation failed while copying a protobuf field path");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return Status::error("unexpected failure while copying a protobuf field path");
    }
  }

  [[nodiscard]] Expected<ProtoReader::Field> locate(const ProtoReader& reader, ProtoFieldId field_id) const {
    if (field_id >= requested_.size()) {
      return Status::error("protobuf field id is outside the traversal plan");
    }
    const auto& numbers = requested_[field_id].numbers;
    if (numbers.size() > ProtoReader::kMaxRecursionDepth) {
      return Status::error("protobuf field path depth exceeds 64");
    }
    ProtoReader current = reader;
    for (size_t index = 0; index < numbers.size(); ++index) {
      auto field = current.last(numbers[index]);
      if (!field) {
        return field.status();
      }
      if (index + 1 == numbers.size()) {
        return field;
      }
      if (field->wire_type != ProtoReader::WireType::kLengthDelimited) {
        return Status::error("protobuf field-path intermediate is not a message");
      }
      current = ProtoReader(field->bytes);
    }
    return Status::error("protobuf field path is empty");
  }

 private:
  std::vector<detail::ProtoRequestedField> requested_;

  friend class ProtoFieldLocator;
};

class ProtoFieldLocator {
 public:
  explicit ProtoFieldLocator(ByteView descriptor_set, std::string_view root_type = {}) {
    PJ_PARSER_MODULE_TRY {
      status_ = parse(descriptor_set, root_type);
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      status_ = Status::error("allocation failed while decoding FileDescriptorSet");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      status_ = Status::error("unexpected failure while decoding FileDescriptorSet");
    }
  }

  [[nodiscard]] const Status& status() const noexcept {
    return status_;
  }

  [[nodiscard]] Expected<ProtoTraversalPlan> locate(std::initializer_list<std::string_view> paths) const {
    PJ_PARSER_MODULE_TRY {
      return locate(std::vector<std::string_view>(paths));
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return Status::error("allocation failed while compiling protobuf field paths");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return Status::error("unexpected failure while compiling protobuf field paths");
    }
  }

  [[nodiscard]] Expected<ProtoTraversalPlan> locate(const std::vector<std::string_view>& paths) const {
    if (!status_.isOk()) {
      return status_;
    }
    PJ_PARSER_MODULE_TRY {
      ProtoTraversalPlan plan;
      for (const auto& path : paths) {
        auto request = compilePath(path);
        if (!request) {
          return request.status();
        }
        for (const auto& existing : plan.requested_) {
          if (existing.path == request->path) {
            return Status::error("duplicate protobuf field path requested");
          }
        }
        plan.requested_.push_back(std::move(*request));
      }
      if (plan.requested_.empty()) {
        return Status::error("at least one protobuf field path is required");
      }
      return plan;
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return Status::error("allocation failed while compiling protobuf field paths");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return Status::error("unexpected failure while compiling protobuf field paths");
    }
  }

 private:
  [[nodiscard]] Status parse(ByteView descriptor_set, std::string_view root_type) {
    if (descriptor_set.data == nullptr || descriptor_set.size == 0) {
      return Status::error("FileDescriptorSet is empty");
    }
    ProtoReader set_reader(descriptor_set);
    auto files = set_reader.matching(1);
    if (!files || files->empty()) {
      return files ? Status::error("FileDescriptorSet contains no files") : files.status();
    }
    for (const auto& file_field : *files) {
      if (file_field.wire_type != ProtoReader::WireType::kLengthDelimited) {
        return Status::error("FileDescriptorSet file entry is not a message");
      }
      Status decoded = parseFile(file_field.bytes);
      if (!decoded.isOk()) {
        return decoded;
      }
    }

    if (root_type.empty()) {
      size_t root_count = 0;
      for (size_t index = 0; index < messages_.size(); ++index) {
        if (messages_[index].top_level) {
          root_index_ = index;
          ++root_count;
        }
      }
      if (root_count != 1) {
        return Status::error("protobuf root type is required when FileDescriptorSet has multiple top-level messages");
      }
      return Status::ok();
    }

    const std::string normalized = detail::normalizeProtoTypeName(root_type);
    auto root = findMessage(normalized);
    if (!root) {
      return root.status();
    }
    root_index_ = *root;
    return Status::ok();
  }

  [[nodiscard]] Status parseFile(ByteView bytes) {
    ProtoReader file(bytes);
    std::string package;
    auto packages = file.matching(2);
    if (!packages) {
      return packages.status();
    }
    if (!packages->empty()) {
      const auto& field = packages->back();
      if (field.wire_type != ProtoReader::WireType::kLengthDelimited) {
        return Status::error("FileDescriptorProto package is not a string");
      }
      package = detail::protoString(field.bytes);
    }
    auto declarations = file.matching(4);
    if (!declarations) {
      return declarations.status();
    }
    for (const auto& declaration : *declarations) {
      if (declaration.wire_type != ProtoReader::WireType::kLengthDelimited) {
        return Status::error("FileDescriptorProto message_type is not a message");
      }
      Status decoded = parseMessage(declaration.bytes, package, true, 1);
      if (!decoded.isOk()) {
        return decoded;
      }
    }
    return Status::ok();
  }

  [[nodiscard]] Status parseMessage(ByteView bytes, const std::string& parent, bool top_level, size_t depth) {
    if (depth > ProtoReader::kMaxRecursionDepth) {
      return Status::error("protobuf descriptor nesting depth exceeds 64");
    }
    ProtoReader message(bytes);
    auto name_field = message.last(1);
    if (!name_field || name_field->wire_type != ProtoReader::WireType::kLengthDelimited || name_field->bytes.empty()) {
      return Status::error("DescriptorProto is missing its name");
    }
    const std::string name = detail::protoString(name_field->bytes);
    const std::string full_name = parent.empty() ? name : parent + "." + name;
    for (const auto& existing : messages_) {
      if (existing.full_name == full_name) {
        return Status::error("FileDescriptorSet contains a duplicate message name");
      }
    }
    const size_t message_index = messages_.size();
    messages_.push_back(detail::ProtoSchemaMessage{full_name, {}, top_level});

    auto fields = message.matching(2);
    if (!fields) {
      return fields.status();
    }
    for (const auto& field : *fields) {
      if (field.wire_type != ProtoReader::WireType::kLengthDelimited) {
        return Status::error("DescriptorProto field entry is not a message");
      }
      auto decoded = parseField(field.bytes);
      if (!decoded) {
        return decoded.status();
      }
      messages_[message_index].fields.push_back(std::move(*decoded));
    }
    auto& decoded_fields = messages_[message_index].fields;
    std::sort(decoded_fields.begin(), decoded_fields.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.name < rhs.name;
    });
    for (size_t index = 1; index < decoded_fields.size(); ++index) {
      if (decoded_fields[index - 1].name == decoded_fields[index].name) {
        return Status::error("DescriptorProto contains a duplicate field name or number");
      }
    }
    std::sort(decoded_fields.begin(), decoded_fields.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.number < rhs.number;
    });
    for (size_t index = 1; index < decoded_fields.size(); ++index) {
      if (decoded_fields[index - 1].number == decoded_fields[index].number) {
        return Status::error("DescriptorProto contains a duplicate field name or number");
      }
    }

    auto nested = message.matching(3);
    if (!nested) {
      return nested.status();
    }
    for (const auto& declaration : *nested) {
      if (declaration.wire_type != ProtoReader::WireType::kLengthDelimited) {
        return Status::error("DescriptorProto nested_type is not a message");
      }
      Status decoded = parseMessage(declaration.bytes, full_name, false, depth + 1);
      if (!decoded.isOk()) {
        return decoded;
      }
    }
    return Status::ok();
  }

  [[nodiscard]] Expected<detail::ProtoSchemaField> parseField(ByteView bytes) const {
    ProtoReader field(bytes);
    auto name = field.last(1);
    auto number = field.varint(3);
    auto label = field.varint(4);
    auto type = field.varint(5);
    if (!name || name->wire_type != ProtoReader::WireType::kLengthDelimited || name->bytes.empty() || !number ||
        !label || !type) {
      return Status::error("FieldDescriptorProto is missing a required field");
    }
    if (*number == 0 || *number > UINT32_C(0x1FFFFFFF) || *label == 0 || *label > 3 || *type == 0 || *type > 18 ||
        *type == 10) {
      return Status::error("FieldDescriptorProto uses an unsupported field number, label, or type");
    }
    detail::ProtoSchemaField result;
    result.name = detail::protoString(name->bytes);
    result.number = static_cast<uint32_t>(*number);
    result.type = static_cast<uint32_t>(*type);
    result.repeated = *label == 3;
    if (result.type == 11) {
      auto type_name = field.last(6);
      if (!type_name || type_name->wire_type != ProtoReader::WireType::kLengthDelimited) {
        return Status::error("message FieldDescriptorProto is missing type_name");
      }
      result.type_name = detail::normalizeProtoTypeName(detail::protoString(type_name->bytes));
    }
    return result;
  }

  [[nodiscard]] Expected<size_t> findMessage(std::string_view full_name) const {
    for (size_t index = 0; index < messages_.size(); ++index) {
      if (messages_[index].full_name == full_name) {
        return index;
      }
    }
    return Status::error("protobuf message type is absent from FileDescriptorSet");
  }

  [[nodiscard]] Expected<detail::ProtoRequestedField> compilePath(std::string_view path) const {
    if (path.empty()) {
      return Status::error("protobuf field path is empty");
    }
    detail::ProtoRequestedField request;
    request.path = std::string(path);
    size_t message_index = root_index_;
    size_t position = 0;
    while (position < path.size()) {
      const size_t dot = path.find('.', position);
      const std::string_view component =
          path.substr(position, dot == std::string_view::npos ? path.size() - position : dot - position);
      if (component.empty()) {
        return Status::error("protobuf field path contains an empty component");
      }
      const detail::ProtoSchemaField* selected = nullptr;
      for (const auto& field : messages_[message_index].fields) {
        if (field.name == component) {
          selected = &field;
          break;
        }
      }
      if (selected == nullptr) {
        return Status::error("protobuf field path is absent from FileDescriptorSet");
      }
      request.numbers.push_back(selected->number);
      if (dot == std::string_view::npos) {
        return request;
      }
      if (selected->type != 11 || selected->repeated) {
        return Status::error("protobuf field path may descend only through singular message fields");
      }
      auto nested = findMessage(selected->type_name);
      if (!nested) {
        return nested.status();
      }
      message_index = *nested;
      position = dot + 1;
      if (request.numbers.size() >= ProtoReader::kMaxRecursionDepth) {
        return Status::error("protobuf field path depth exceeds 64");
      }
    }
    return Status::error("protobuf field path is malformed");
  }

  std::vector<detail::ProtoSchemaMessage> messages_;
  size_t root_index_ = 0;
  Status status_;
};

}  // namespace pj
