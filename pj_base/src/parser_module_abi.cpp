// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/parser_module_abi.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace PJ::parser_module {
namespace {

constexpr size_t kBindingHeaderSize = 16;
constexpr size_t kBindingFieldCountV1 = 6;
constexpr size_t kBindingFieldDescriptorSize = 8;
constexpr size_t kParseInputHeaderSize = 24;
constexpr size_t kObjectOutputFixedSize = 36;
constexpr size_t kScalarOutputFixedSize = 24;
constexpr size_t kScalarFieldPrefixSize = 9;

enum class ScalarValueKind : uint8_t {
  kFloat64 = PJ_MODULE_SCALAR_VALUE_F64,
  kInt64 = PJ_MODULE_SCALAR_VALUE_I64,
  kUint64 = PJ_MODULE_SCALAR_VALUE_U64,
  kBool = PJ_MODULE_SCALAR_VALUE_BOOL,
  kString = PJ_MODULE_SCALAR_VALUE_STRING,
};

template <typename T>
Expected<T> malformed(std::string message) {
  return unexpected(std::string("malformed parser module block: ") + std::move(message));
}

[[nodiscard]] bool checkedAdd(size_t lhs, size_t rhs, size_t* out) {
  if (rhs > std::numeric_limits<size_t>::max() - lhs) {
    return false;
  }
  *out = lhs + rhs;
  return true;
}

// GCC 15 at -O3 reports a -Wfree-nonheap-object false positive when
// std::vector<uint8_t>::push_back's reallocation path is inlined into the
// little-endian writer below (the freed pointer is the vector's own heap
// block). Debug and -O2 builds are clean, so this only surfaced in Release
// package builds. Clang has no such warning group.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif

class ByteWriter {
 public:
  explicit ByteWriter(size_t reserve) {
    bytes_.reserve(reserve);
  }

  void u8(uint8_t value) {
    bytes_.push_back(value);
  }

  void u16(uint16_t value) {
    littleEndian(value);
  }

  void u32(uint32_t value) {
    littleEndian(value);
  }

  void u64(uint64_t value) {
    littleEndian(value);
  }

  void i64(int64_t value) {
    u64(std::bit_cast<uint64_t>(value));
  }

  void f64(double value) {
    u64(std::bit_cast<uint64_t>(value));
  }

  void zeros(size_t count) {
    bytes_.insert(bytes_.end(), count, uint8_t{0});
  }

