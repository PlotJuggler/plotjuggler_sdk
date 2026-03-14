#pragma once

#include "pj_base/plugin_data_api.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"

namespace PJ::sdk {

using DataSourceHandle = PJ_data_source_handle_t;
using TopicHandle = PJ_topic_handle_t;
using FieldHandle = PJ_field_handle_t;

[[nodiscard]] inline PJ_primitive_type_t toAbiType(PrimitiveType type) {
  return static_cast<PJ_primitive_type_t>(type);
}

[[nodiscard]] inline PrimitiveType fromAbiType(PJ_primitive_type_t type) {
  return static_cast<PrimitiveType>(type);
}

inline bool operator==(DataSourceHandle a, DataSourceHandle b) {
  return a.id == b.id;
}

inline bool operator==(TopicHandle a, TopicHandle b) {
  return a.id == b.id;
}

inline bool operator==(FieldHandle a, FieldHandle b) {
  return a.topic == b.topic && a.id == b.id;
}

inline bool operator!=(DataSourceHandle a, DataSourceHandle b) {
  return !(a == b);
}

inline bool operator!=(TopicHandle a, TopicHandle b) {
  return !(a == b);
}

inline bool operator!=(FieldHandle a, FieldHandle b) {
  return !(a == b);
}

/// Typed null — a null value with an explicit column type. Use this when the
/// schema defines a field's type but the value is absent (e.g., an optional
/// field in ROS/Protobuf that is not set in a particular message). Unlike
/// `kNull` (untyped), a `TypedNull` can create a new column even on first use.
struct TypedNull {
  PrimitiveType type;
};

using ValueRef = std::variant<NullValue,
                              TypedNull,
                              float,
                              double,
                              int8_t,
                              int16_t,
                              int32_t,
                              int64_t,
                              uint8_t,
                              uint16_t,
                              uint32_t,
                              uint64_t,
                              bool,
                              std::string_view>;

/// Returns true if the value is null (either untyped kNull or TypedNull).
[[nodiscard]] inline bool isNull(const ValueRef& v) {
  return std::holds_alternative<NullValue>(v) || std::holds_alternative<TypedNull>(v);
}

struct NamedFieldValue {
  std::string name;  // owned — safe from dangling string_view references
  ValueRef value;
};

struct BoundFieldValue {
  FieldHandle field;
  ValueRef value;
};

class CatalogSnapshot {
 public:
  CatalogSnapshot() = default;
  explicit CatalogSnapshot(PJ_catalog_snapshot_t raw) : raw_(raw) {}
  ~CatalogSnapshot() {
    reset();
  }

  CatalogSnapshot(const CatalogSnapshot&) = delete;
  CatalogSnapshot& operator=(const CatalogSnapshot&) = delete;

  CatalogSnapshot(CatalogSnapshot&& other) noexcept : raw_(other.release()) {}

  CatalogSnapshot& operator=(CatalogSnapshot&& other) noexcept {
    if (this != &other) {
      reset();
      raw_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] Span<const PJ_data_source_info_t> dataSources() const {
    return Span<const PJ_data_source_info_t>(raw_.data_sources, raw_.data_source_count);
  }

  [[nodiscard]] Span<const PJ_topic_info_t> topics() const {
    return Span<const PJ_topic_info_t>(raw_.topics, raw_.topic_count);
  }

  [[nodiscard]] Span<const PJ_field_info_t> fields() const {
    return Span<const PJ_field_info_t>(raw_.fields, raw_.field_count);
  }

 private:
  PJ_catalog_snapshot_t raw_{};

  [[nodiscard]] PJ_catalog_snapshot_t release() noexcept {
    auto raw = raw_;
    raw_ = {};
    return raw;
  }

  void reset() {
    if (raw_.release != nullptr) {
      raw_.release(raw_.release_ctx);
      raw_ = {};
    }
  }
};

class MaterializedSeries {
 public:
  MaterializedSeries() = default;
  explicit MaterializedSeries(PJ_materialized_series_t raw) : raw_(raw) {}
  ~MaterializedSeries() {
    reset();
  }

  MaterializedSeries(const MaterializedSeries&) = delete;
  MaterializedSeries& operator=(const MaterializedSeries&) = delete;

  MaterializedSeries(MaterializedSeries&& other) noexcept : raw_(other.release()) {}

