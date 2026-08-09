#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cdr_field_locator.hpp
 * @brief Bind-time ROS 2 .msg bundle compiler and cached CDR field traversal.
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pj_base/parser_module/cdr_reader.hpp"

namespace pj {
namespace detail {

enum class CdrValueKind : uint8_t {
  kBool,
  kI8,
  kU8,
  kI16,
  kU16,
  kI32,
  kU32,
  kI64,
  kU64,
  kF32,
  kF64,
  kString,
  kStruct,
};

enum class CdrContainerKind : uint8_t {
  kScalar,
  kFixedArray,
  kSequence,
};

struct CdrSchemaType {
  CdrValueKind kind = CdrValueKind::kU8;
  CdrContainerKind container = CdrContainerKind::kScalar;
  size_t fixed_count = 0;
  size_t maximum_container_count = 0;
  size_t maximum_string_length = 0;
  std::string nested_name;
  size_t nested_index = std::numeric_limits<size_t>::max();
};

struct CdrSchemaField {
  std::string name;
  CdrSchemaType type;
};

struct CdrSchemaStruct {
  std::string name;
  std::vector<CdrSchemaField> fields;
};

struct CdrRequestedField {
  std::string path;
  std::vector<size_t> steps;
  CdrReader::CachedKind cached_kind = CdrReader::CachedKind::kUnknown;
};

inline std::string_view trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

inline std::string normalizeRosType(std::string_view name) {
  const size_t msg = name.find("/msg/");
  if (msg == std::string_view::npos) {
    return std::string(name);
  }
  return std::string(name.substr(0, msg)) + "/" + std::string(name.substr(msg + 5));
}

inline Expected<size_t> parseDecimal(std::string_view text) {
  if (text.empty()) {
    return Status::error("array extent is empty");
  }
  size_t value = 0;
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return Status::error("array extent is not a decimal integer");
    }
    const size_t digit = static_cast<size_t>(character - '0');
    if (value > (std::numeric_limits<size_t>::max() - digit) / 10) {
      return Status::error("array extent overflows the host size type");
    }
    value = value * 10 + digit;
  }
  return value;
}

inline Expected<CdrSchemaType> parseCdrType(std::string_view spelling) {
  CdrSchemaType type;
  const size_t bracket = spelling.find('[');
  std::string_view base = bracket == std::string_view::npos ? spelling : spelling.substr(0, bracket);
  if (bracket != std::string_view::npos) {
    if (spelling.back() != ']' || spelling.find('[', bracket + 1) != std::string_view::npos) {
      return Status::error("unsupported ROS 2 array spelling");
    }
    const std::string_view extent = spelling.substr(bracket + 1, spelling.size() - bracket - 2);
    if (extent.empty()) {
      type.container = CdrContainerKind::kSequence;
    } else if (extent.substr(0, 2) == "<=") {
      auto count = parseDecimal(extent.substr(2));
      if (!count || *count == 0) {
        return Status::error("ROS 2 bounded sequences require a positive maximum");
      }
      type.container = CdrContainerKind::kSequence;
      type.maximum_container_count = *count;
    } else {
      auto count = parseDecimal(extent);
      if (!count || *count == 0) {
        return Status::error("ROS 2 fixed arrays require a positive extent");
      }
      type.container = CdrContainerKind::kFixedArray;
      type.fixed_count = *count;
    }
  }
  if (base == "wstring" || base.substr(0, 9) == "wstring<=") {
    return Status::error("wide ROS 2 strings are not supported by CdrFieldLocator");
  }
  if (base.substr(0, 8) == "string<=") {
    auto maximum = parseDecimal(base.substr(8));
    if (!maximum || *maximum == 0) {
      return Status::error("bounded ROS 2 strings require a positive maximum");
    }
    type.maximum_string_length = *maximum;
    base = "string";
  }

  if (base == "bool") {
    type.kind = CdrValueKind::kBool;
  } else if (base == "int8") {
    type.kind = CdrValueKind::kI8;
  } else if (base == "uint8" || base == "byte" || base == "char") {
    type.kind = CdrValueKind::kU8;
  } else if (base == "int16") {
    type.kind = CdrValueKind::kI16;
  } else if (base == "uint16") {
    type.kind = CdrValueKind::kU16;
  } else if (base == "int32") {
    type.kind = CdrValueKind::kI32;
  } else if (base == "uint32") {
    type.kind = CdrValueKind::kU32;
  } else if (base == "int64") {
    type.kind = CdrValueKind::kI64;
  } else if (base == "uint64") {
    type.kind = CdrValueKind::kU64;
  } else if (base == "float32") {
    type.kind = CdrValueKind::kF32;
  } else if (base == "float64") {
    type.kind = CdrValueKind::kF64;
  } else if (base == "string") {
    type.kind = CdrValueKind::kString;
  } else {
    type.kind = CdrValueKind::kStruct;
    type.nested_name = normalizeRosType(base);
  }
  return type;
}