  void bytes(Span<const uint8_t> value) {
    if (!value.empty()) {
      bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
  }

  void string(std::string_view value) {
    if (!value.empty()) {
      const auto* begin = reinterpret_cast<const uint8_t*>(value.data());
      bytes_.insert(bytes_.end(), begin, begin + value.size());
    }
  }

  [[nodiscard]] std::vector<uint8_t> finish() && {
    return std::move(bytes_);
  }

 private:
  /// Every v1 block encodes unsigned integers little-endian, least significant
  /// byte first.
  template <typename Unsigned>
  void littleEndian(Unsigned value) {
    for (unsigned shift = 0; shift < sizeof(Unsigned) * 8U; shift += 8U) {
      bytes_.push_back(static_cast<uint8_t>((value >> shift) & UINT64_C(0xFF)));
    }
  }

  std::vector<uint8_t> bytes_;
};

class ByteReader {
 public:
  explicit ByteReader(Span<const uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool u8(uint8_t* out) {
    if (remaining() < 1) {
      return false;
    }
    *out = bytes_[position_++];
    return true;
  }

  [[nodiscard]] bool u16(uint16_t* out) {
    return littleEndian(out);
  }

  [[nodiscard]] bool u32(uint32_t* out) {
    return littleEndian(out);
  }

  [[nodiscard]] bool u64(uint64_t* out) {
    return littleEndian(out);
  }

  [[nodiscard]] bool i64(int64_t* out) {
    uint64_t value = 0;
    if (!u64(&value)) {
      return false;
    }
    *out = std::bit_cast<int64_t>(value);
    return true;
  }

  [[nodiscard]] bool f64(double* out) {
    uint64_t value = 0;
    if (!u64(&value)) {
      return false;
    }
    *out = std::bit_cast<double>(value);
    return true;
  }

  [[nodiscard]] bool skip(size_t count) {
    if (count > remaining()) {
      return false;
    }
    position_ += count;
    return true;
  }

  [[nodiscard]] bool take(size_t count, Span<const uint8_t>* out) {
    if (count > remaining()) {
      return false;
    }
    *out = bytes_.subspan(position_, count);
    position_ += count;
    return true;
  }

  [[nodiscard]] size_t remaining() const {
    return bytes_.size() - position_;
  }

 private:
  /// Mirror of ByteWriter::littleEndian: least significant byte first.
  template <typename Unsigned>
  [[nodiscard]] bool littleEndian(Unsigned* out) {
    if (remaining() < sizeof(Unsigned)) {
      return false;
    }
    uint64_t value = 0;
    for (unsigned index = 0; index < sizeof(Unsigned); ++index) {
      value |= static_cast<uint64_t>(bytes_[position_ + index]) << (index * 8U);
    }
    position_ += sizeof(Unsigned);
    *out = static_cast<Unsigned>(value);
    return true;
  }

  Span<const uint8_t> bytes_;
  size_t position_ = 0;
};

[[nodiscard]] bool validRoute(uint16_t route) {
  return route == static_cast<uint16_t>(Route::kScalar) || route == static_cast<uint16_t>(Route::kObject);
}

[[nodiscard]] std::string_view asStringView(Span<const uint8_t> bytes) {
  if (bytes.empty()) {
    return {};
  }
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] Expected<size_t> scalarValueSize(const ScalarValueV1& value) {
  if (std::holds_alternative<double>(value) || std::holds_alternative<int64_t>(value) ||
      std::holds_alternative<uint64_t>(value)) {
    return size_t{8};
  }
  if (std::holds_alternative<bool>(value)) {
    return size_t{1};
  }
  const auto string_value = std::get<std::string_view>(value);
  if (string_value.size() > std::numeric_limits<uint32_t>::max()) {
    return unexpected(std::string("scalar string exceeds the v1 uint32 length limit"));
  }
  size_t size = 0;
  if (!checkedAdd(size_t{4}, string_value.size(), &size)) {
    return unexpected(std::string("scalar string size overflows the host size type"));
  }
  return size;
}

}  // namespace

Expected<std::vector<uint8_t>> writeBindingInfoV1(const BindingInfoV1& info) {
  const uint16_t route = static_cast<uint16_t>(info.route);
  if (!validRoute(route)) {
    return unexpected(std::string("BindingInfo v1 route must be scalar or object"));
  }

  const std::array<Span<const uint8_t>, kBindingFieldCountV1> fields{
      info.encoding, info.type_name, info.schema, info.claim_id, info.config_json, info.schema_digest};
  std::array<uint32_t, kBindingFieldCountV1> offsets{};
  size_t total_size = kBindingHeaderSize + kBindingFieldCountV1 * kBindingFieldDescriptorSize;
  for (size_t index = 0; index < fields.size(); ++index) {
    if (fields[index].size() > std::numeric_limits<uint32_t>::max() ||
        total_size > std::numeric_limits<uint32_t>::max()) {
      return unexpected(std::string("BindingInfo v1 field exceeds the uint32 offset/length limit"));
    }
    offsets[index] = static_cast<uint32_t>(total_size);
    if (!checkedAdd(total_size, fields[index].size(), &total_size)) {
      return unexpected(std::string("BindingInfo v1 size overflows the host size type"));
    }
  }
  if (total_size > std::numeric_limits<uint32_t>::max()) {
    return unexpected(std::string("BindingInfo v1 block exceeds the uint32 offset range"));
  }

  ByteWriter writer(total_size);
  writer.u16(kBindingInfoVersionV1);
  writer.u16(route);
  writer.u32(info.claim_index);
  writer.u16(info.expected_object_type);
  writer.u16(0);
  writer.u32(static_cast<uint32_t>(kBindingFieldCountV1));
  for (size_t index = 0; index < fields.size(); ++index) {
    writer.u32(offsets[index]);
    writer.u32(static_cast<uint32_t>(fields[index].size()));
  }
  for (const auto& field : fields) {
    writer.bytes(field);
  }
  return std::move(writer).finish();
}

Expected<BindingInfoV1> readBindingInfoV1(Span<const uint8_t> bytes) {
  ByteReader reader(bytes);
  uint16_t version = 0;
  uint16_t route = 0;
  uint32_t claim_index = 0;
  uint16_t expected_object_type = 0;
  uint32_t field_count = 0;
  if (!reader.u16(&version) || !reader.u16(&route) || !reader.u32(&claim_index) || !reader.u16(&expected_object_type) ||
      !reader.skip(2) /* reserved */ || !reader.u32(&field_count)) {
    return malformed<BindingInfoV1>("truncated BindingInfo v1 header");
  }
  if (version != kBindingInfoVersionV1) {
    return malformed<BindingInfoV1>("unsupported BindingInfo version");
  }
  if (!validRoute(route)) {
    return malformed<BindingInfoV1>("invalid BindingInfo route");
  }
  if (field_count < kBindingFieldCountV1) {
    return malformed<BindingInfoV1>("BindingInfo v1 has fewer than six fields");
  }
  if (field_count > reader.remaining() / kBindingFieldDescriptorSize) {
    return malformed<BindingInfoV1>("truncated BindingInfo field table");
  }

  std::array<Span<const uint8_t>, kBindingFieldCountV1> fields{};
  for (uint32_t index = 0; index < field_count; ++index) {
    uint32_t offset = 0;
    uint32_t length = 0;
    if (!reader.u32(&offset) || !reader.u32(&length)) {
      return malformed<BindingInfoV1>("truncated BindingInfo field descriptor");
    }
    const size_t field_offset = offset;
    const size_t field_length = length;
    if (field_offset > bytes.size() || field_length > bytes.size() - field_offset) {
      return malformed<BindingInfoV1>("BindingInfo field range is outside the block");
    }
    if (index < kBindingFieldCountV1) {
      fields[index] = bytes.subspan(field_offset, field_length);
    }
  }

  return BindingInfoV1{
      .route = static_cast<Route>(route),
      .claim_index = claim_index,
      .expected_object_type = expected_object_type,
      .encoding = fields[0],
      .type_name = fields[1],
      .schema = fields[2],
      .claim_id = fields[3],
      .config_json = fields[4],
      .schema_digest = fields[5],
  };
}

Expected<std::vector<uint8_t>> writeParseInputV1(const ParseInputV1& input) {
  size_t total_size = 0;
  if (!checkedAdd(kParseInputHeaderSize, input.payload.size(), &total_size)) {
    return unexpected(std::string("parse-input v1 size overflows the host size type"));
  }
  ByteWriter writer(total_size);
  writer.u8(input.has_timestamp ? uint8_t{1} : uint8_t{0});
  writer.zeros(7);
  writer.i64(input.timestamp_ns);
  writer.u64(static_cast<uint64_t>(input.payload.size()));
  writer.bytes(input.payload);
  return std::move(writer).finish();
}

Expected<ParseInputV1> readParseInputV1(Span<const uint8_t> bytes) {
  ByteReader reader(bytes);
  uint8_t flags = 0;
  int64_t timestamp_ns = 0;
  uint64_t payload_length = 0;
  if (!reader.u8(&flags) || !reader.skip(7) || !reader.i64(&timestamp_ns) || !reader.u64(&payload_length)) {
    return malformed<ParseInputV1>("truncated parse-input v1 header");
  }
  if ((flags & uint8_t{0xFE}) != 0) {
    return malformed<ParseInputV1>("parse-input v1 has unknown flag bits");
  }
  if (payload_length != reader.remaining()) {
    return malformed<ParseInputV1>("parse-input payload length does not match the block");
  }
  Span<const uint8_t> payload;
  if (!reader.take(static_cast<size_t>(payload_length), &payload)) {
    return malformed<ParseInputV1>("truncated parse-input payload");
  }
  return ParseInputV1{.has_timestamp = (flags & uint8_t{1}) != 0, .timestamp_ns = timestamp_ns, .payload = payload};
}

Expected<std::vector<uint8_t>> writeOutputDescriptorV1(const OutputDescriptorV1& output) {
  if (const auto* object = std::get_if<ObjectOutputV1>(&output)) {
    size_t total_size = 0;
    if (!checkedAdd(kObjectOutputFixedSize, object->wire.size(), &total_size)) {
      return unexpected(std::string("object output v1 size overflows the host size type"));
    }
    ByteWriter writer(total_size);
    writer.u16(kOutputDescriptorVersionV1);
    writer.u8(static_cast<uint8_t>(Route::kObject));
    writer.u8(0);
    writer.u16(object->object_type);
    writer.u16(object->splice.has_value() ? uint16_t{1} : uint16_t{0});
    writer.u32(object->splice.has_value() ? object->splice->field_number : 0);
    writer.u64(object->splice.has_value() ? object->splice->input_offset : 0);
    writer.u64(object->splice.has_value() ? object->splice->input_length : 0);
    writer.u64(static_cast<uint64_t>(object->wire.size()));
    writer.bytes(object->wire);
    return std::move(writer).finish();
  }

  const auto& scalar = std::get<ScalarOutputV1>(output);
  if (scalar.fields.size() > std::numeric_limits<uint32_t>::max()) {
    return unexpected(std::string("scalar output v1 has too many fields"));
  }

  size_t values_end = kScalarOutputFixedSize;
  for (const auto& field : scalar.fields) {
    const auto value_size = scalarValueSize(field.value);
    if (!value_size) {
      return unexpected(value_size.error());
    }
    if (field.name.size() > std::numeric_limits<uint32_t>::max() ||
        !checkedAdd(values_end, kScalarFieldPrefixSize, &values_end) ||
        !checkedAdd(values_end, *value_size, &values_end)) {
      return unexpected(std::string("scalar output v1 field size exceeds its encoded range"));
    }
  }

  std::vector<uint32_t> name_offsets;
  name_offsets.reserve(scalar.fields.size());
  size_t total_size = values_end;
  for (const auto& field : scalar.fields) {
    if (total_size > std::numeric_limits<uint32_t>::max()) {
      return unexpected(std::string("scalar output v1 name offset exceeds uint32"));
    }
    name_offsets.push_back(static_cast<uint32_t>(total_size));
    if (!checkedAdd(total_size, field.name.size(), &total_size) || total_size > std::numeric_limits<uint32_t>::max()) {
      return unexpected(std::string("scalar output v1 block exceeds the uint32 name-offset range"));
    }
  }

  ByteWriter writer(total_size);
  writer.u16(kOutputDescriptorVersionV1);
  writer.u8(static_cast<uint8_t>(Route::kScalar));
  writer.u8(0);
  writer.u8(scalar.has_timestamp ? uint8_t{1} : uint8_t{0});
  writer.zeros(7);
  writer.i64(scalar.timestamp_ns);
  writer.u32(static_cast<uint32_t>(scalar.fields.size()));
  for (size_t index = 0; index < scalar.fields.size(); ++index) {
    const auto& field = scalar.fields[index];
    writer.u32(name_offsets[index]);
    writer.u32(static_cast<uint32_t>(field.name.size()));
    if (const auto* float_value = std::get_if<double>(&field.value)) {
      writer.u8(static_cast<uint8_t>(ScalarValueKind::kFloat64));
      writer.f64(*float_value);
    } else if (const auto* signed_value = std::get_if<int64_t>(&field.value)) {
      writer.u8(static_cast<uint8_t>(ScalarValueKind::kInt64));
      writer.i64(*signed_value);
    } else if (const auto* unsigned_value = std::get_if<uint64_t>(&field.value)) {
      writer.u8(static_cast<uint8_t>(ScalarValueKind::kUint64));
      writer.u64(*unsigned_value);
    } else if (const auto* bool_value = std::get_if<bool>(&field.value)) {
      writer.u8(static_cast<uint8_t>(ScalarValueKind::kBool));
      writer.u8(*bool_value ? uint8_t{1} : uint8_t{0});
    } else {
      const auto string_value = std::get<std::string_view>(field.value);
      writer.u8(static_cast<uint8_t>(ScalarValueKind::kString));
      writer.u32(static_cast<uint32_t>(string_value.size()));
      writer.string(string_value);
    }
  }
  for (const auto& field : scalar.fields) {
    writer.string(field.name);
  }
  return std::move(writer).finish();
}

Expected<OutputDescriptorV1> readOutputDescriptorV1(Span<const uint8_t> bytes) {
  ByteReader reader(bytes);
  uint16_t version = 0;
  uint8_t route = 0;
  if (!reader.u16(&version) || !reader.u8(&route) || !reader.skip(1) /* reserved */) {
    return malformed<OutputDescriptorV1>("truncated output descriptor header");
  }
  if (version != kOutputDescriptorVersionV1) {
    return malformed<OutputDescriptorV1>("unsupported output descriptor version");
  }

  if (route == static_cast<uint8_t>(Route::kObject)) {
    uint16_t object_type = 0;
    uint16_t splice_count = 0;
    uint32_t field_number = 0;
    uint64_t input_offset = 0;
    uint64_t input_length = 0;
    uint64_t wire_length = 0;
    if (!reader.u16(&object_type) || !reader.u16(&splice_count) || !reader.u32(&field_number) ||
        !reader.u64(&input_offset) || !reader.u64(&input_length) || !reader.u64(&wire_length)) {
      return malformed<OutputDescriptorV1>("truncated object output descriptor");
    }
    if (splice_count > 1) {
      return malformed<OutputDescriptorV1>("object output splice_count is not 0 or 1");
    }
    if (wire_length != reader.remaining()) {
      return malformed<OutputDescriptorV1>("object output wire length does not match the block");
    }
    Span<const uint8_t> wire;
    if (!reader.take(static_cast<size_t>(wire_length), &wire)) {
      return malformed<OutputDescriptorV1>("truncated object output wire bytes");
    }
    std::optional<ObjectSpliceV1> splice;
    if (splice_count == 1) {
      splice = ObjectSpliceV1{
          .field_number = field_number,
          .input_offset = input_offset,
          .input_length = input_length,
      };
    }
    return OutputDescriptorV1(ObjectOutputV1{.object_type = object_type, .splice = splice, .wire = wire});
  }

  if (route != static_cast<uint8_t>(Route::kScalar)) {
    return malformed<OutputDescriptorV1>("invalid output descriptor route");
  }

  uint8_t has_timestamp = 0;
  int64_t timestamp_ns = 0;
  uint32_t field_count = 0;
  if (!reader.u8(&has_timestamp) || !reader.skip(7) || !reader.i64(&timestamp_ns) || !reader.u32(&field_count)) {
    return malformed<OutputDescriptorV1>("truncated scalar output descriptor");
  }
  if (has_timestamp > 1) {
    return malformed<OutputDescriptorV1>("scalar output has_timestamp is not 0 or 1");
  }
  if (field_count > reader.remaining() / (kScalarFieldPrefixSize + 1)) {
    return malformed<OutputDescriptorV1>("scalar output field count exceeds the block");
  }

  struct DecodedField {
    uint32_t name_offset;
    uint32_t name_length;
    ScalarValueV1 value;
  };
  std::vector<DecodedField> decoded_fields;
  decoded_fields.reserve(field_count);
  for (uint32_t index = 0; index < field_count; ++index) {
    uint32_t name_offset = 0;
    uint32_t name_length = 0;
    uint8_t value_kind = 0;
    if (!reader.u32(&name_offset) || !reader.u32(&name_length) || !reader.u8(&value_kind)) {
      return malformed<OutputDescriptorV1>("truncated scalar field descriptor");
    }

    ScalarValueV1 value;
    switch (static_cast<ScalarValueKind>(value_kind)) {
      case ScalarValueKind::kFloat64: {
        double decoded = 0;
        if (!reader.f64(&decoded)) {
          return malformed<OutputDescriptorV1>("truncated scalar float64 value");
        }
        value = decoded;
        break;
      }
      case ScalarValueKind::kInt64: {
        int64_t decoded = 0;
        if (!reader.i64(&decoded)) {
          return malformed<OutputDescriptorV1>("truncated scalar int64 value");
        }
        value = decoded;
        break;
      }
      case ScalarValueKind::kUint64: {
        uint64_t decoded = 0;
        if (!reader.u64(&decoded)) {
          return malformed<OutputDescriptorV1>("truncated scalar uint64 value");
        }
        value = decoded;
        break;
      }
      case ScalarValueKind::kBool: {
        uint8_t decoded = 0;
        if (!reader.u8(&decoded) || decoded > 1) {
          return malformed<OutputDescriptorV1>("invalid scalar bool value");
        }
        value = decoded != 0;
        break;
      }
      case ScalarValueKind::kString: {
        uint32_t length = 0;
        Span<const uint8_t> decoded;
        if (!reader.u32(&length) || !reader.take(length, &decoded)) {
          return malformed<OutputDescriptorV1>("truncated scalar string value");
        }
        value = asStringView(decoded);
        break;
      }
      default:
        return malformed<OutputDescriptorV1>("unknown scalar value kind");
    }
    decoded_fields.push_back(DecodedField{.name_offset = name_offset, .name_length = name_length, .value = value});
  }

  ScalarOutputV1 scalar{
      .has_timestamp = has_timestamp != 0,
      .timestamp_ns = timestamp_ns,
      .fields = {},
  };
  scalar.fields.reserve(decoded_fields.size());
  for (auto& field : decoded_fields) {
    const size_t name_offset = field.name_offset;
    const size_t name_length = field.name_length;
    if (name_offset > bytes.size() || name_length > bytes.size() - name_offset) {
      return malformed<OutputDescriptorV1>("scalar field name range is outside the block");
    }
    scalar.fields.push_back(
        ScalarFieldV1{
            .name = asStringView(bytes.subspan(name_offset, name_length)),
            .value = std::move(field.value),
        });
  }
  return OutputDescriptorV1(std::move(scalar));
}

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic pop
#endif

}  // namespace PJ::parser_module
