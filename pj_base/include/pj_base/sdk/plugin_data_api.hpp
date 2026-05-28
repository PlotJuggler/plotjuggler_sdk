#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "pj_base/builtin/builtin_object.hpp"
#include "pj_base/expected.hpp"
#include "pj_base/number_parse.hpp"
#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/arrow.hpp"
#include "pj_base/sdk/object_bytes.hpp"
#include "pj_base/span.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"

namespace PJ::sdk {

using DataSourceHandle = PJ_data_source_handle_t;
using TopicHandle = PJ_topic_handle_t;
using FieldHandle = PJ_field_handle_t;
using ObjectTopicHandle = PJ_object_topic_handle_t;

inline bool operator==(ObjectTopicHandle a, ObjectTopicHandle b) {
  return a.id == b.id;
}

inline bool operator!=(ObjectTopicHandle a, ObjectTopicHandle b) {
  return !(a == b);
}

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

using ValueRef = std::variant<
    NullValue, TypedNull, float, double, int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t, bool,
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

/// One row of scalar data with an optional parser-controlled timestamp.
/// When `ts` is nullopt the host uses the message's own timestamp
/// (the value passed to parse_scalars). Set `ts` to override — e.g. to
/// use a timestamp field embedded inside the payload rather than the
/// transport-level receive time.
struct ScalarRecord {
  std::optional<Timestamp> ts;
  std::vector<NamedFieldValue> fields;
};

/// A builtin object (image, point cloud, …) with an optional parser-controlled
/// timestamp. Mirrors ScalarRecord for the object route: when `ts` is nullopt
/// the host uses the message's own timestamp; set it to use the sensor time
/// embedded in the payload (e.g. ROS Header.stamp).
struct ObjectRecord {
  std::optional<Timestamp> ts;
  BuiltinObject object;
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
        if constexpr (std::is_same_v<T, NullValue>) {
          return PrimitiveType::kUnspecified;
        } else if constexpr (std::is_same_v<T, TypedNull>) {
          return v.type;
        } else if constexpr (std::is_same_v<T, float>) {
          return PrimitiveType::kFloat32;
        } else if constexpr (std::is_same_v<T, double>) {
          return PrimitiveType::kFloat64;
        } else if constexpr (std::is_same_v<T, int8_t>) {
          return PrimitiveType::kInt8;
        } else if constexpr (std::is_same_v<T, int16_t>) {
          return PrimitiveType::kInt16;
        } else if constexpr (std::is_same_v<T, int32_t>) {
          return PrimitiveType::kInt32;
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return PrimitiveType::kInt64;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
          return PrimitiveType::kUint8;
        } else if constexpr (std::is_same_v<T, uint16_t>) {
          return PrimitiveType::kUint16;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
          return PrimitiveType::kUint32;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
          return PrimitiveType::kUint64;
        } else if constexpr (std::is_same_v<T, bool>) {
          return PrimitiveType::kBool;
        } else if constexpr (std::is_same_v<T, std::string_view>) {
          return PrimitiveType::kString;
        }
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

// ---------------------------------------------------------------------------
// Write-host views (protocol v4)
//
// Three distinct typed views, one per plugin family, each wrapping its own
// ABI fat pointer. The host-side impl may share one backend across all
// three services — but at the ABI layer the types are distinct so the
// compiler enforces scope.
//
// Arrow C Data Interface is the canonical bulk path (appendArrowStream).
// Per-record helpers remain for streaming producers and simple plugins.
// ---------------------------------------------------------------------------

// --- PJ_error_t helpers ------------------------------------------------------

/// Copy a string_view into a fixed-size null-terminated char buffer, truncating.
inline void setErrorField(char* dest, std::size_t dest_size, std::string_view src) {
  if (dest == nullptr || dest_size == 0) {
    return;
  }
  std::size_t n = src.size() < dest_size - 1 ? src.size() : dest_size - 1;
  std::memcpy(dest, src.data(), n);
  dest[n] = '\0';
}

/// Populate a PJ_error_t with code + domain + message. Safe on NULL pointer.
/// Clears the `extended` escape-hatch slots to prevent stale-pointer reuse.
inline void fillError(PJ_error_t* err, int32_t code, std::string_view domain, std::string_view message) {
  if (err == nullptr) {
    return;
  }
  err->code = code;
  setErrorField(err->domain, sizeof(err->domain), domain);
  setErrorField(err->message, sizeof(err->message), message);
  err->extended = nullptr;
  err->extended_kind[0] = '\0';
}

/// Attach a typed payload to an already-populated error. @p kind is a
/// reverse-DNS ID ("pj.error.cause.v1" etc); @p payload is valid for the
/// lifetime of the current ABI call window. Safe on NULL.
inline void setExtended(PJ_error_t* err, std::string_view kind, const void* payload) {
  if (err == nullptr) {
    return;
  }
  err->extended = payload;
  setErrorField(err->extended_kind, sizeof(err->extended_kind), kind);
}

/// Returns true if the error carries a typed extended payload.
[[nodiscard]] inline bool hasExtended(const PJ_error_t& err) {
  return err.extended_kind[0] != '\0' && err.extended != nullptr;
}

/// Convert a PJ_error_t into a human-readable string. Safe on zero-initialized.
[[nodiscard]] inline std::string errorToString(const PJ_error_t& err) {
  std::string out;
  if (err.domain[0] != '\0') {
    out.append(err.domain);
    out.append(": ");
  }
  if (err.message[0] != '\0') {
    out.append(err.message);
  }
  if (out.empty()) {
    out = "unspecified error";
  }
  return out;
}

/// Builds a PJ_named_field_value_t span from a C++ NamedFieldValue span.
[[nodiscard]] inline std::vector<PJ_named_field_value_t> toAbiNamed(Span<const NamedFieldValue> fields) {
  std::vector<PJ_named_field_value_t> raw;
  raw.reserve(fields.size());
  for (const auto& field : fields) {
    raw.push_back(
        PJ_named_field_value_t{
            .name = toAbiString(field.name),
            .is_null = isNull(field.value),
            .value = toAbiScalar(field.value),
        });
  }
  return raw;
}

[[nodiscard]] inline std::vector<PJ_bound_field_value_t> toAbiBound(Span<const BoundFieldValue> fields) {
  std::vector<PJ_bound_field_value_t> raw;
  raw.reserve(fields.size());
  for (const auto& field : fields) {
    raw.push_back(
        PJ_bound_field_value_t{
            .field = field.field,
            .is_null = isNull(field.value),
            .value = toAbiScalar(field.value),
        });
  }
  return raw;
}

/// View over PJ_source_write_host_t. Exposes multi-topic writes rooted on
/// a single data source.
class SourceWriteHostView {
 public:
  SourceWriteHostView() = default;
  explicit SourceWriteHostView(PJ_source_write_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  [[nodiscard]] Expected<TopicHandle> ensureTopic(std::string_view topic_name) const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    TopicHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_topic(host_.ctx, toAbiString(topic_name), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Expected<FieldHandle> ensureField(
      TopicHandle topic, std::string_view field_name, PrimitiveType type) const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    FieldHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_field(host_.ctx, topic, toAbiString(field_name), toAbiType(type), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Status appendRecord(TopicHandle topic, Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    auto raw = toAbiNamed(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_record(host_.ctx, topic, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    auto raw = toAbiBound(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_bound_record(host_.ctx, topic, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
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

  /// Hand an Arrow C Data Interface stream to the host for bulk ingest.
  ///
  /// Bulk-write via Arrow C Data Interface. Recommended overload — takes
  /// an `ArrowStreamHolder` by rvalue reference and disarms it on success,
  /// making the ownership-transfer dance impossible to forget.
  ///
  ///   PJ::sdk::ArrowStreamHolder stream(buildStream());
  ///   auto status = writeHost().appendArrowStream(topic, std::move(stream), "timestamp");
  ///   // stream is inert on success, still alive on failure — either way,
  ///   // no manual release() call is needed.
  ///
  /// @param timestamp_column Name of the int64 column in the stream's schema
  ///        whose values are nanoseconds since Unix epoch. Empty means use
  ///        a synthetic monotonic timestamp.
  [[nodiscard]] Status appendArrowStream(
      TopicHandle topic, ArrowStreamHolder&& stream, std::string_view timestamp_column = "timestamp") const {
    auto status = appendArrowStream(topic, stream.get(), timestamp_column);
    if (status) {
      (void)stream.release();  // host took ownership; disarm the holder.
    }
    return status;
  }

  /// Raw-pointer overload — ABI escape hatch. Prefer the rvalue-ref version
  /// above, which manages ownership for you.
  ///
  /// Ownership: on success, the host takes ownership of @p stream — it pulls
  /// all batches via get_next and calls stream->release before returning.
  /// The plugin must NOT call release itself after a successful call.
  /// On failure (returns error), ownership is NOT transferred — the plugin
  /// retains responsibility for calling stream->release itself.
  [[nodiscard]] Status appendArrowStream(
      TopicHandle topic, struct ArrowArrayStream* stream, std::string_view timestamp_column = "timestamp") const {
    if (!valid()) {
      return unexpected("source write host is not bound");
    }
    if (!PJ_HAS_TAIL_SLOT(PJ_source_write_host_vtable_t, host_.vtable, append_arrow_stream)) {
      return unexpected("source write host does not support append_arrow_stream");
    }
    PJ_error_t err{};
    if (!host_.vtable->append_arrow_stream(host_.ctx, topic, stream, toAbiString(timestamp_column), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] const PJ_source_write_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_source_write_host_t host_{};
};

/// View over PJ_parser_write_host_t. Single-topic: the topic is bound at
/// service-creation time by the host; the plugin never names it.
class ParserWriteHostView {
 public:
  ParserWriteHostView() = default;
  explicit ParserWriteHostView(PJ_parser_write_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  [[nodiscard]] Expected<FieldHandle> ensureField(std::string_view field_name, PrimitiveType type) const {
    if (!valid()) {
      return unexpected("parser write host is not bound");
    }
    FieldHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_field(host_.ctx, toAbiString(field_name), toAbiType(type), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Status appendRecord(Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    if (!valid()) {
      return unexpected("parser write host is not bound");
    }
    auto raw = toAbiNamed(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_record(host_.ctx, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    if (!valid()) {
      return unexpected("parser write host is not bound");
    }
    auto raw = toAbiBound(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_bound_record(host_.ctx, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendRecord(Timestamp timestamp, std::initializer_list<NamedFieldValue> fields) const {
    return appendRecord(timestamp, Span<const NamedFieldValue>(fields.begin(), fields.size()));
  }

  [[nodiscard]] Status appendBoundRecord(Timestamp timestamp, std::initializer_list<BoundFieldValue> fields) const {
    return appendBoundRecord(timestamp, Span<const BoundFieldValue>(fields.begin(), fields.size()));
  }

  /// Bulk-write via Arrow C Data Interface into the parser's bound topic.
  /// Recommended overload — takes an `ArrowStreamHolder` by rvalue reference
  /// and disarms it on success. Same ownership rule as
  /// `SourceWriteHostView::appendArrowStream`.
  [[nodiscard]] Status appendArrowStream(
      ArrowStreamHolder&& stream, std::string_view timestamp_column = "timestamp") const {
    auto status = appendArrowStream(stream.get(), timestamp_column);
    if (status) {
      (void)stream.release();
    }
    return status;
  }

  /// Raw-pointer overload — ABI escape hatch. Prefer the rvalue-ref version
  /// above. Ownership contract matches SourceWriteHostView::appendArrowStream.
  [[nodiscard]] Status appendArrowStream(
      struct ArrowArrayStream* stream, std::string_view timestamp_column = "timestamp") const {
    if (!valid()) {
      return unexpected("parser write host is not bound");
    }
    if (!PJ_HAS_TAIL_SLOT(PJ_parser_write_host_vtable_t, host_.vtable, append_arrow_stream)) {
      return unexpected("parser write host does not support append_arrow_stream");
    }
    PJ_error_t err{};
    if (!host_.vtable->append_arrow_stream(host_.ctx, stream, toAbiString(timestamp_column), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] const PJ_parser_write_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_parser_write_host_t host_{};
};

// ---------------------------------------------------------------------------
// Object read host view (protocol v4)
//
// Read-only access to `pj_datastore::ObjectStore`. Exposes lookup / list /
// latestAt with owning `ObjectBytes` handles. Transformer-style toolbox
// plugins that consume bytes (e.g. object detection on image topics) use
// this view; plugins that only publish scalars ignore it.
// ---------------------------------------------------------------------------

class ToolboxObjectReadHostView {
 public:
  ToolboxObjectReadHostView() = default;
  explicit ToolboxObjectReadHostView(PJ_object_read_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  /// Look up a topic by name. Returns nullopt on miss.
  [[nodiscard]] std::optional<ObjectTopicHandle> lookupTopic(std::string_view name) const {
    if (!valid() || host_.vtable->lookup_topic == nullptr) {
      return std::nullopt;
    }
    ObjectTopicHandle h = host_.vtable->lookup_topic(host_.ctx, toAbiString(name));
    if (h.id == 0) {
      return std::nullopt;
    }
    return h;
  }

  /// Enumerate all object topics visible to this host.
  [[nodiscard]] Expected<std::vector<ObjectTopicHandle>> listTopics() const {
    if (!valid() || host_.vtable->list_topics == nullptr) {
      return unexpected("toolbox object read host is not bound");
    }
    // First pass: ask for the count.
    std::size_t count = 0;
    PJ_error_t err{};
    if (!host_.vtable->list_topics(host_.ctx, nullptr, 0, &count, &err)) {
      return unexpected(errorToString(err));
    }
    std::vector<ObjectTopicHandle> out(count);
    if (count == 0) {
      return out;
    }
    if (!host_.vtable->list_topics(host_.ctx, out.data(), out.size(), &count, &err)) {
      return unexpected(errorToString(err));
    }
    out.resize(count);
    return out;
  }

  /// Return topic metadata — empty string on bad handle.
  [[nodiscard]] std::string_view topicMetadata(ObjectTopicHandle topic) const {
    if (!valid() || host_.vtable->topic_metadata == nullptr) {
      return {};
    }
    const char* meta = host_.vtable->topic_metadata(host_.ctx, topic);
    return meta != nullptr ? std::string_view(meta) : std::string_view{};
  }

  /// Fetch the entry at-or-before `timestamp`. Returns an owning
  /// `ObjectBytes`; consumer may hold it across decoder-worker threads.
  ///
  /// `out_timestamp` (optional) receives the entry's actual timestamp.
  [[nodiscard]] Expected<ObjectBytes> readLatestAt(
      ObjectTopicHandle topic, Timestamp ts, Timestamp* out_timestamp = nullptr) const {
    if (!valid() || host_.vtable->read_latest_at == nullptr) {
      return unexpected("toolbox object read host is not bound");
    }
    PJ_object_bytes_handle_t handle = nullptr;
    int64_t actual_ts = 0;
    PJ_error_t err{};
    if (!host_.vtable->read_latest_at(host_.ctx, topic, ts, &handle, &actual_ts, &err)) {
      return unexpected(errorToString(err));
    }
    if (out_timestamp != nullptr) {
      *out_timestamp = actual_ts;
    }
    return ObjectBytes(handle, host_.vtable);
  }

  [[nodiscard]] std::size_t entryCount(ObjectTopicHandle topic) const {
    if (!valid() || host_.vtable->entry_count == nullptr) {
      return 0;
    }
    return host_.vtable->entry_count(host_.ctx, topic);
  }

  /// Returns {min_ts, max_ts}. Both zero when the topic is empty/unknown.
  [[nodiscard]] std::pair<Timestamp, Timestamp> timeRange(ObjectTopicHandle topic) const {
    if (!valid() || host_.vtable->time_range == nullptr) {
      return {0, 0};
    }
    int64_t lo = 0;
    int64_t hi = 0;
    if (!host_.vtable->time_range(host_.ctx, topic, &lo, &hi)) {
      return {0, 0};
    }
    return {lo, hi};
  }

  [[nodiscard]] const PJ_object_read_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_object_read_host_t host_{};
};

// ---------------------------------------------------------------------------
// Object write host view (protocol v4)
//
// Writes into `pj_datastore::ObjectStore` — timestamped opaque payloads
// for media topics (markers, annotations, images, point clouds).
//
// Two storage shapes via the same view:
//
//   * pushOwned(handle, ts, bytes) — eager: the store copies the bytes and
//     owns them. Appropriate for small structured messages whose aggregate
//     volume fits comfortably in memory.
//
//   * pushLazy(handle, ts, fetch_closure) — lazy: the store keeps only the
//     closure, invoking it on demand when a consumer asks for the entry.
//     Appropriate for large blobs (images, point clouds) whose bytes live
//     in a file the plugin captures by shared_ptr inside the closure.
//
// The `pushLazy` template overload hides the raw C callback/destroy dance
// behind a plain C++ lambda — the SDK heap-allocates a move-capture box
// and wires the destroy callback to delete it.
// ---------------------------------------------------------------------------

class SourceObjectWriteHostView {
 public:
  using FetchFn = std::function<std::vector<uint8_t>()>;

  SourceObjectWriteHostView() = default;
  explicit SourceObjectWriteHostView(PJ_object_write_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  /// Register an object topic with opaque metadata JSON. The JSON is retained
  /// verbatim by the store; viewers and parsers use it to pick a renderer.
  [[nodiscard]] Expected<ObjectTopicHandle> registerTopic(std::string_view name, std::string_view metadata_json) const {
    if (!valid()) {
      return unexpected("source object write host is not bound");
    }
    ObjectTopicHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->register_topic(host_.ctx, toAbiString(name), toAbiString(metadata_json), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  /// Eager push — host copies the bytes into its own storage.
  [[nodiscard]] Status pushOwned(ObjectTopicHandle topic, Timestamp ts, Span<const uint8_t> payload) const {
    if (!valid()) {
      return unexpected("source object write host is not bound");
    }
    PJ_error_t err{};
    if (!host_.vtable->push_owned(host_.ctx, topic, ts, payload.data(), payload.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// Lazy push — store retains the closure; it runs on demand per read.
  ///
  /// `fetch` may capture heavy state by value (e.g. a
  /// `shared_ptr<FileReader>`). The SDK heap-allocates a move-capture box
  /// and registers a destroy callback that `delete`s the box exactly once
  /// when the ObjectStore evicts the entry. Plugin authors never touch
  /// the raw fetch_ctx / fetch_ctx_destroy dance.
  template <class Fetch>
  [[nodiscard]] Status pushLazy(ObjectTopicHandle topic, Timestamp ts, Fetch&& fetch) const {
    if (!valid()) {
      return unexpected("source object write host is not bound");
    }
    auto* box = new LazyBox{FetchFn(std::forward<Fetch>(fetch)), std::vector<uint8_t>{}};
    PJ_error_t err{};
    if (!host_.vtable->push_lazy(host_.ctx, topic, ts, &LazyBox::trampoline, box, &LazyBox::destroy, &err)) {
      delete box;  // push failed — store never took ownership; drop the box.
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// Configure retention. Application-level concern — plugins rarely call this.
  void setRetentionBudget(ObjectTopicHandle topic, int64_t time_window_ns, size_t max_memory_bytes) const {
    if (!valid()) {
      return;
    }
    host_.vtable->set_retention_budget(host_.ctx, topic, time_window_ns, max_memory_bytes);
  }

  [[nodiscard]] const PJ_object_write_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_object_write_host_t host_{};

  /// Heap-allocated box that bridges a C++ fetch lambda to the C ABI
  /// `(fetch_fn, fetch_ctx, destroy_fn)` triple. The `last_bytes` cache
  /// keeps the buffer alive across the window the host needs to copy
  /// from it; see the lifetime note on `PJ_lazy_fetch_fn_t`.
  struct LazyBox {
    FetchFn fetch;
    std::vector<uint8_t> last_bytes;

    static bool trampoline(void* ctx, const uint8_t** out_data, size_t* out_size) noexcept {
      if (ctx == nullptr || out_data == nullptr || out_size == nullptr) {
        return false;
      }
      auto* self = static_cast<LazyBox*>(ctx);
      try {
        self->last_bytes = self->fetch();
      } catch (...) {
        return false;
      }
      *out_data = self->last_bytes.data();
      *out_size = self->last_bytes.size();
      return true;
    }

    static void destroy(void* ctx) noexcept {
      delete static_cast<LazyBox*>(ctx);
    }
  };
};

// ---------------------------------------------------------------------------
// Parser object write host view (protocol v4)
//
// Topic bound by the host at service-creation time; the parser never names
// topics (mirrors the scalar ParserWriteHostView contract). Media-capable
// parsers resolve this alongside the scalar ParserWriteHost, writing header
// scalars to one and the media payload to the other from a single parse()
// call.
// ---------------------------------------------------------------------------

class ParserObjectWriteHostView {
 public:
  using FetchFn = std::function<std::vector<uint8_t>()>;

  ParserObjectWriteHostView() = default;
  explicit ParserObjectWriteHostView(PJ_parser_object_write_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  [[nodiscard]] Status pushOwned(Timestamp ts, Span<const uint8_t> payload) const {
    if (!valid()) {
      return unexpected("parser object write host is not bound");
    }
    PJ_error_t err{};
    if (!host_.vtable->push_owned(host_.ctx, ts, payload.data(), payload.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// Lazy push (uncommon for parsers; SDK hides the closure ABI dance). See
  /// SourceObjectWriteHostView::pushLazy for the ownership contract.
  template <class Fetch>
  [[nodiscard]] Status pushLazy(Timestamp ts, Fetch&& fetch) const {
    if (!valid()) {
      return unexpected("parser object write host is not bound");
    }
    auto* box = new LazyBox{FetchFn(std::forward<Fetch>(fetch)), std::vector<uint8_t>{}};
    PJ_error_t err{};
    if (!host_.vtable->push_lazy(host_.ctx, ts, &LazyBox::trampoline, box, &LazyBox::destroy, &err)) {
      delete box;
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] const PJ_parser_object_write_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_parser_object_write_host_t host_{};

  struct LazyBox {
    FetchFn fetch;
    std::vector<uint8_t> last_bytes;

    static bool trampoline(void* ctx, const uint8_t** out_data, size_t* out_size) noexcept {
      if (ctx == nullptr || out_data == nullptr || out_size == nullptr) {
        return false;
      }
      auto* self = static_cast<LazyBox*>(ctx);
      try {
        self->last_bytes = self->fetch();
      } catch (...) {
        return false;
      }
      *out_data = self->last_bytes.data();
      *out_size = self->last_bytes.size();
      return true;
    }

    static void destroy(void* ctx) noexcept {
      delete static_cast<LazyBox*>(ctx);
    }
  };
};

namespace detail {
inline PrimitiveType formatToPrimitiveType(const char* fmt) noexcept {
  if (fmt == nullptr || fmt[0] == '\0') {
    return PrimitiveType::kUnspecified;
  }
  // Arrow format string grammar — single-char codes cover the primitive set.
  switch (fmt[0]) {
    case 'b':
      return PrimitiveType::kBool;
    case 'c':
      return PrimitiveType::kInt8;
    case 'C':
      return PrimitiveType::kUint8;
    case 's':
      return PrimitiveType::kInt16;
    case 'S':
      return PrimitiveType::kUint16;
    case 'i':
      return PrimitiveType::kInt32;
    case 'I':
      return PrimitiveType::kUint32;
    case 'l':
      return PrimitiveType::kInt64;
    case 'L':
      return PrimitiveType::kUint64;
    case 'f':
      return PrimitiveType::kFloat32;
    case 'g':
      return PrimitiveType::kFloat64;
    case 'u':
    case 'U':
    case 'z':
    case 'Z':
      return PrimitiveType::kString;
    default:
      return PrimitiveType::kUnspecified;
  }
}
}  // namespace detail

/// Typed view over the two-column Arrow struct returned by
/// `ToolboxHostView::readSeriesArrow`.
///
/// Owns the `ArrowSchema` + `ArrowArray` (move-only — destructor calls
/// `release` on both) and exposes `rowCount()`, `type()`, `timestamps()`, and
/// typed `valuesAs*()` pointers directly into the Arrow buffers. This lets
/// toolbox plugins keep a familiar "materialised series" API without
/// reimplementing the Arrow-format walk every time.
///
/// Column layout: children[0] = int64 timestamp (ns epoch),
/// children[1] = typed field value. Validity bitmap is per Arrow spec.
class MaterializedSeriesView {
 public:
  MaterializedSeriesView() = default;
  MaterializedSeriesView(ArrowSchemaHolder schema, ArrowArrayHolder array) noexcept
      : schema_(std::move(schema)), array_(std::move(array)) {}

  MaterializedSeriesView(MaterializedSeriesView&&) noexcept = default;
  MaterializedSeriesView& operator=(MaterializedSeriesView&&) noexcept = default;

  [[nodiscard]] bool valid() const noexcept {
    return schema_.valid() && array_.valid() && schema_.get()->n_children >= 2 && array_.get()->n_children >= 2;
  }

  /// Number of samples.
  [[nodiscard]] size_t rowCount() const noexcept {
    return array_.valid() ? static_cast<size_t>(array_.get()->length) : 0;
  }

  /// Primitive type of the value column.
  [[nodiscard]] PrimitiveType type() const noexcept {
    if (!valid()) {
      return PrimitiveType::kUnspecified;
    }
    return detail::formatToPrimitiveType(schema_.get()->children[1]->format);
  }

  /// Int64 nanoseconds-since-epoch timestamps. Span aliases the Arrow
  /// buffer; valid until the holder is moved-from or destroyed.
  [[nodiscard]] Span<const int64_t> timestamps() const noexcept {
    if (!valid()) {
      return {};
    }
    const auto* ts = array_.get()->children[0];
    if (ts == nullptr || ts->n_buffers < 2) {
      return {};
    }
    const auto* ptr = static_cast<const int64_t*>(ts->buffers[1]);
    return {ptr, static_cast<size_t>(ts->length)};
  }

  /// Typed value-column pointer. Returns nullptr if the actual column
  /// type doesn't match the requested one.
#define PJ_SDK_VALUES_AS(CppT, PjT, SuffixMethod)                     \
  [[nodiscard]] const CppT* valuesAs##SuffixMethod() const noexcept { \
    if (type() != PrimitiveType::PjT)                                 \
      return nullptr;                                                 \
    const auto* col = array_.get()->children[1];                      \
    if (col == nullptr || col->n_buffers < 2)                         \
      return nullptr;                                                 \
    return static_cast<const CppT*>(col->buffers[1]);                 \
  }

  PJ_SDK_VALUES_AS(double, kFloat64, Float64)
  PJ_SDK_VALUES_AS(float, kFloat32, Float32)
  PJ_SDK_VALUES_AS(int8_t, kInt8, Int8)
  PJ_SDK_VALUES_AS(int16_t, kInt16, Int16)
  PJ_SDK_VALUES_AS(int32_t, kInt32, Int32)
  PJ_SDK_VALUES_AS(int64_t, kInt64, Int64)
  PJ_SDK_VALUES_AS(uint8_t, kUint8, Uint8)
  PJ_SDK_VALUES_AS(uint16_t, kUint16, Uint16)
  PJ_SDK_VALUES_AS(uint32_t, kUint32, Uint32)
  PJ_SDK_VALUES_AS(uint64_t, kUint64, Uint64)

#undef PJ_SDK_VALUES_AS

 private:
  ArrowSchemaHolder schema_;
  ArrowArrayHolder array_;
};

/// View over PJ_toolbox_host_t. Multi-source read+write + catalog.
class ToolboxHostView {
 public:
  ToolboxHostView() = default;
  explicit ToolboxHostView(PJ_toolbox_host_t host) : host_(host) {}

  [[nodiscard]] bool valid() const {
    return host_.ctx != nullptr && host_.vtable != nullptr;
  }

  [[nodiscard]] Expected<DataSourceHandle> createDataSource(std::string_view name) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    DataSourceHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->create_data_source(host_.ctx, toAbiString(name), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Expected<TopicHandle> ensureTopic(DataSourceHandle source, std::string_view topic_name) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    TopicHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_topic(host_.ctx, source, toAbiString(topic_name), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Expected<FieldHandle> ensureField(
      TopicHandle topic, std::string_view field_name, PrimitiveType type) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    FieldHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->ensure_field(host_.ctx, topic, toAbiString(field_name), toAbiType(type), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  [[nodiscard]] Status appendRecord(TopicHandle topic, Timestamp timestamp, Span<const NamedFieldValue> fields) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    auto raw = toAbiNamed(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_record(host_.ctx, topic, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Status appendBoundRecord(
      TopicHandle topic, Timestamp timestamp, Span<const BoundFieldValue> fields) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    auto raw = toAbiBound(fields);
    PJ_error_t err{};
    if (!host_.vtable->append_bound_record(host_.ctx, topic, timestamp, raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
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

  /// Bulk-write via Arrow C Data Interface. Recommended overload — takes
  /// an `ArrowStreamHolder` by rvalue reference and disarms it on success.
  /// Same ownership rule as `SourceWriteHostView::appendArrowStream`.
  [[nodiscard]] Status appendArrowStream(
      TopicHandle topic, ArrowStreamHolder&& stream, std::string_view timestamp_column = "timestamp") const {
    auto status = appendArrowStream(topic, stream.get(), timestamp_column);
    if (status) {
      (void)stream.release();
    }
    return status;
  }

  /// Raw-pointer overload — ABI escape hatch. Prefer the rvalue-ref version
  /// above. Ownership contract matches SourceWriteHostView::appendArrowStream.
  [[nodiscard]] Status appendArrowStream(
      TopicHandle topic, struct ArrowArrayStream* stream, std::string_view timestamp_column = "timestamp") const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    if (!PJ_HAS_TAIL_SLOT(PJ_toolbox_host_vtable_t, host_.vtable, append_arrow_stream)) {
      return unexpected("toolbox host does not support append_arrow_stream");
    }
    PJ_error_t err{};
    if (!host_.vtable->append_arrow_stream(host_.ctx, topic, stream, toAbiString(timestamp_column), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] Expected<CatalogSnapshot> catalogSnapshot() const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    if (!PJ_HAS_TAIL_SLOT(PJ_toolbox_host_vtable_t, host_.vtable, acquire_catalog_snapshot)) {
      return unexpected("toolbox host does not support acquire_catalog_snapshot");
    }
    PJ_catalog_snapshot_t raw{};
    PJ_error_t err{};
    if (!host_.vtable->acquire_catalog_snapshot(host_.ctx, &raw, &err)) {
      return unexpected(errorToString(err));
    }
    return CatalogSnapshot(raw);
  }

  /// Read one field's time series into host-owned Arrow structs.
  ///
  /// The caller passes in zero-initialised @p out_schema and @p out_array;
  /// the host populates them (allocates buffers, sets release callbacks).
  /// On success the caller MUST invoke out_schema->release and
  /// out_array->release when done. The array has two columns:
  /// ["timestamp" (int64), <field_name> (typed)].
  [[nodiscard]] Status readSeriesArrow(
      FieldHandle field, struct ArrowSchema* out_schema, struct ArrowArray* out_array) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    if (out_schema == nullptr || out_array == nullptr) {
      return unexpected("readSeriesArrow: out_schema and out_array must not be null");
    }
    if (!PJ_HAS_TAIL_SLOT(PJ_toolbox_host_vtable_t, host_.vtable, read_series_arrow)) {
      return unexpected("toolbox host does not support read_series_arrow");
    }
    PJ_error_t err{};
    if (!host_.vtable->read_series_arrow(host_.ctx, field, out_schema, out_array, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// Convenience wrapper over `readSeriesArrow`. Returns a
  /// `MaterializedSeriesView` that owns the `ArrowSchema` + `ArrowArray`
  /// pair and exposes typed `rowCount()`, `timestamps()`, and
  /// `valuesAs*()` accessors directly into the Arrow buffers.
  ///
  /// The returned view is move-only; its destructor calls `release` on
  /// both Arrow structs.
  [[nodiscard]] Expected<MaterializedSeriesView> readSeries(FieldHandle field) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    ArrowSchemaHolder schema;
    ArrowArrayHolder array;
    auto status = readSeriesArrow(field, schema.out(), array.out());
    if (!status) {
      return unexpected(status.error());
    }
    return MaterializedSeriesView(std::move(schema), std::move(array));
  }

  /// Register an object topic for media payloads (images, point clouds,
  /// annotations) under a previously created data source. `metadata_json`
  /// is opaque to the store and retained verbatim; viewers and parsers
  /// read it to pick a renderer.
  ///
  /// Returns `unexpected` if the host predates this ABI slot — older hosts
  /// can be detected via the ABI's `PJ_HAS_TAIL_SLOT` macro; in this
  /// view's terms, the function pointer will be null.
  [[nodiscard]] Expected<ObjectTopicHandle> registerObjectTopic(
      DataSourceHandle source, std::string_view name, std::string_view metadata_json) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    if (!hasTailSlot(offsetof(PJ_toolbox_host_vtable_t, register_object_topic), host_.vtable->register_object_topic)) {
      return unexpected("toolbox host does not support object topics (older host)");
    }
    ObjectTopicHandle handle{};
    PJ_error_t err{};
    if (!host_.vtable->register_object_topic(
            host_.ctx, source, toAbiString(name), toAbiString(metadata_json), &handle, &err)) {
      return unexpected(errorToString(err));
    }
    return handle;
  }

  /// Eager push of an object payload — host copies the bytes into its own
  /// storage. Returns `unexpected` on older hosts that don't expose the
  /// slot (see registerObjectTopic).
  [[nodiscard]] Status pushOwnedObject(ObjectTopicHandle topic, Timestamp ts, Span<const uint8_t> payload) const {
    if (!valid()) {
      return unexpected("toolbox host is not bound");
    }
    if (!hasTailSlot(offsetof(PJ_toolbox_host_vtable_t, push_owned_object), host_.vtable->push_owned_object)) {
      return unexpected("toolbox host does not support object payloads (older host)");
    }
    PJ_error_t err{};
    if (!host_.vtable->push_owned_object(host_.ctx, topic, ts, payload.data(), payload.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  [[nodiscard]] const PJ_toolbox_host_t& raw() const noexcept {
    return host_;
  }

 private:
  PJ_toolbox_host_t host_{};

  /// Tail-slot guard mirroring PJ_HAS_TAIL_SLOT from the C ABI: the host's
  /// struct_size must be large enough to cover the field, AND the slot
  /// must be non-null. Templated on the function-pointer type so the
  /// sizeof check stays accurate without naming the typedef.
  template <class Fn>
  [[nodiscard]] bool hasTailSlot(std::size_t field_offset, Fn fn) const noexcept {
    return host_.vtable != nullptr && host_.vtable->struct_size >= field_offset + sizeof(Fn) && fn != nullptr;
  }
};

// ---------------------------------------------------------------------------
// ColorMapRegistryView — typed C++ view over PJ_colormap_registry_t
// ---------------------------------------------------------------------------

/// Signature of a color evaluation callback — mirrors the C ABI.
using ColorMapEvalFn = const char* (*)(double value, void* user_ctx);

/// C++ wrapper around PJ_colormap_registry_t for plugins that publish
/// colormaps. Constructed from the fat pointer delivered via
/// `bind_colormap_registry`. Empty-constructible; `valid()` tells whether
/// the host exposed a registry.
class ColorMapRegistryView {
 public:
  ColorMapRegistryView() = default;
  explicit ColorMapRegistryView(PJ_colormap_registry_t registry) : registry_(registry) {}

  [[nodiscard]] bool valid() const noexcept {
    return registry_.vtable != nullptr && registry_.ctx != nullptr;
  }

  /// Register (or replace) a named colormap. The new entry becomes active.
  [[nodiscard]] Status registerMap(std::string_view name, ColorMapEvalFn eval_fn, void* user_ctx) const {
    if (!valid() || registry_.vtable->register_map == nullptr) {
      return unexpected("colormap registry is not bound");
    }
    PJ_error_t err{};
    if (!registry_.vtable->register_map(registry_.ctx, toAbiString(name), eval_fn, user_ctx, &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// Unregister a colormap by name. Clears the active selection if it matched.
  [[nodiscard]] Status unregisterMap(std::string_view name) const {
    if (!valid() || registry_.vtable->unregister_map == nullptr) {
      return unexpected("colormap registry is not bound");
    }
    PJ_error_t err{};
    if (!registry_.vtable->unregister_map(registry_.ctx, toAbiString(name), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

 private:
  PJ_colormap_registry_t registry_{};
};

// ---------------------------------------------------------------------------
// SettingsView — typed C++ view over PJ_settings_store_t
// ---------------------------------------------------------------------------

/// A read result from SettingsView, modeled on Qt's QVariant: it holds the
/// raw stored string (or nothing, if the key was absent) and converts on
/// demand with a caller-supplied default. Scalars are stored as strings by
/// SettingsView::setValue and parsed back here.
class SettingsValue {
 public:
  SettingsValue() = default;
  explicit SettingsValue(std::optional<std::string> raw) : raw_(std::move(raw)) {}

  /// True when the key was absent (no value stored).
  [[nodiscard]] bool isNull() const noexcept {
    return !raw_.has_value();
  }

  [[nodiscard]] std::string toString(std::string_view def = {}) const {
    return raw_.has_value() ? *raw_ : std::string(def);
  }

  [[nodiscard]] std::int64_t toInt(std::int64_t def = 0) const {
    return raw_.has_value() ? parseNumber<std::int64_t>(*raw_).value_or(def) : def;
  }

  [[nodiscard]] double toDouble(double def = 0.0) const {
    return raw_.has_value() ? parseNumber<double>(*raw_).value_or(def) : def;
  }

  /// "true"/"1"/"on" → true; "false"/"0"/"off" → false; otherwise @p def.
  [[nodiscard]] bool toBool(bool def = false) const {
    if (!raw_.has_value()) {
      return def;
    }
    const std::string& s = *raw_;
    if (s == "true" || s == "1" || s == "on") {
      return true;
    }
    if (s == "false" || s == "0" || s == "off") {
      return false;
    }
    return def;
  }

 private:
  std::optional<std::string> raw_;
};

/// C++ wrapper around PJ_settings_store_t — an optional, QSettings-like
/// key/value store the host may expose to plugins (service "pj.settings.v1").
/// Empty-constructible; `valid()` tells whether the host bound a store.
/// Scalars are stored as strings; reads return an Expected so a backend fault
/// is visible rather than silently masked as a missing key:
///   if (auto v = settings.value("count")) { int n = v->toInt(42); }
/// An unbound store or an absent key is a successful Expected holding a null
/// value/empty list/false — only a host backend fault yields `!has_value()`.
/// All calls are main-thread, mirroring QSettings usage.
class SettingsView {
 public:
  SettingsView() = default;
  explicit SettingsView(PJ_settings_store_t store) : store_(store) {}

  [[nodiscard]] bool valid() const noexcept {
    return store_.vtable != nullptr && store_.ctx != nullptr;
  }

  // --- writes (QSettings setValue style; scalars serialized to string) ---

  [[nodiscard]] Status setValue(std::string_view key, std::string_view value) const {
    if (!valid() || store_.vtable->set_string == nullptr) {
      return unexpected("settings store is not bound");
    }
    PJ_error_t err{};
    if (!store_.vtable->set_string(store_.ctx, toAbiString(key), toAbiString(value), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  /// const char* overload so a string literal binds to the string setter
  /// rather than the bool one.
  [[nodiscard]] Status setValue(std::string_view key, const char* value) const {
    return setValue(key, std::string_view(value == nullptr ? "" : value));
  }

  [[nodiscard]] Status setValue(std::string_view key, std::int64_t value) const {
    const std::string s = std::to_string(value);
    return setValue(key, std::string_view(s));
  }

  [[nodiscard]] Status setValue(std::string_view key, int value) const {
    return setValue(key, static_cast<std::int64_t>(value));
  }

  [[nodiscard]] Status setValue(std::string_view key, double value) const {
    char buf[40];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec != std::errc{}) {
      return unexpected("settings: failed to format double value");
    }
    return setValue(key, std::string_view(buf, static_cast<std::size_t>(ptr - buf)));
  }

  [[nodiscard]] Status setValue(std::string_view key, bool value) const {
    return setValue(key, std::string_view(value ? "true" : "false"));
  }

  [[nodiscard]] Status setValue(std::string_view key, const std::vector<std::string>& values) const {
    if (!valid() || store_.vtable->set_string_list == nullptr) {
      return unexpected("settings store is not bound");
    }
    std::vector<PJ_string_view_t> raw;
    raw.reserve(values.size());
    for (const auto& v : values) {
      raw.push_back(toAbiString(v));
    }
    PJ_error_t err{};
    if (!store_.vtable->set_string_list(store_.ctx, toAbiString(key), raw.data(), raw.size(), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

  // --- reads ---

  /// Read a scalar as a QVariant-like SettingsValue. A failed Expected means
  /// the host backend faulted; success holds a null value (isNull()) when the
  /// store is unbound (optional service) or the key is absent. So `!has_value()`
  /// is exactly a real host error, not a missing key.
  [[nodiscard]] Expected<SettingsValue> value(std::string_view key) const {
    if (!valid() || store_.vtable->get_string == nullptr) {
      return SettingsValue{};
    }
    PJ_string_view_t out{};
    bool found = false;
    PJ_error_t err{};
    if (!store_.vtable->get_string(store_.ctx, toAbiString(key), &out, &found, &err)) {
      return unexpected(errorToString(err));
    }
    if (!found) {
      return SettingsValue{};
    }
    // Copy out of the host's scratch buffer before it can be reused.
    return SettingsValue{std::string(toStringView(out))};
  }

  /// Read a string list. A failed Expected means the host backend faulted;
  /// success holds an empty vector when the store is unbound or the key is
  /// absent.
  [[nodiscard]] Expected<std::vector<std::string>> valueStringList(std::string_view key) const {
    std::vector<std::string> result;
    if (!valid() || store_.vtable->get_string_list == nullptr) {
      return result;
    }
    const PJ_string_view_t* items = nullptr;
    std::size_t count = 0;
    bool found = false;
    PJ_error_t err{};
    if (!store_.vtable->get_string_list(store_.ctx, toAbiString(key), &items, &count, &found, &err)) {
      return unexpected(errorToString(err));
    }
    if (!found) {
      return result;
    }
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      result.emplace_back(toStringView(items[i]));
    }
    return result;
  }

  /// Report whether a key exists. A failed Expected means the host backend
  /// faulted; success holds false when the store is unbound.
  [[nodiscard]] Expected<bool> contains(std::string_view key) const {
    if (!valid() || store_.vtable->contains == nullptr) {
      return false;
    }
    bool present = false;
    PJ_error_t err{};
    if (!store_.vtable->contains(store_.ctx, toAbiString(key), &present, &err)) {
      return unexpected(errorToString(err));
    }
    return present;
  }

  [[nodiscard]] Status remove(std::string_view key) const {
    if (!valid() || store_.vtable->remove_key == nullptr) {
      return unexpected("settings store is not bound");
    }
    PJ_error_t err{};
    if (!store_.vtable->remove_key(store_.ctx, toAbiString(key), &err)) {
      return unexpected(errorToString(err));
    }
    return okStatus();
  }

 private:
  PJ_settings_store_t store_{};
};

}  // namespace PJ::sdk