inline CdrReader::CachedKind cachedKind(const CdrSchemaType& type) {
  if (type.container == CdrContainerKind::kSequence || type.container == CdrContainerKind::kFixedArray) {
    return type.kind == CdrValueKind::kU8 ? CdrReader::CachedKind::kBytes : CdrReader::CachedKind::kUnknown;
  }
  switch (type.kind) {
    case CdrValueKind::kBool:
      return CdrReader::CachedKind::kBool;
    case CdrValueKind::kI8:
      return CdrReader::CachedKind::kI8;
    case CdrValueKind::kU8:
      return CdrReader::CachedKind::kU8;
    case CdrValueKind::kI16:
      return CdrReader::CachedKind::kI16;
    case CdrValueKind::kU16:
      return CdrReader::CachedKind::kU16;
    case CdrValueKind::kI32:
      return CdrReader::CachedKind::kI32;
    case CdrValueKind::kU32:
      return CdrReader::CachedKind::kU32;
    case CdrValueKind::kI64:
      return CdrReader::CachedKind::kI64;
    case CdrValueKind::kU64:
      return CdrReader::CachedKind::kU64;
    case CdrValueKind::kF32:
      return CdrReader::CachedKind::kF32;
    case CdrValueKind::kF64:
      return CdrReader::CachedKind::kF64;
    case CdrValueKind::kString:
      return CdrReader::CachedKind::kString;
    default:
      return CdrReader::CachedKind::kUnknown;
  }
}

}  // namespace detail

class CdrTraversalPlan {
 public:
  CdrTraversalPlan() = default;

  [[nodiscard]] size_t size() const noexcept {
    return requested_.size();
  }

  [[nodiscard]] Expected<CdrFieldId> field(std::string_view path) const {
    for (size_t index = 0; index < requested_.size(); ++index) {
      if (requested_[index].path == path) {
        return index;
      }
    }
    return Status::error("field path is not present in the CDR traversal plan");
  }

 private:
  [[nodiscard]] Status locateAll(CdrReader& reader) const {
    if (!reader.status_.isOk()) {
      return reader.status_;
    }
    std::array<size_t, CdrReader::kMaxTraversalDepth> path{};
    Status result = walkStruct(reader, 0, 0, path);
    if (!result.isOk()) {
      return result;
    }
    for (const auto& cached : reader.cached_fields_) {
      if (!cached.located) {
        return reader.fail("requested CDR field was not reached during traversal");
      }
    }
    return Status::ok();
  }

  [[nodiscard]] Status walkStruct(
      CdrReader& reader, size_t struct_index, size_t depth,
      std::array<size_t, CdrReader::kMaxTraversalDepth>& path) const {
    if (depth >= CdrReader::kMaxTraversalDepth) {
      return reader.fail("CDR traversal depth exceeds 64");
    }
    const auto& structure = structs_[struct_index];
    for (size_t field_index = 0; field_index < structure.fields.size(); ++field_index) {
      path[depth] = field_index;
      size_t requested_id = std::numeric_limits<size_t>::max();
      bool has_descendant = false;
      for (size_t index = 0; index < requested_.size(); ++index) {
        const auto& request = requested_[index];
        if (request.steps.size() <= depth) {
          continue;
        }
        bool prefix_matches = true;
        for (size_t step = 0; step <= depth; ++step) {
          if (request.steps[step] != path[step]) {
            prefix_matches = false;
            break;
          }
        }
        if (prefix_matches && request.steps.size() == depth + 1) {
          requested_id = index;
        } else if (prefix_matches) {
          has_descendant = true;
        }
      }
      Status result = walkValue(reader, structure.fields[field_index].type, depth, path, requested_id, has_descendant);
      if (!result.isOk()) {
        return result;
      }
    }
    return Status::ok();
  }

