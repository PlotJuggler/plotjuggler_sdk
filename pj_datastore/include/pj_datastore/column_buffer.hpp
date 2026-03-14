#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "pj_base/span.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/buffer.hpp"

namespace PJ {

// Import base types into engine namespace
using PJ::BitSpan;
using PJ::FieldId;
using PJ::PrimitiveType;
using PJ::Span;

// Physical storage category. Narrow integers (int8, int16) widen to int64;

// int32 is kept as a dedicated storage kind because it's extremely common.
// Narrow unsigned integers (uint8..uint32) widen to uint64.
// FOR compression recovers further byte savings at seal time.
enum class StorageKind : uint8_t {
  kFloat32,
  kFloat64,
  kInt32,
  kInt64,
  kUint64,
  kBool,
  kString,
};

[[nodiscard]] constexpr StorageKind storageKindOf(PrimitiveType t) noexcept {
  switch (t) {
    case PrimitiveType::kFloat32:
      return StorageKind::kFloat32;
    case PrimitiveType::kFloat64:
      return StorageKind::kFloat64;
    case PrimitiveType::kInt8:
    case PrimitiveType::kInt16:
      return StorageKind::kInt64;
    case PrimitiveType::kInt32:
      return StorageKind::kInt32;
    case PrimitiveType::kInt64:
      return StorageKind::kInt64;
    case PrimitiveType::kUint8:
    case PrimitiveType::kUint16:
    case PrimitiveType::kUint32:
    case PrimitiveType::kUint64:
      return StorageKind::kUint64;
    case PrimitiveType::kBool:
      return StorageKind::kBool;
    case PrimitiveType::kString:
      return StorageKind::kString;
    case PrimitiveType::kUnspecified:
      break;
  }
  return StorageKind::kFloat64;
}

// Byte size of a StorageKind's fixed-width element. Returns 0 for kString.
[[nodiscard]] constexpr std::size_t storageKindSize(StorageKind k) noexcept {
  switch (k) {
    case StorageKind::kFloat32:
      return sizeof(float);
    case StorageKind::kFloat64:
      return sizeof(double);
    case StorageKind::kInt32:
      return sizeof(int32_t);
    case StorageKind::kInt64:
      return sizeof(int64_t);
    case StorageKind::kUint64:
      return sizeof(uint64_t);
    case StorageKind::kBool:
      return sizeof(uint8_t);
    case StorageKind::kString:
      return 0;
  }
  return 0;
}

enum class EncodingType : uint8_t {
  kRaw,               // Unencoded typed storage
  kDictionary,        // Dictionary encoding (strings)
  kPackedBool,        // Packed bitfield (bools)
  kConstant,          // Single repeated value
  kFrameOfReference,  // Min-subtracted narrowed offsets
};

/// Flattened column descriptor derived from a schema leaf.
struct ColumnDescriptor {
  /// Stable field id assigned by the writer.
  FieldId field_id;
  /// Logical field type from schema.
  PrimitiveType logical_type;  // Full logical type for metadata/schema
  /// Fully-qualified field path (e.g. "pose.position.x").
  std::string field_path;  // e.g., "position.x"
};

/// In-memory typed column buffer with optional validity bitmap.
class TypedColumnBuffer {
 public:
  /// Construct a buffer for one logical column.
  explicit TypedColumnBuffer(ColumnDescriptor descriptor);

  /// Descriptor metadata used by this buffer.
  [[nodiscard]] const ColumnDescriptor& descriptor() const noexcept;

  /// Number of appended rows.
  [[nodiscard]] std::size_t rowCount() const noexcept;

  /// True if at least one row is null.
  [[nodiscard]] bool hasNulls() const noexcept;

  /// True if row is valid (non-null).
  [[nodiscard]] bool isValid(std::size_t row) const noexcept;

  // Append typed values (7 storage types)
  /// Append one float32 value.
  void appendFloat32(float value);

  /// Append one float64 value.
  void appendFloat64(double value);

  /// Append one int32 value.
  void appendInt32(int32_t value);

  /// Append one int64 value.
  void appendInt64(int64_t value);

  /// Append one uint64 value.
  void appendUint64(uint64_t value);

  /// Append one bool value (stored as uint8 0/1).
  void appendBool(bool value);

  /// Append one UTF-8 string.
  void appendString(std::string_view value);

  /// Append a null row (value slot is zero-filled).
  void appendNull();

  // Read typed values (7 storage types)
  /// Read one float32 value.
  [[nodiscard]] float readFloat32(std::size_t row) const;

  /// Read one float64 value.
  [[nodiscard]] double readFloat64(std::size_t row) const;

  /// Read one int32 value.
  [[nodiscard]] int32_t readInt32(std::size_t row) const;

  /// Read one int64 value.
  [[nodiscard]] int64_t readInt64(std::size_t row) const;

  /// Read one uint64 value.
  [[nodiscard]] uint64_t readUint64(std::size_t row) const;

  /// Read one bool value.
  [[nodiscard]] bool readBool(std::size_t row) const;

  /// Read one string view.
  [[nodiscard]] std::string_view readString(std::size_t row) const;

  /// Return true if row is null.
  [[nodiscard]] bool isNull(std::size_t row) const;

  // Read any numeric column as double (for stats, display).
  // For string columns, returns NaN.
  [[nodiscard]] double readAsDouble(std::size_t row) const;

  // ---- Bulk append (contiguous memcpy-based) ----
  /// Append contiguous float32 values.
  void appendFloat32Bulk(Span<const float> data);

  /// Append contiguous float64 values.
  void appendFloat64Bulk(Span<const double> data);

  /// Append contiguous int32 values.
  void appendInt32Bulk(Span<const int32_t> data);

  /// Append contiguous int64 values.
  void appendInt64Bulk(Span<const int64_t> data);

  /// Append contiguous uint64 values.
  void appendUint64Bulk(Span<const uint64_t> data);

  /// Append contiguous bool bytes (0/1).
  void appendBoolBulk(Span<const uint8_t> data);

  /// Append strings from Arrow-compatible offset+data layout.
  /// offsets has (count + 1) entries; data contains the concatenated strings.
  void appendStringsBulk(Span<const uint32_t> offsets, Span<const char> data);

  /// Append a validity bitmap for the most recently appended `count` rows.
  /// Arrow-compatible bit layout. bit_offset is the starting bit within bitmap.
  void appendValidityBulk(BitSpan validity);

  // Access underlying buffers (for encoding at seal time)
  /// Raw value bytes.
  [[nodiscard]] const RawBuffer& valueBuffer() const noexcept;

  /// Packed validity bitmap.
  [[nodiscard]] const BitVector& validityBuffer() const noexcept;

  /// String offsets bytes (uint32 array).
  [[nodiscard]] const RawBuffer& offsetsBuffer() const noexcept;  // strings only

 private:
  /// Column descriptor metadata.
  ColumnDescriptor descriptor_;
  /// Raw value payload.
  RawBuffer values_;
  /// Packed validity bits.
  BitVector validity_;
  /// String offset buffer.
  RawBuffer offsets_;  // For string: offset array (uint32_t per entry + 1 sentinel)
  /// Number of rows currently stored.
  std::size_t row_count_ = 0;
  /// Number of null rows.
  std::size_t null_count_ = 0;
  /// Whether validity_ has been initialized.
  bool validity_initialized_ = false;

  void ensureValidityInitialized();

  template <typename T>
  void appendFixed(T value);

  template <typename T>
  void appendFixedBulk(Span<const T> data);

  template <typename T>
  [[nodiscard]] T readFixed(std::size_t row) const;
};

}  // namespace PJ