  MaterializedSeries& operator=(MaterializedSeries&& other) noexcept {
    if (this != &other) {
      reset();
      raw_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] DataSourceHandle source() const {
    return raw_.source;
  }

  [[nodiscard]] TopicHandle topic() const {
    return raw_.topic;
  }

  [[nodiscard]] FieldHandle field() const {
    return raw_.field;
  }

  [[nodiscard]] PrimitiveType type() const {
    return fromAbiType(raw_.type);
  }

  [[nodiscard]] Span<const Timestamp> timestamps() const {
    return Span<const Timestamp>(raw_.timestamps, raw_.row_count);
  }

  [[nodiscard]] Span<const uint8_t> validityBits() const {
    return Span<const uint8_t>(raw_.validity_bits, raw_.validity_size);
  }

  [[nodiscard]] const PJ_materialized_series_t& raw() const {
    return raw_;
  }

 private:
  PJ_materialized_series_t raw_{};

  [[nodiscard]] PJ_materialized_series_t release() noexcept {
    auto raw = raw_;
    raw_ = {};
    return raw;
  }

  void reset() {
    if (raw_.release != nullptr) {
      raw_.release(raw_.release_ctx);
      raw_ = {};
    }
  }
};

[[nodiscard]] inline std::string_view toStringView(PJ_string_view_t view) {
  return std::string_view(view.data == nullptr ? "" : view.data, view.size);
}

[[nodiscard]] inline PJ_string_view_t toAbiString(std::string_view view) {
  return PJ_string_view_t{view.data(), view.size()};
}

[[nodiscard]] inline PJ_bytes_view_t toAbiBytes(Span<const uint8_t> bytes) {
  return PJ_bytes_view_t{bytes.data(), bytes.size()};
}

[[nodiscard]] inline PrimitiveType typeOf(const ValueRef& value) {
  return std::visit(
      [](auto&& v) -> PrimitiveType {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NullValue>) return PrimitiveType::kUnspecified;
        else if constexpr (std::is_same_v<T, TypedNull>) return v.type;
        else if constexpr (std::is_same_v<T, float>) return PrimitiveType::kFloat32;
        else if constexpr (std::is_same_v<T, double>) return PrimitiveType::kFloat64;
        else if constexpr (std::is_same_v<T, int8_t>) return PrimitiveType::kInt8;
        else if constexpr (std::is_same_v<T, int16_t>) return PrimitiveType::kInt16;
        else if constexpr (std::is_same_v<T, int32_t>) return PrimitiveType::kInt32;
        else if constexpr (std::is_same_v<T, int64_t>) return PrimitiveType::kInt64;
        else if constexpr (std::is_same_v<T, uint8_t>) return PrimitiveType::kUint8;
        else if constexpr (std::is_same_v<T, uint16_t>) return PrimitiveType::kUint16;
        else if constexpr (std::is_same_v<T, uint32_t>) return PrimitiveType::kUint32;
        else if constexpr (std::is_same_v<T, uint64_t>) return PrimitiveType::kUint64;
        else if constexpr (std::is_same_v<T, bool>) return PrimitiveType::kBool;
        else if constexpr (std::is_same_v<T, std::string_view>) return PrimitiveType::kString;
      },
      value);
}

[[nodiscard]] inline PJ_scalar_value_t toAbiScalar(const ValueRef& value) {
  PJ_scalar_value_t out{};
  out.type = toAbiType(typeOf(value));
  std::visit(
      [&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NullValue>) {
          // zeroed scalar — type is a safe default, value is unused
        } else if constexpr (std::is_same_v<T, TypedNull>) {
          // zeroed scalar — type is set above via typeOf(), value is unused
        } else if constexpr (std::is_same_v<T, float>) {
          out.data.as_float32 = v;
        } else if constexpr (std::is_same_v<T, double>) {
          out.data.as_float64 = v;
        } else if constexpr (std::is_same_v<T, int8_t>) {
          out.data.as_int8 = v;
        } else if constexpr (std::is_same_v<T, int16_t>) {
          out.data.as_int16 = v;
        } else if constexpr (std::is_same_v<T, int32_t>) {
          out.data.as_int32 = v;
        } else if constexpr (std::is_same_v<T, int64_t>) {
          out.data.as_int64 = v;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
          out.data.as_uint8 = v;
        } else if constexpr (std::is_same_v<T, uint16_t>) {
          out.data.as_uint16 = v;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
          out.data.as_uint32 = v;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
          out.data.as_uint64 = v;
        } else if constexpr (std::is_same_v<T, bool>) {
          out.data.as_bool = v ? 1 : 0;
        } else if constexpr (std::is_same_v<T, std::string_view>) {
          out.data.as_string = toAbiString(v);
        }
      },
      value);
  return out;
}

class SourceWriteHostView {
 public:
  explicit SourceWriteHostView(PJ_source_write_host_t host) : host_(host) {}