  [[nodiscard]] Status walkValue(
      CdrReader& reader, const detail::CdrSchemaType& type, size_t depth,
      std::array<size_t, CdrReader::kMaxTraversalDepth>& path, size_t requested_id, bool has_descendant) const {
    if (has_descendant &&
        (type.kind != detail::CdrValueKind::kStruct || type.container != detail::CdrContainerKind::kScalar)) {
      return reader.fail("CDR field path descends through a non-scalar nested struct");
    }

    if (type.container == detail::CdrContainerKind::kSequence) {
      auto count = reader.readU32();
      if (!count) {
        return count.status();
      }
      if (type.maximum_container_count != 0 && *count > type.maximum_container_count) {
        return reader.fail("CDR bounded-sequence length exceeds its schema maximum");
      }
      if (type.kind == detail::CdrValueKind::kU8) {
        if (static_cast<uint64_t>(*count) > reader.remaining()) {
          return reader.fail("CDR byte-sequence length exceeds the remaining payload");
        }
        const size_t offset = reader.position_;
        reader.position_ += *count;
        if (requested_id != std::numeric_limits<size_t>::max()) {
          reader.cached_fields_[requested_id] =
              CdrReader::CachedField{CdrReader::CachedKind::kBytes, offset, *count, true};
        }
        return Status::ok();
      }
      auto minimum = minimumSize(type, 0);
      if (!minimum) {
        return reader.fail(minimum.status().message());
      }
      if (*count != 0 && *minimum == 0) {
        return reader.fail("CDR sequence element has zero serialized minimum size");
      }
      if (*minimum != 0 && static_cast<uint64_t>(*count) > reader.remaining() / *minimum) {
        return reader.fail("CDR sequence length exceeds the remaining payload");
      }
      detail::CdrSchemaType element = type;
      element.container = detail::CdrContainerKind::kScalar;
      for (uint32_t index = 0; index < *count; ++index) {
        const size_t before = reader.position_;
        Status result = walkValue(reader, element, depth, path, std::numeric_limits<size_t>::max(), false);
        if (!result.isOk()) {
          return result;
        }
        if (reader.position_ <= before) {
          return reader.fail("CDR sequence element did not consume input");
        }
      }
      return Status::ok();
    }

    if (type.container == detail::CdrContainerKind::kFixedArray) {
      if (type.kind == detail::CdrValueKind::kU8) {
        if (type.fixed_count > reader.remaining()) {
          return reader.fail("truncated CDR fixed byte array");
        }
        const size_t offset = reader.position_;
        reader.position_ += type.fixed_count;
        if (requested_id != std::numeric_limits<size_t>::max()) {
          reader.cached_fields_[requested_id] =
              CdrReader::CachedField{CdrReader::CachedKind::kBytes, offset, type.fixed_count, true};
        }
        return Status::ok();
      }
      detail::CdrSchemaType element = type;
      element.container = detail::CdrContainerKind::kScalar;
      auto minimum = minimumSize(element, 0);
      if (!minimum) {
        return reader.fail(minimum.status().message());
      }
      if (type.fixed_count != 0 && *minimum == 0) {
        return reader.fail("CDR fixed-array element has zero serialized minimum size");
      }
      for (size_t index = 0; index < type.fixed_count; ++index) {
        const size_t before = reader.position_;
        Status result = walkValue(reader, element, depth, path, std::numeric_limits<size_t>::max(), false);
        if (!result.isOk()) {
          return result;
        }
        if (reader.position_ <= before) {
          return reader.fail("CDR fixed-array element did not consume input");
        }
      }
      return Status::ok();
    }

    if (type.kind == detail::CdrValueKind::kStruct) {
      return walkStruct(reader, type.nested_index, depth + 1, path);
    }

    if (type.kind == detail::CdrValueKind::kString) {
      auto length = reader.readU32();
      if (!length) {
        return length.status();
      }
      if (*length == 0 || static_cast<uint64_t>(*length) > reader.remaining()) {
        return reader.fail("invalid CDR string length");
      }
      if (type.maximum_string_length != 0 && static_cast<size_t>(*length - 1) > type.maximum_string_length) {
        return reader.fail("CDR bounded-string length exceeds its schema maximum");
      }
      const size_t offset = reader.position_;
      if (reader.payload_.data[offset + *length - 1] != 0) {
        return reader.fail("CDR string is not NUL terminated");
      }
      reader.position_ += *length;
      if (requested_id != std::numeric_limits<size_t>::max()) {
        reader.cached_fields_[requested_id] =
            CdrReader::CachedField{CdrReader::CachedKind::kString, offset, *length - 1, true};
      }
      return Status::ok();
    }

    const size_t width = primitiveSize(type.kind);
    Status aligned = reader.align(width);
    if (!aligned.isOk()) {
      return aligned;
    }
    if (width > reader.remaining()) {
      return reader.fail("truncated CDR primitive while locating fields");
    }
    const size_t offset = reader.position_;
    if (type.kind == detail::CdrValueKind::kBool && reader.payload_.data[offset] > 1) {
      return reader.fail("CDR bool is not 0 or 1");
    }
    reader.position_ += width;
    if (requested_id != std::numeric_limits<size_t>::max()) {
      reader.cached_fields_[requested_id] = CdrReader::CachedField{detail::cachedKind(type), offset, width, true};
    }
    return Status::ok();
  }

