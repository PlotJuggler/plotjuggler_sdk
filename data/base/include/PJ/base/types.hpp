#pragma once
#include <cstddef>
#include <cstdint>
#include <variant>

namespace PJ {

/// Stable identifier for a dataset.
using DatasetId = uint32_t;
/// Stable identifier for a topic.
using TopicId = uint32_t;
/// Stable identifier for a flattened field/column.
using FieldId = uint32_t;
/// Stable identifier for a sealed chunk.
using ChunkId = uint64_t;
/// Stable identifier for a time domain.
using TimeDomainId = uint32_t;
/// Stable identifier for a registered schema.
using SchemaId = uint32_t;
/// Stable identifier for a plugin instance.
using PluginId = uint32_t;

/// Timestamp in nanoseconds since epoch (or domain origin).
using Timestamp = int64_t;  // nanoseconds since epoch

/// Numeric scalar kind used by scalar APIs.
enum class NumericType : uint8_t {
  kFloat32,
  kFloat64,
  kInt8,
  kInt16,
  kInt32,
  kInt64,
  kUint8,
  kUint16,
  kUint32,
  kUint64,
};

/// Runtime numeric payload used by scalar APIs.
using NumericValue =
    std::variant<float, double, int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t>;

// Size in bytes of each numeric type
[[nodiscard]] constexpr size_t numeric_type_size(NumericType type) noexcept {
  switch (type) {
    case NumericType::kFloat32:
      return sizeof(float);
    case NumericType::kFloat64:
      return sizeof(double);
    case NumericType::kInt8:
      return sizeof(int8_t);
    case NumericType::kInt16:
      return sizeof(int16_t);
    case NumericType::kInt32:
      return sizeof(int32_t);
    case NumericType::kInt64:
      return sizeof(int64_t);
    case NumericType::kUint8:
      return sizeof(uint8_t);
    case NumericType::kUint16:
      return sizeof(uint16_t);
    case NumericType::kUint32:
      return sizeof(uint32_t);
    case NumericType::kUint64:
      return sizeof(uint64_t);
  }
  return 0;  // unreachable
}

// Map a NumericValue variant index to NumericType
[[nodiscard]] constexpr NumericType numeric_value_type(const NumericValue& v) noexcept {
  return static_cast<NumericType>(v.index());
}

// Convert any NumericValue to double (for stats, display)
[[nodiscard]] constexpr double numeric_value_to_double(const NumericValue& v) noexcept {
  return std::visit([](const auto& val) -> double { return static_cast<double>(val); }, v);
}

/// Stable identifier for a derived DAG node.
using NodeId = uint32_t;

/// Sentinel value for an invalid/uninitialized chunk id.
constexpr ChunkId kInvalidChunkId = 0;

/// Sentinel value for an invalid/uninitialized node id.
constexpr NodeId kInvalidNodeId = 0;

}  // namespace PJ
