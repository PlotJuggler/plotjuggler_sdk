#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "pj_base/span.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"

namespace PJ {

// ---------------------------------------------------------------------------
// Field type enum — the plugin-visible type set
// ---------------------------------------------------------------------------

enum class FieldType : uint8_t {
  kFloat32,
  kFloat64,
  kInt32,
  kInt64,
  kUint64,
  kBool,
  kString
};

// ---------------------------------------------------------------------------
// Value reference — tagged union for passing scalar values at the boundary
// ---------------------------------------------------------------------------

using ValueRef = std::variant<float, double, int32_t, int64_t, uint64_t, bool, std::string_view>;

// ---------------------------------------------------------------------------
// Handles — cheap, host-managed identity tokens
// ---------------------------------------------------------------------------

struct DataSourceHandle {
  DatasetId id = 0;

  friend bool operator==(DataSourceHandle a, DataSourceHandle b) { return a.id == b.id; }
  friend bool operator!=(DataSourceHandle a, DataSourceHandle b) { return a.id != b.id; }
};

struct TopicHandle {
  TopicId id = 0;

  friend bool operator==(TopicHandle a, TopicHandle b) { return a.id == b.id; }
  friend bool operator!=(TopicHandle a, TopicHandle b) { return a.id != b.id; }
};

struct FieldHandle {
  TopicHandle topic;
  FieldId id = 0;

  friend bool operator==(FieldHandle a, FieldHandle b) { return a.topic == b.topic && a.id == b.id; }
  friend bool operator!=(FieldHandle a, FieldHandle b) { return !(a == b); }
};

// ---------------------------------------------------------------------------
// Write API value carriers
// ---------------------------------------------------------------------------

struct NamedFieldValue {
  std::string_view name;
  FieldType type;
  bool is_null = false;
  ValueRef value;
};

struct BoundFieldValue {
  FieldHandle field;
  bool is_null = false;
  ValueRef value;
};

// ---------------------------------------------------------------------------
// Read API — catalog enumeration types
// ---------------------------------------------------------------------------

struct DataSourceInfoView {
  DataSourceHandle handle;
  std::string_view name;
  uint32_t first_topic = 0;
  uint32_t topic_count = 0;
};

struct TopicInfoView {
  TopicHandle handle;
  DataSourceHandle source;
  std::string_view name;
  uint32_t first_field = 0;
  uint32_t field_count = 0;
};

struct FieldInfoView {
  FieldHandle handle;
  std::string_view name;
  FieldType type;
};

struct CatalogView {
  Span<const DataSourceInfoView> data_sources;
  Span<const TopicInfoView> topics;
  Span<const FieldInfoView> fields;
};

// ---------------------------------------------------------------------------
// Read API — materialized series types
// ---------------------------------------------------------------------------

struct BoolSeriesValues {
  std::vector<uint8_t> values;
};

struct StringSeriesValues {
  std::vector<uint32_t> offsets;
  std::vector<char> bytes;
};

struct MaterializedSeries {
  DataSourceHandle source;
  TopicHandle topic;
  FieldHandle field;
  FieldType type;

  std::vector<Timestamp> timestamps;
  std::variant<std::vector<float>,
               std::vector<double>,
               std::vector<int32_t>,
               std::vector<int64_t>,
               std::vector<uint64_t>,
               BoolSeriesValues,
               StringSeriesValues>
      values;
  std::vector<uint8_t> validity_bits;
};

// ---------------------------------------------------------------------------
// FieldType ↔ PrimitiveType conversion
// ---------------------------------------------------------------------------

constexpr PrimitiveType toPrimitiveType(FieldType ft) noexcept {
  switch (ft) {
    case FieldType::kFloat32: return PrimitiveType::kFloat32;
    case FieldType::kFloat64: return PrimitiveType::kFloat64;
    case FieldType::kInt32: return PrimitiveType::kInt32;
    case FieldType::kInt64: return PrimitiveType::kInt64;
    case FieldType::kUint64: return PrimitiveType::kUint64;
    case FieldType::kBool: return PrimitiveType::kBool;
    case FieldType::kString: return PrimitiveType::kString;
  }
  return PrimitiveType::kFloat64;  // unreachable
}

constexpr std::optional<FieldType> toFieldType(PrimitiveType pt) noexcept {
  switch (pt) {
    case PrimitiveType::kFloat32: return FieldType::kFloat32;
    case PrimitiveType::kFloat64: return FieldType::kFloat64;
    case PrimitiveType::kInt32: return FieldType::kInt32;
    case PrimitiveType::kInt64: return FieldType::kInt64;
    case PrimitiveType::kUint64: return FieldType::kUint64;
    case PrimitiveType::kBool: return FieldType::kBool;
    case PrimitiveType::kString: return FieldType::kString;
    default: return std::nullopt;
  }
}

// Maps narrow primitive types to the nearest FieldType (for catalog display).
constexpr FieldType toFieldTypeWidened(PrimitiveType pt) noexcept {
  switch (pt) {
    case PrimitiveType::kFloat32: return FieldType::kFloat32;
    case PrimitiveType::kFloat64: return FieldType::kFloat64;
    case PrimitiveType::kInt8:
    case PrimitiveType::kInt16:
    case PrimitiveType::kInt32: return FieldType::kInt32;
    case PrimitiveType::kInt64: return FieldType::kInt64;
    case PrimitiveType::kUint8:
    case PrimitiveType::kUint16:
    case PrimitiveType::kUint32:
    case PrimitiveType::kUint64: return FieldType::kUint64;
    case PrimitiveType::kBool: return FieldType::kBool;
    case PrimitiveType::kString: return FieldType::kString;
  }
  return FieldType::kFloat64;  // unreachable
}

}  // namespace PJ