  [[nodiscard]] Expected<size_t> minimumSize(const detail::CdrSchemaType& type, size_t depth) const {
    if (type.kind == detail::CdrValueKind::kString) {
      return size_t{5};
    }
    if (type.kind != detail::CdrValueKind::kStruct) {
      return primitiveSize(type.kind);
    }
    if (depth >= CdrReader::kMaxTraversalDepth) {
      return Status::error("CDR schema nesting depth exceeds 64");
    }
    size_t total = 0;
    for (const auto& field : structs_[type.nested_index].fields) {
      auto minimum = minimumSize(field.type, depth + 1);
      if (!minimum) {
        return minimum.status();
      }
      size_t field_minimum = *minimum;
      if (field.type.container == detail::CdrContainerKind::kSequence) {
        field_minimum = 4;
      } else if (field.type.container == detail::CdrContainerKind::kFixedArray) {
        if (field_minimum > std::numeric_limits<size_t>::max() / field.type.fixed_count) {
          return Status::error("CDR schema minimum size overflows the host size type");
        }
        field_minimum *= field.type.fixed_count;
      }
      if (field_minimum > std::numeric_limits<size_t>::max() - total) {
        return Status::error("CDR schema minimum size overflows the host size type");
      }
      total += field_minimum;
    }
    return total;
  }

  [[nodiscard]] static size_t primitiveSize(detail::CdrValueKind kind) {
    switch (kind) {
      case detail::CdrValueKind::kBool:
      case detail::CdrValueKind::kI8:
      case detail::CdrValueKind::kU8:
        return 1;
      case detail::CdrValueKind::kI16:
      case detail::CdrValueKind::kU16:
        return 2;
      case detail::CdrValueKind::kI32:
      case detail::CdrValueKind::kU32:
      case detail::CdrValueKind::kF32:
        return 4;
      case detail::CdrValueKind::kI64:
      case detail::CdrValueKind::kU64:
      case detail::CdrValueKind::kF64:
        return 8;
      default:
        return 1;
    }
  }

  std::vector<detail::CdrSchemaStruct> structs_;
  std::vector<detail::CdrRequestedField> requested_;

  friend class CdrFieldLocator;
  friend class CdrReader;
};

class CdrFieldLocator {
 public:
  explicit CdrFieldLocator(std::string_view schema) {
    PJ_PARSER_MODULE_TRY {
      status_ = parse(schema);
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      status_ = Status::error("allocation failed while compiling the ROS 2 schema");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      status_ = Status::error("unexpected failure while compiling the ROS 2 schema");
    }
  }

  [[nodiscard]] const Status& status() const noexcept {
    return status_;
  }

  [[nodiscard]] Expected<CdrTraversalPlan> locate(std::initializer_list<std::string_view> paths) const {
    PJ_PARSER_MODULE_TRY {
      return locate(std::vector<std::string_view>(paths));
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return Status::error("allocation failed while compiling CDR field paths");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return Status::error("unexpected failure while compiling CDR field paths");
    }
  }

