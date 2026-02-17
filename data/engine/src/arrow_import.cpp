#include "pj/engine/arrow_import.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nanoarrow/nanoarrow.h"
#include "nanoarrow/nanoarrow.hpp"
#include "nanoarrow/nanoarrow_ipc.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

#include "pj/base/type_tree.hpp"
#include "pj/base/types.hpp"
#include "pj/engine/column_buffer.hpp"
#include "pj/engine/writer.hpp"

namespace pj::engine::arrow_import {
namespace {

// ---------------------------------------------------------------------------
// Non-owning IPC input stream from absl::Span<const uint8_t>
// ---------------------------------------------------------------------------

struct SpanInputStreamData {
  const uint8_t* data;
  int64_t size;
  int64_t offset;
};

ArrowErrorCode span_input_stream_read(
    ArrowIpcInputStream* stream, uint8_t* buf,
    int64_t buf_size_bytes, int64_t* size_read_out,
    ArrowError* /*error*/) {
  auto* s = static_cast<SpanInputStreamData*>(stream->private_data);
  const int64_t available = s->size - s->offset;
  const int64_t to_read = std::min(buf_size_bytes, available);
  if (to_read > 0) {
    std::memcpy(buf, s->data + s->offset, static_cast<std::size_t>(to_read));
    s->offset += to_read;
  }
  *size_read_out = to_read;
  return NANOARROW_OK;
}

void span_input_stream_release(ArrowIpcInputStream* stream) {
  delete static_cast<SpanInputStreamData*>(stream->private_data);
  stream->private_data = nullptr;
  stream->release = nullptr;
}

void init_span_input_stream(ArrowIpcInputStream* stream,
                            absl::Span<const uint8_t> span) {
  stream->read = span_input_stream_read;
  stream->release = span_input_stream_release;
  stream->private_data = new SpanInputStreamData{
      span.data(), static_cast<int64_t>(span.size()), 0};
}

// ---------------------------------------------------------------------------
// nanoarrow ArrowType → PrimitiveType
// ---------------------------------------------------------------------------

std::optional<PrimitiveType> nanoarrow_type_to_primitive(ArrowType type) {
  switch (type) {
    case NANOARROW_TYPE_INT8:         return PrimitiveType::kInt8;
    case NANOARROW_TYPE_INT16:        return PrimitiveType::kInt16;
    case NANOARROW_TYPE_INT32:        return PrimitiveType::kInt32;
    case NANOARROW_TYPE_INT64:        return PrimitiveType::kInt64;
    case NANOARROW_TYPE_UINT8:        return PrimitiveType::kUint8;
    case NANOARROW_TYPE_UINT16:       return PrimitiveType::kUint16;
    case NANOARROW_TYPE_UINT32:       return PrimitiveType::kUint32;
    case NANOARROW_TYPE_UINT64:       return PrimitiveType::kUint64;
    case NANOARROW_TYPE_FLOAT:        return PrimitiveType::kFloat32;
    case NANOARROW_TYPE_DOUBLE:       return PrimitiveType::kFloat64;
    case NANOARROW_TYPE_BOOL:         return PrimitiveType::kBool;
    case NANOARROW_TYPE_STRING:
    case NANOARROW_TYPE_LARGE_STRING: return PrimitiveType::kString;
    default:                          return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// Helpers: extract raw data from nanoarrow ArrowArrayView children
// ---------------------------------------------------------------------------

struct ColumnDataWithBuffer {
  ColumnData col_data;
  std::vector<int64_t> int64_buf;
  std::vector<uint64_t> uint64_buf;
};

ColumnDataWithBuffer make_column_data_nanoarrow(
    const ArrowArrayView* child,
    const ArrowColumnMapping& mapping,
    int64_t length) {
  ColumnDataWithBuffer result;
  const auto sk = storage_kind_of(mapping.pj_type);
  const auto n = static_cast<std::size_t>(length);

  // Validity bitmap
  const uint8_t* validity = nullptr;
  std::size_t validity_offset = 0;
  if (child->null_count > 0 &&
      child->buffer_views[0].data.as_uint8 != nullptr) {
    validity = child->buffer_views[0].data.as_uint8;
    validity_offset = static_cast<std::size_t>(child->offset);
  }

  switch (sk) {
    case StorageKind::kFloat32: {
      result.col_data = ColumnData::Float32(
          mapping.pj_column_index,
          child->buffer_views[1].data.as_float + child->offset,
          n, validity, validity_offset);
      break;
    }
    case StorageKind::kFloat64: {
      result.col_data = ColumnData::Float64(
          mapping.pj_column_index,
          child->buffer_views[1].data.as_double + child->offset,
          n, validity, validity_offset);
      break;
    }
    case StorageKind::kInt32: {
      result.col_data = ColumnData::Int32(
          mapping.pj_column_index,
          child->buffer_views[1].data.as_int32 + child->offset,
          n, validity, validity_offset);
      break;
    }
    case StorageKind::kInt64: {
      switch (mapping.pj_type) {
        case PrimitiveType::kInt8:
        case PrimitiveType::kInt16: {
          result.int64_buf.resize(n);
          for (int64_t i = 0; i < length; ++i) {
            result.int64_buf[static_cast<std::size_t>(i)] =
                ArrowArrayViewGetIntUnsafe(child, i);
          }
          result.col_data = ColumnData::Int64(
              mapping.pj_column_index, result.int64_buf.data(), n,
              validity, validity_offset);
          break;
        }
        case PrimitiveType::kInt64: {
          result.col_data = ColumnData::Int64(
              mapping.pj_column_index,
              child->buffer_views[1].data.as_int64 + child->offset,
              n, validity, validity_offset);
          break;
        }
        default:
          break;
      }
      break;
    }
    case StorageKind::kUint64: {
      switch (mapping.pj_type) {
        case PrimitiveType::kUint8:
        case PrimitiveType::kUint16:
        case PrimitiveType::kUint32: {
          result.uint64_buf.resize(n);
          for (int64_t i = 0; i < length; ++i) {
            result.uint64_buf[static_cast<std::size_t>(i)] =
                ArrowArrayViewGetUIntUnsafe(child, i);
          }
          result.col_data = ColumnData::Uint64(
              mapping.pj_column_index, result.uint64_buf.data(), n,
              validity, validity_offset);
          break;
        }
        case PrimitiveType::kUint64: {
          result.col_data = ColumnData::Uint64(
              mapping.pj_column_index,
              child->buffer_views[1].data.as_uint64 + child->offset,
              n, validity, validity_offset);
          break;
        }
        default:
          break;
      }
      break;
    }
    case StorageKind::kBool: {
      // Arrow stores bools as packed bits; we need unpacked uint8_t.
      std::vector<uint8_t> bool_buf(n);
      for (int64_t i = 0; i < length; ++i) {
        bool_buf[static_cast<std::size_t>(i)] =
            ArrowArrayViewGetIntUnsafe(child, i) != 0 ? uint8_t{1}
                                                      : uint8_t{0};
      }
      // Store in uint64_buf as raw bytes
      result.uint64_buf.resize(
          (n + sizeof(uint64_t) - 1) / sizeof(uint64_t));
      std::memcpy(result.uint64_buf.data(), bool_buf.data(), n);
      result.col_data = ColumnData::Bool(
          mapping.pj_column_index,
          reinterpret_cast<const uint8_t*>(result.uint64_buf.data()), n,
          validity, validity_offset);
      break;
    }
    case StorageKind::kString: {
      // STRING: int32_t offsets in buffer_views[1], char data in buffer_views[2]
      const auto* offsets_ptr =
          child->buffer_views[1].data.as_int32 + child->offset;
      result.col_data = ColumnData::String(
          mapping.pj_column_index,
          reinterpret_cast<const uint32_t*>(offsets_ptr),
          child->buffer_views[2].data.as_char,
          n, validity, validity_offset);
      break;
    }
  }

  return result;
}

/// Extract timestamps from an ArrowArrayView child column.
std::vector<Timestamp> extract_timestamps_nanoarrow(
    const ArrowArrayView* view, int64_t length) {
  const auto n = static_cast<std::size_t>(length);
  std::vector<Timestamp> result(n);

  if (view->storage_type == NANOARROW_TYPE_INT64) {
    const auto* raw = view->buffer_views[1].data.as_int64 + view->offset;
    std::memcpy(result.data(), raw, n * sizeof(Timestamp));
  } else if (view->storage_type == NANOARROW_TYPE_UINT64) {
    const auto* raw = view->buffer_views[1].data.as_uint64 + view->offset;
    std::memcpy(result.data(), raw, n * sizeof(Timestamp));
  } else if (view->storage_type == NANOARROW_TYPE_INT32) {
    const auto* raw = view->buffer_views[1].data.as_int32 + view->offset;
    for (int64_t i = 0; i < length; ++i) {
      result[static_cast<std::size_t>(i)] =
          static_cast<Timestamp>(raw[i]);
    }
  } else {
    for (int64_t i = 0; i < length; ++i) {
      result[static_cast<std::size_t>(i)] = i;
    }
  }

  return result;
}

std::vector<Timestamp> generate_sequential_timestamps(int64_t length) {
  const auto n = static_cast<std::size_t>(length);
  std::vector<Timestamp> result(n);
  for (int64_t i = 0; i < length; ++i) {
    result[static_cast<std::size_t>(i)] = i;
  }
  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// schema_from_ipc
// ---------------------------------------------------------------------------

absl::StatusOr<std::pair<
    std::shared_ptr<pj::TypeTreeNode>,
    std::vector<ArrowColumnMapping>>>
schema_from_ipc(absl::Span<const uint8_t> ipc_stream) {
  ArrowIpcInputStream input;
  init_span_input_stream(&input, ipc_stream);

  nanoarrow::UniqueArrayStream stream;
  int rc = ArrowIpcArrayStreamReaderInit(stream.get(), &input, nullptr);
  if (rc != NANOARROW_OK) {
    return absl::InternalError("Failed to initialize IPC stream reader");
  }

  nanoarrow::UniqueSchema schema;
  rc = stream->get_schema(stream.get(), schema.get());
  if (rc != NANOARROW_OK) {
    return absl::InvalidArgumentError(
        "Failed to read schema from IPC stream");
  }

  std::vector<ArrowColumnMapping> mappings;
  std::vector<std::shared_ptr<pj::TypeTreeNode>> children;

  for (int64_t i = 0; i < schema->n_children; ++i) {
    ArrowSchemaView view;
    ArrowError error;
    rc = ArrowSchemaViewInit(&view, schema->children[i], &error);
    if (rc != NANOARROW_OK) {
      continue;  // skip unrecognized types
    }

    auto pj_type = nanoarrow_type_to_primitive(view.type);
    if (!pj_type.has_value()) {
      continue;  // skip unsupported types
    }

    ArrowColumnMapping m;
    m.arrow_column_index = static_cast<int>(i);
    m.pj_column_index = mappings.size();
    m.pj_type = *pj_type;
    m.field_name = schema->children[i]->name != nullptr
                       ? schema->children[i]->name
                       : "";

    children.push_back(pj::make_primitive(m.field_name, *pj_type));
    mappings.push_back(std::move(m));
  }

  if (mappings.empty()) {
    return absl::InvalidArgumentError(
        "No supported columns found in Arrow IPC schema");
  }

  auto type_tree = pj::make_struct("arrow_row", std::move(children));
  return std::make_pair(std::move(type_tree), std::move(mappings));
}

// ---------------------------------------------------------------------------
// import_ipc_stream
// ---------------------------------------------------------------------------

absl::Status import_ipc_stream(
    DataWriter& writer,
    TopicId topic_id,
    absl::Span<const uint8_t> ipc_stream,
    const std::vector<ArrowColumnMapping>& mappings,
    int timestamp_column) {
  ArrowIpcInputStream input;
  init_span_input_stream(&input, ipc_stream);

  nanoarrow::UniqueArrayStream stream;
  int rc = ArrowIpcArrayStreamReaderInit(stream.get(), &input, nullptr);
  if (rc != NANOARROW_OK) {
    return absl::InternalError("Failed to initialize IPC stream reader");
  }

  // Read schema (required by IPC stream format)
  nanoarrow::UniqueSchema schema;
  rc = stream->get_schema(stream.get(), schema.get());
  if (rc != NANOARROW_OK) {
    return absl::InvalidArgumentError(
        "Failed to read schema from IPC stream");
  }

  // Initialize array view from schema for decoding batches
  nanoarrow::UniqueArrayView array_view;
  rc = ArrowArrayViewInitFromSchema(array_view.get(), schema.get(), nullptr);
  if (rc != NANOARROW_OK) {
    return absl::InternalError(
        "Failed to initialize ArrowArrayView from schema");
  }

  // Iterate over record batches
  nanoarrow::UniqueArray batch;
  while (true) {
    batch.reset();
    rc = stream->get_next(stream.get(), batch.get());
    if (rc != NANOARROW_OK) {
      return absl::InternalError(
          "Failed to read next batch from IPC stream");
    }
    if (batch->release == nullptr) {
      break;  // end of stream
    }

    const int64_t num_rows = batch->length;
    if (num_rows == 0) {
      continue;
    }

    // Set array data into the view for buffer access
    rc = ArrowArrayViewSetArray(array_view.get(), batch.get(), nullptr);
    if (rc != NANOARROW_OK) {
      return absl::InternalError(
          "Failed to set array on ArrowArrayView");
    }

    // Extract timestamps
    std::vector<Timestamp> timestamps;
    if (timestamp_column >= 0) {
      if (timestamp_column >=
          static_cast<int>(array_view->n_children)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "timestamp_column ", timestamp_column,
            " out of range (", array_view->n_children, " children)"));
      }
      timestamps = extract_timestamps_nanoarrow(
          array_view->children[timestamp_column], num_rows);
    } else {
      timestamps = generate_sequential_timestamps(num_rows);
    }

    // Build ColumnData for each mapping
    std::vector<ColumnDataWithBuffer> col_buffers;
    col_buffers.reserve(mappings.size());
    std::vector<ColumnData> col_data_vec;
    col_data_vec.reserve(mappings.size());

    for (const auto& mapping : mappings) {
      if (mapping.arrow_column_index >=
          static_cast<int>(array_view->n_children)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Arrow column index ", mapping.arrow_column_index,
            " out of range"));
      }
      col_buffers.push_back(make_column_data_nanoarrow(
          array_view->children[mapping.arrow_column_index],
          mapping, num_rows));
    }

    for (auto& cb : col_buffers) {
      col_data_vec.push_back(cb.col_data);
    }

    auto status = writer.append_columns(topic_id, timestamps, col_data_vec);
    if (!status.ok()) {
      return status;
    }
  }

  return absl::OkStatus();
}

}  // namespace pj::engine::arrow_import