  /// Returns true if both context and vtable pointers are set.
  [[nodiscard]] bool valid() const { return host_.ctx != nullptr && host_.vtable != nullptr; }

  [[nodiscard]] Expected<TopicHandle> ensureTopic(std::string_view topic_name) const {
    if (!valid()) {
      return unexpected("write host is not bound");
    }
    TopicHandle handle{};
    if (!host_.vtable->ensure_topic(host_.ctx, toAbiString(topic_name), &handle)) {
      return unexpected(std::string(lastError()));
    }
    return handle;
  }

  [[nodiscard]] Expected<FieldHandle> ensureField(
      TopicHandle topic, std::string_view field_name, PrimitiveType type) const {
    if (!valid()) {
      return unexpected("write host is not bound");
    }
    FieldHandle handle{};
    if (!host_.vtable->ensure_field(host_.ctx, topic, toAbiString(field_name), toAbiType(type), &handle)) {
      return unexpected(std::string(lastError()));
    }
    return handle;
  }

  /// Append one record with named fields.
  /// Fields not included in the span are automatically filled with null.
  /// This enables sparse records — not all fields need data for every row.
  /// Pre-register all fields via ensureField() before the first appendRecord().
  [[nodiscard]] Status appendRecord(
      TopicHandle topic, Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    if (!valid()) {
      return unexpected("write host is not bound");
    }
    std::vector<PJ_named_field_value_t> raw_fields;
    raw_fields.reserve(fields.size());
    for (const auto& field : fields) {
      raw_fields.push_back(PJ_named_field_value_t{
          .name = toAbiString(field.name),
          .is_null = isNull(field.value),
          .value = toAbiScalar(field.value),
      });
    }
    if (!host_.vtable->append_record(host_.ctx, topic, timestamp, raw_fields.data(), raw_fields.size())) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    if (!valid()) {
      return unexpected("write host is not bound");
    }
    std::vector<PJ_bound_field_value_t> raw_fields;
    raw_fields.reserve(fields.size());
    for (const auto& field : fields) {
      raw_fields.push_back(PJ_bound_field_value_t{
          .field = field.field,
          .is_null = isNull(field.value),
          .value = toAbiScalar(field.value),
      });
    }
    if (!host_.vtable->append_bound_record(host_.ctx, topic, timestamp, raw_fields.data(), raw_fields.size())) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendRecord(
      TopicHandle topic, Timestamp timestamp, std::initializer_list<NamedFieldValue> fields) const {
    return appendRecord(topic, timestamp, Span<const NamedFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, std::initializer_list<BoundFieldValue> fields) const {
    return appendBoundRecord(topic, timestamp, Span<const BoundFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendArrowIpc(
      TopicHandle topic, Span<const uint8_t> ipc_stream, std::string_view timestamp_column = "_timestamp") const {
    if (!valid()) {
      return unexpected("write host is not bound");
    }
    if (!host_.vtable->append_arrow_ipc(
            host_.ctx, topic, toAbiBytes(ipc_stream), toAbiString(timestamp_column))) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] std::string_view lastError() const {
    if (!valid()) {
      return {};
    }
    const char* err = host_.vtable->get_last_error(host_.ctx);
    return err == nullptr ? std::string_view{} : std::string_view(err);
  }

 private:
  PJ_source_write_host_t host_;
};

class ParserWriteHostView {
 public:
  explicit ParserWriteHostView(PJ_parser_write_host_t host) : host_(host) {}

  [[nodiscard]] Expected<FieldHandle> ensureField(std::string_view field_name, PrimitiveType type) const {
    FieldHandle handle{};
    if (!host_.vtable->ensure_field(host_.ctx, toAbiString(field_name), toAbiType(type), &handle)) {
      return unexpected(std::string(lastError()));
    }
    return handle;
  }

  [[nodiscard]] Status appendRecord(Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    std::vector<PJ_named_field_value_t> raw_fields;
    raw_fields.reserve(fields.size());
    for (const auto& field : fields) {
      raw_fields.push_back(PJ_named_field_value_t{
          .name = toAbiString(field.name),
          .is_null = isNull(field.value),
          .value = toAbiScalar(field.value),
      });
    }
    if (!host_.vtable->append_record(host_.ctx, timestamp, raw_fields.data(), raw_fields.size())) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    std::vector<PJ_bound_field_value_t> raw_fields;
    raw_fields.reserve(fields.size());
    for (const auto& field : fields) {
      raw_fields.push_back(PJ_bound_field_value_t{
          .field = field.field,
          .is_null = isNull(field.value),
          .value = toAbiScalar(field.value),
      });
    }
    if (!host_.vtable->append_bound_record(host_.ctx, timestamp, raw_fields.data(), raw_fields.size())) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendRecord(Timestamp timestamp, std::initializer_list<NamedFieldValue> fields) const {
    return appendRecord(timestamp, Span<const NamedFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendBoundRecord(
      Timestamp timestamp, std::initializer_list<BoundFieldValue> fields) const {
    return appendBoundRecord(timestamp, Span<const BoundFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendArrowIpc(
      Span<const uint8_t> ipc_stream, std::string_view timestamp_column = "_timestamp") const {
    if (!host_.vtable->append_arrow_ipc(host_.ctx, toAbiBytes(ipc_stream), toAbiString(timestamp_column))) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] std::string_view lastError() const {
    const char* err = host_.vtable->get_last_error(host_.ctx);
    return err == nullptr ? std::string_view{} : std::string_view(err);
  }

 private:
  PJ_parser_write_host_t host_;
};

class ToolboxHostView {
 public:
  explicit ToolboxHostView(PJ_toolbox_host_t host) : host_(host) {}

  [[nodiscard]] Expected<DataSourceHandle> createDataSource(std::string_view name) const {
    DataSourceHandle handle{};
    if (!host_.vtable->create_data_source(host_.ctx, toAbiString(name), &handle)) {
      return unexpected(std::string(lastError()));
    }
    return handle;
  }

  [[nodiscard]] Expected<TopicHandle> ensureTopic(DataSourceHandle source, std::string_view topic_name) const {
    TopicHandle handle{};
    if (!host_.vtable->ensure_topic(host_.ctx, source, toAbiString(topic_name), &handle)) {
      return unexpected(std::string(lastError()));
    }
    return handle;
  }

  [[nodiscard]] Expected<FieldHandle> ensureField(
      TopicHandle topic, std::string_view field_name, PrimitiveType type) const {
    FieldHandle handle{};
    if (!host_.vtable->ensure_field(host_.ctx, topic, toAbiString(field_name), toAbiType(type), &handle)) {
      return unexpected(std::string(lastError()));
    }
    return handle;
  }

  [[nodiscard]] Status appendRecord(
      TopicHandle topic, Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    std::vector<PJ_named_field_value_t> raw_fields;
    raw_fields.reserve(fields.size());
    for (const auto& field : fields) {
      raw_fields.push_back(PJ_named_field_value_t{
          .name = toAbiString(field.name),
          .is_null = isNull(field.value),
          .value = toAbiScalar(field.value),
      });
    }
    if (!host_.vtable->append_record(host_.ctx, topic, timestamp, raw_fields.data(), raw_fields.size())) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    std::vector<PJ_bound_field_value_t> raw_fields;
    raw_fields.reserve(fields.size());
    for (const auto& field : fields) {
      raw_fields.push_back(PJ_bound_field_value_t{
          .field = field.field,
          .is_null = isNull(field.value),
          .value = toAbiScalar(field.value),
      });
    }
    if (!host_.vtable->append_bound_record(host_.ctx, topic, timestamp, raw_fields.data(), raw_fields.size())) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendRecord(
      TopicHandle topic, Timestamp timestamp, std::initializer_list<NamedFieldValue> fields) const {
    return appendRecord(topic, timestamp, Span<const NamedFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, std::initializer_list<BoundFieldValue> fields) const {
    return appendBoundRecord(topic, timestamp, Span<const BoundFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendArrowIpc(
      TopicHandle topic, Span<const uint8_t> ipc_stream, std::string_view timestamp_column = "_timestamp") const {
    if (!host_.vtable->append_arrow_ipc(
            host_.ctx, topic, toAbiBytes(ipc_stream), toAbiString(timestamp_column))) {
      return unexpected(std::string(lastError()));
    }
    return okStatus();
  }

  [[nodiscard]] Expected<CatalogSnapshot> catalogSnapshot() const {
    PJ_catalog_snapshot_t raw{};
    if (!host_.vtable->acquire_catalog_snapshot(host_.ctx, &raw)) {
      return unexpected(std::string(lastError()));
    }
    return CatalogSnapshot(raw);
  }

  [[nodiscard]] Expected<MaterializedSeries> readSeries(FieldHandle field) const {
    PJ_materialized_series_t raw{};
    if (!host_.vtable->read_series(host_.ctx, field, &raw)) {
      return unexpected(std::string(lastError()));
    }
    return MaterializedSeries(raw);
  }

  [[nodiscard]] std::string_view lastError() const {
    const char* err = host_.vtable->get_last_error(host_.ctx);
    return err == nullptr ? std::string_view{} : std::string_view(err);
  }

 private:
  PJ_toolbox_host_t host_;
};

}  // namespace PJ::sdk