  [[nodiscard]] Expected<CdrTraversalPlan> locate(const std::vector<std::string_view>& paths) const {
    if (!status_.isOk()) {
      return status_;
    }
    PJ_PARSER_MODULE_TRY {
      CdrTraversalPlan plan;
      plan.structs_ = structs_;
      for (const auto& path : paths) {
        auto request = compilePath(plan.structs_, path);
        if (!request) {
          return request.status();
        }
        for (const auto& existing : plan.requested_) {
          if (existing.path == request->path) {
            return Status::error("duplicate CDR field path requested");
          }
        }
        plan.requested_.push_back(std::move(*request));
      }
      if (plan.requested_.empty()) {
        return Status::error("at least one CDR field path is required");
      }
      return plan;
    }
    PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
      return Status::error("allocation failed while compiling CDR field paths");
    }
    PJ_PARSER_MODULE_CATCH_ALL {
      return Status::error("unexpected failure while compiling CDR field paths");
    }
  }

 private:
  [[nodiscard]] Status parse(std::string_view schema) {
    if (schema.empty()) {
      return Status::error("ROS 2 .msg schema bundle is empty");
    }
    structs_.clear();
    structs_.push_back(detail::CdrSchemaStruct{"", {}});
    size_t current = 0;
    size_t position = 0;
    while (position <= schema.size()) {
      const size_t end = schema.find('\n', position);
      std::string_view line =
          schema.substr(position, end == std::string_view::npos ? schema.size() - position : end - position);
      position = end == std::string_view::npos ? schema.size() + 1 : end + 1;
      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }
      const size_t comment = line.find('#');
      if (comment != std::string_view::npos) {
        line = line.substr(0, comment);
      }
      line = detail::trim(line);
      if (line.empty() || line.find("===") == 0) {
        continue;
      }
      if (line.find("---") == 0) {
        return Status::error("ROS 2 service/action schemas are not supported by CdrFieldLocator");
      }
      if (line.find("MSG:") == 0) {
        const std::string name = detail::normalizeRosType(detail::trim(line.substr(4)));
        if (name.empty()) {
          return Status::error("ROS 2 schema bundle contains an empty MSG name");
        }
        if (structs_.size() == 1 && structs_[0].fields.empty() && structs_[0].name.empty()) {
          structs_[0].name = name;
          current = 0;
        } else {
          for (const auto& structure : structs_) {
            if (detail::normalizeRosType(structure.name) == name) {
              return Status::error("ROS 2 schema bundle contains a duplicate MSG name");
            }
          }
          structs_.push_back(detail::CdrSchemaStruct{name, {}});
          current = structs_.size() - 1;
        }
        continue;
      }
      const size_t split = line.find_first_of(" \t");
      if (split == std::string_view::npos) {
        return Status::error("malformed ROS 2 field declaration");
      }
      const std::string_view type_spelling = line.substr(0, split);
      std::string_view remainder = detail::trim(line.substr(split + 1));
      const size_t name_end = remainder.find_first_of(" \t=");
      const std::string_view field_name = remainder.substr(0, name_end);
      if (field_name.empty()) {
        return Status::error("ROS 2 field declaration has no name");
      }
      if (remainder.find('=') != std::string_view::npos) {
        continue;
      }
      auto type = detail::parseCdrType(type_spelling);
      if (!type) {
        return type.status();
      }
      for (const auto& field : structs_[current].fields) {
        if (field.name == field_name) {
          return Status::error("ROS 2 schema type contains a duplicate field name");
        }
      }
      structs_[current].fields.push_back(detail::CdrSchemaField{std::string(field_name), std::move(*type)});
    }
    if (structs_[0].fields.empty()) {
      return Status::error("ROS 2 .msg root type has no fields");
    }
    for (auto& structure : structs_) {
      for (auto& field : structure.fields) {
        if (field.type.kind != detail::CdrValueKind::kStruct) {
          continue;
        }
        auto nested = findStruct(field.type.nested_name);
        if (!nested) {
          return nested.status();
        }
        field.type.nested_index = *nested;
      }
    }
    std::vector<uint8_t> visit_state(structs_.size(), 0);
    for (size_t index = 0; index < structs_.size(); ++index) {
      Status validated = validateSchemaGraph(index, visit_state, 0);
      if (!validated.isOk()) {
        return validated;
      }
    }
    return Status::ok();
  }

  [[nodiscard]] Status validateSchemaGraph(size_t struct_index, std::vector<uint8_t>& visit_state, size_t depth) const {
    if (depth >= CdrReader::kMaxTraversalDepth) {
      return Status::error("ROS 2 schema nesting depth exceeds 64");
    }
    if (visit_state[struct_index] == 1) {
      return Status::error("ROS 2 schema contains a cyclic nested type");
    }
    if (visit_state[struct_index] == 2) {
      return Status::ok();
    }
    visit_state[struct_index] = 1;
    for (const auto& field : structs_[struct_index].fields) {
      if (field.type.kind != detail::CdrValueKind::kStruct) {
        continue;
      }
      Status nested = validateSchemaGraph(field.type.nested_index, visit_state, depth + 1);
      if (!nested.isOk()) {
        return nested;
      }
    }
    visit_state[struct_index] = 2;
    return Status::ok();
  }

  [[nodiscard]] Expected<size_t> findStruct(std::string_view name) const {
    size_t match = std::numeric_limits<size_t>::max();
    for (size_t index = 0; index < structs_.size(); ++index) {
      const std::string normalized = detail::normalizeRosType(structs_[index].name);
      const size_t slash = normalized.rfind('/');
      const std::string_view short_name =
          slash == std::string::npos ? std::string_view(normalized) : std::string_view(normalized).substr(slash + 1);
      if (normalized == name || short_name == name) {
        if (match != std::numeric_limits<size_t>::max()) {
          return Status::error("ROS 2 nested type name is ambiguous in the schema bundle");
        }
        match = index;
      }
    }
    if (match == std::numeric_limits<size_t>::max()) {
      return Status::error("ROS 2 nested type is missing from the concatenated schema bundle");
    }
    return match;
  }

  [[nodiscard]] static Expected<detail::CdrRequestedField> compilePath(
      const std::vector<detail::CdrSchemaStruct>& structs, std::string_view path) {
    if (path.empty()) {
      return Status::error("CDR field path is empty");
    }
    detail::CdrRequestedField request;
    request.path = std::string(path);
    size_t struct_index = 0;
    size_t position = 0;
    while (position < path.size()) {
      const size_t dot = path.find('.', position);
      const std::string_view component =
          path.substr(position, dot == std::string_view::npos ? path.size() - position : dot - position);
      if (component.empty()) {
        return Status::error("CDR field path contains an empty component");
      }
      const auto& fields = structs[struct_index].fields;
      size_t field_index = std::numeric_limits<size_t>::max();
      for (size_t index = 0; index < fields.size(); ++index) {
        if (fields[index].name == component) {
          field_index = index;
          break;
        }
      }
      if (field_index == std::numeric_limits<size_t>::max()) {
        return Status::error("CDR field path is absent from the ROS 2 schema");
      }
      request.steps.push_back(field_index);
      const auto& type = fields[field_index].type;
      if (dot == std::string_view::npos) {
        request.cached_kind = detail::cachedKind(type);
        if (request.cached_kind == CdrReader::CachedKind::kUnknown) {
          return Status::error("requested CDR terminal field has an unsupported type");
        }
        return request;
      }
      if (type.kind != detail::CdrValueKind::kStruct || type.container != detail::CdrContainerKind::kScalar) {
        return Status::error("CDR field path may descend only through scalar nested structs");
      }
      struct_index = type.nested_index;
      position = dot + 1;
      if (request.steps.size() >= CdrReader::kMaxTraversalDepth) {
        return Status::error("CDR field path depth exceeds 64");
      }
    }
    return Status::error("CDR field path is malformed");
  }

  std::vector<detail::CdrSchemaStruct> structs_;
  Status status_;
};

inline CdrReader::CdrReader(PayloadView payload, const CdrTraversalPlan& plan) : payload_(payload), plan_(&plan) {
  initialize();
  if (!status_.isOk()) {
    return;
  }
  PJ_PARSER_MODULE_TRY {
    cached_fields_.resize(plan.size());
  }
  PJ_PARSER_MODULE_CATCH_BAD_ALLOC {
    status_ = Status::error("allocation failed while preparing CDR field cache");
  }
  PJ_PARSER_MODULE_CATCH_ALL {
    status_ = Status::error("unexpected failure while preparing CDR field cache");
  }
}

inline Status CdrReader::ensurePlannedFields() {
  if (!status_.isOk()) {
    return status_;
  }
  if (plan_ == nullptr) {
    return fail("CDR field access requires a traversal plan");
  }
  if (traversal_count_ == 0) {
    ++traversal_count_;
    position_ = data_origin_;
    depth_ = 0;
    status_ = plan_->locateAll(*this);
  }
  return status_;
}

inline Expected<CdrReader::CachedField> CdrReader::cached(CdrFieldId field, CachedKind expected) {
  Status located = ensurePlannedFields();
  if (!located.isOk()) {
    return located;
  }
  if (field >= cached_fields_.size()) {
    return fail("CDR field id is outside the traversal plan");
  }
  const CachedField value = cached_fields_[field];
  if (!value.located || value.kind != expected) {
    return fail("CDR field accessor does not match the planned field type");
  }
  return value;
}

inline Expected<uint64_t> CdrReader::cachedUnsigned(CdrFieldId field, CachedKind kind, size_t width) {
  auto value = cached(field, kind);
  if (!value) {
    return value.status();
  }
  uint64_t result = 0;
  if (little_endian_) {
    for (size_t index = 0; index < width; ++index) {
      result |= static_cast<uint64_t>(payload_.data[value->offset + index]) << (index * 8);
    }
  } else {
    for (size_t index = 0; index < width; ++index) {
      result = (result << 8) | payload_.data[value->offset + index];
    }
  }
  return result;
}

inline Expected<uint32_t> CdrReader::u32(CdrFieldId field) {
  auto value = cachedUnsigned(field, CachedKind::kU32, 4);
  return value ? Expected<uint32_t>(static_cast<uint32_t>(*value)) : Expected<uint32_t>(value.status());
}

inline Expected<bool> CdrReader::boolean(CdrFieldId field) {
  auto value = cachedUnsigned(field, CachedKind::kBool, 1);
  if (!value) {
    return value.status();
  }
  if (*value > 1) {
    return fail("CDR bool is not 0 or 1");
  }
  return *value != 0;
}

inline Expected<int8_t> CdrReader::i8(CdrFieldId field) {
  return cachedBits<int8_t, uint8_t>(field, CachedKind::kI8);
}

inline Expected<uint8_t> CdrReader::u8(CdrFieldId field) {
  auto value = cachedUnsigned(field, CachedKind::kU8, 1);
  return value ? Expected<uint8_t>(static_cast<uint8_t>(*value)) : Expected<uint8_t>(value.status());
}

inline Expected<int16_t> CdrReader::i16(CdrFieldId field) {
  return cachedBits<int16_t, uint16_t>(field, CachedKind::kI16);
}

inline Expected<uint16_t> CdrReader::u16(CdrFieldId field) {
  auto value = cachedUnsigned(field, CachedKind::kU16, 2);
  return value ? Expected<uint16_t>(static_cast<uint16_t>(*value)) : Expected<uint16_t>(value.status());
}

inline Expected<int32_t> CdrReader::i32(CdrFieldId field) {
  return cachedBits<int32_t, uint32_t>(field, CachedKind::kI32);
}

inline Expected<uint64_t> CdrReader::u64(CdrFieldId field) {
  return cachedUnsigned(field, CachedKind::kU64, 8);
}

inline Expected<int64_t> CdrReader::i64(CdrFieldId field) {
  return cachedBits<int64_t, uint64_t>(field, CachedKind::kI64);
}

inline Expected<float> CdrReader::f32(CdrFieldId field) {
  return cachedBits<float, uint32_t>(field, CachedKind::kF32);
}

inline Expected<double> CdrReader::f64(CdrFieldId field) {
  return cachedBits<double, uint64_t>(field, CachedKind::kF64);
}

inline Expected<std::string_view> CdrReader::string(CdrFieldId field) {
  auto value = cached(field, CachedKind::kString);
  if (!value) {
    return value.status();
  }
  return std::string_view(reinterpret_cast<const char*>(payload_.data + value->offset), value->size);
}

inline Expected<ByteView> CdrReader::bytes(CdrFieldId field) {
  auto value = cached(field, CachedKind::kBytes);
  if (!value) {
    return value.status();
  }
  return ByteView(payload_.data + value->offset, value->size);
}

inline Expected<InputSpanRef> CdrReader::spanRef(CdrFieldId field) {
  auto value = cached(field, CachedKind::kBytes);
  if (!value) {
    return value.status();
  }
  return InputSpanRef{static_cast<uint64_t>(value->offset), static_cast<uint64_t>(value->size)};
}

}  // namespace pj
