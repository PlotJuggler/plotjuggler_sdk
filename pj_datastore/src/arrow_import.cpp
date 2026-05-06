// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include "pj_datastore/arrow_import.hpp"

#include <fmt/format.h>

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
#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/column_buffer.hpp"
#include "pj_datastore/writer.hpp"

namespace PJ::arrow_import {
namespace {

// ---------------------------------------------------------------------------
// Non-owning IPC input stream from PJ::Span<const uint8_t>
// ---------------------------------------------------------------------------

struct SpanInputStreamData {
  const uint8_t* data;
  int64_t size;
  int64_t offset;
};

ArrowErrorCode span_input_stream_read(
    ArrowIpcInputStream* stream, uint8_t* buf, int64_t buf_size_bytes, int64_t* size_read_out, ArrowError* /*error*/) {
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

void init_span_input_stream(ArrowIpcInputStream* stream, PJ::Span<const uint8_t> span) {
  stream->read = span_input_stream_read;
  stream->release = span_input_stream_release;
  stream->private_data = new SpanInputStreamData{span.data(), static_cast<int64_t>(span.size()), 0};
}

// ---------------------------------------------------------------------------
// nanoarrow ArrowType → PrimitiveType
// ---------------------------------------------------------------------------

std::optional<PrimitiveType> nanoarrow_type_to_primitive(ArrowType type) {
  switch (type) {
    case NANOARROW_TYPE_INT8:
      return PrimitiveType::kInt8;
    case NANOARROW_TYPE_INT16:
      return PrimitiveType::kInt16;
    case NANOARROW_TYPE_INT32:
      return PrimitiveType::kInt32;
    case NANOARROW_TYPE_INT64:
      return PrimitiveType::kInt64;
    case NANOARROW_TYPE_UINT8:
      return PrimitiveType::kUint8;
    case NANOARROW_TYPE_UINT16:
      return PrimitiveType::kUint16;
    case NANOARROW_TYPE_UINT32:
      return PrimitiveType::kUint32;
    case NANOARROW_TYPE_UINT64:
      return PrimitiveType::kUint64;
    case NANOARROW_TYPE_FLOAT:
      return PrimitiveType::kFloat32;
    case NANOARROW_TYPE_DOUBLE:
      return PrimitiveType::kFloat64;
    case NANOARROW_TYPE_BOOL:
      return PrimitiveType::kBool;
    case NANOARROW_TYPE_STRING:
    case NANOARROW_TYPE_LARGE_STRING:
      return PrimitiveType::kString;
    default:
      return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// Helpers: extract raw data from nanoarrow ArrowArrayView children
// ---------------------------------------------------------------------------

struct ColumnDataWithBuffer {
  ColumnData col_data;
  std::vector<int64_t> int64_buf;
  std::vector<uint64_t> uint64_buf;
  std::vector<uint32_t> offset_buf;
};

ColumnDataWithBuffer make_column_data_nanoarrow(
    const ArrowArrayView* child, const ArrowColumnMapping& mapping, int64_t length) {
  ColumnDataWithBuffer result;
  const auto sk = storageKindOf(mapping.pj_type);
  const auto n = static_cast<std::size_t>(length);

  // Validity bitmap
  BitSpan validity_view;
  if (child->null_count > 0 && child->buffer_views[0].data.as_uint8 != nullptr) {
    const std::size_t validity_offset = static_cast<std::size_t>(child->offset);
    const std::size_t validity_bytes = (validity_offset + n + 7) / 8;
    validity_view =
        BitSpan{Span<const uint8_t>(child->buffer_views[0].data.as_uint8, validity_bytes), validity_offset, n};
  }

  switch (sk) {
    case StorageKind::kFloat32: {
      result.col_data = ColumnData::Float32(
          mapping.pj_column_index, Span<const float>(child->buffer_views[1].data.as_float + child->offset, n),
          validity_view);
      break;
    }
    case StorageKind::kFloat64: {
      result.col_data = ColumnData::Float64(
          mapping.pj_column_index, Span<const double>(child->buffer_views[1].data.as_double + child->offset, n),
          validity_view);
      break;
    }
    case StorageKind::kInt32: {
      result.col_data = ColumnData::Int32(
          mapping.pj_column_index, Span<const int32_t>(child->buffer_views[1].data.as_int32 + child->offset, n),
          validity_view);
      break;
    }
    case StorageKind::kInt64: {
      switch (mapping.pj_type) {
        case PrimitiveType::kInt8:
        case PrimitiveType::kInt16: {
          result.int64_buf.resize(n);
          for (int64_t i = 0; i < length; ++i) {
            result.int64_buf[static_cast<std::size_t>(i)] = ArrowArrayViewGetIntUnsafe(child, i);
          }
          result.col_data = ColumnData::Int64(
              mapping.pj_column_index, Span<const int64_t>(result.int64_buf.data(), n), validity_view);
          break;
        }
        case PrimitiveType::kInt64: {
          result.col_data = ColumnData::Int64(
              mapping.pj_column_index, Span<const int64_t>(child->buffer_views[1].data.as_int64 + child->offset, n),
              validity_view);
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
            result.uint64_buf[static_cast<std::size_t>(i)] = ArrowArrayViewGetUIntUnsafe(child, i);
          }
          result.col_data = ColumnData::Uint64(
              mapping.pj_column_index, Span<const uint64_t>(result.uint64_buf.data(), n), validity_view);
          break;
        }
        case PrimitiveType::kUint64: {
          result.col_data = ColumnData::Uint64(
              mapping.pj_column_index, Span<const uint64_t>(child->buffer_views[1].data.as_uint64 + child->offset, n),
              validity_view);
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
        bool_buf[static_cast<std::size_t>(i)] = ArrowArrayViewGetIntUnsafe(child, i) != 0 ? uint8_t{1} : uint8_t{0};
      }
      // Store in uint64_buf as raw bytes
      result.uint64_buf.resize((n + sizeof(uint64_t) - 1) / sizeof(uint64_t));
      std::memcpy(result.uint64_buf.data(), bool_buf.data(), n);
      result.col_data = ColumnData::Bool(
          mapping.pj_column_index, Span<const uint8_t>(reinterpret_cast<const uint8_t*>(result.uint64_buf.data()), n),
          validity_view);
      break;
    }
    case StorageKind::kString: {
      // STRING: Arrow uses int32_t offsets; PJ uses uint32_t. Copy with cast to avoid UB.
      const auto* offsets_ptr = child->buffer_views[1].data.as_int32 + child->offset;
      result.offset_buf.resize(n + 1);
      for (std::size_t i = 0; i <= n; ++i) {
        result.offset_buf[i] = static_cast<uint32_t>(offsets_ptr[i]);
      }
      result.col_data = ColumnData::String(
          mapping.pj_column_index, Span<const uint32_t>(result.offset_buf.data(), n + 1),
          Span<const char>(
              child->buffer_views[2].data.as_char, static_cast<std::size_t>(child->buffer_views[2].size_bytes)),
          validity_view);
      break;
    }
  }

  return result;
}

/// Extract timestamps from an ArrowArrayView child column.
std::vector<Timestamp> extract_timestamps_nanoarrow(const ArrowArrayView* view, int64_t length) {
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
      result[static_cast<std::size_t>(i)] = static_cast<Timestamp>(raw[i]);
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

namespace {

// Derive column mappings + type tree from an already-populated nanoarrow
// schema. Shared between schemaFromIpc and schemaFromArrowStream.
PJ::Expected<std::pair<std::shared_ptr<PJ::TypeTreeNode>, std::vector<ArrowColumnMapping>>> mappingsFromSchema(
    const ArrowSchema* schema) {
  std::vector<ArrowColumnMapping> mappings;
  std::vector<std::shared_ptr<PJ::TypeTreeNode>> children;

  for (int64_t i = 0; i < schema->n_children; ++i) {
    ArrowSchemaView view;
    ArrowError error;
    const int rc = ArrowSchemaViewInit(&view, schema->children[i], &error);
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
    m.field_name = schema->children[i]->name != nullptr ? schema->children[i]->name : "";

    children.push_back(PJ::makePrimitive(m.field_name, *pj_type));
    mappings.push_back(std::move(m));
  }

  if (mappings.empty()) {
    return PJ::unexpected("No supported columns found in Arrow schema");
  }

  auto type_tree = PJ::makeStruct("arrow_row", std::move(children));
  return std::make_pair(std::move(type_tree), std::move(mappings));
}

// Pull record batches from an ArrowArrayStream* and feed them into the
// writer. The stream's schema must already be known (caller passes it in).
// Ownership: the caller retains ownership of @p stream; this helper does
// NOT call stream->release.
PJ::Status ingestBatchesFromStream(
    DataWriter& writer, TopicId topic_id, ArrowArrayStream* stream, const ArrowSchema* schema,
    const std::vector<ArrowColumnMapping>& mappings, int timestamp_column) {
  nanoarrow::UniqueArrayView array_view;
  int rc = ArrowArrayViewInitFromSchema(array_view.get(), const_cast<ArrowSchema*>(schema), nullptr);
  if (rc != NANOARROW_OK) {
    return PJ::unexpected("Failed to initialize ArrowArrayView from schema");
  }

  nanoarrow::UniqueArray batch;
  while (true) {
    batch.reset();
    rc = stream->get_next(stream, batch.get());
    if (rc != NANOARROW_OK) {
      const char* err = stream->get_last_error != nullptr ? stream->get_last_error(stream) : nullptr;
      return PJ::unexpected(fmt::format("Failed to read next batch: {}", err != nullptr ? err : "unknown"));
    }
    if (batch->release == nullptr) {
      break;  // end of stream
    }

    const int64_t num_rows = batch->length;
    if (num_rows == 0) {
      continue;
    }

    rc = ArrowArrayViewSetArray(array_view.get(), batch.get(), nullptr);
    if (rc != NANOARROW_OK) {
      return PJ::unexpected("Failed to set array on ArrowArrayView");
    }

    std::vector<Timestamp> timestamps;
    if (timestamp_column >= 0) {
      if (timestamp_column >= static_cast<int>(array_view->n_children)) {
        return PJ::unexpected(
            fmt::format("timestamp_column {} out of range ({} children)", timestamp_column, array_view->n_children));
      }
      timestamps = extract_timestamps_nanoarrow(array_view->children[timestamp_column], num_rows);
    } else {
      timestamps = generate_sequential_timestamps(num_rows);
    }

    std::vector<ColumnDataWithBuffer> col_buffers;
    col_buffers.reserve(mappings.size());
    for (const auto& mapping : mappings) {
      if (mapping.arrow_column_index >= static_cast<int>(array_view->n_children)) {
        return PJ::unexpected(fmt::format("Arrow column index {} out of range", mapping.arrow_column_index));
      }
      col_buffers.push_back(
          make_column_data_nanoarrow(array_view->children[mapping.arrow_column_index], mapping, num_rows));
    }

    std::vector<ColumnData> col_data_vec;
    col_data_vec.reserve(col_buffers.size());
    for (auto& cb : col_buffers) {
      col_data_vec.push_back(cb.col_data);
    }

    auto status = writer.appendColumns(topic_id, timestamps, col_data_vec);
    if (!status.has_value()) {
      return status;
    }
  }

  return PJ::okStatus();
}

}  // namespace

// ---------------------------------------------------------------------------
// schemaFromIpc
// ---------------------------------------------------------------------------

PJ::Expected<std::pair<std::shared_ptr<PJ::TypeTreeNode>, std::vector<ArrowColumnMapping>>> schemaFromIpc(
    PJ::Span<const uint8_t> ipc_stream) {
  ArrowIpcInputStream input;
  init_span_input_stream(&input, ipc_stream);

  nanoarrow::UniqueArrayStream stream;
  int rc = ArrowIpcArrayStreamReaderInit(stream.get(), &input, nullptr);
  if (rc != NANOARROW_OK) {
    return PJ::unexpected("Failed to initialize IPC stream reader");
  }

  nanoarrow::UniqueSchema schema;
  rc = stream->get_schema(stream.get(), schema.get());
  if (rc != NANOARROW_OK) {
    return PJ::unexpected("Failed to read schema from IPC stream");
  }

  return mappingsFromSchema(schema.get());
}

// ---------------------------------------------------------------------------
// schemaFromArrowStream
// ---------------------------------------------------------------------------

PJ::Expected<std::pair<std::shared_ptr<PJ::TypeTreeNode>, std::vector<ArrowColumnMapping>>> schemaFromArrowStream(
    ArrowArrayStream* stream) {
  if (stream == nullptr || stream->get_schema == nullptr) {
    return PJ::unexpected("null ArrowArrayStream or missing get_schema");
  }

  nanoarrow::UniqueSchema schema;
  const int rc = stream->get_schema(stream, schema.get());
  if (rc != NANOARROW_OK) {
    const char* err = stream->get_last_error != nullptr ? stream->get_last_error(stream) : nullptr;
    return PJ::unexpected(fmt::format("Failed to read schema from ArrowArrayStream: {}", err != nullptr ? err : ""));
  }

  return mappingsFromSchema(schema.get());
}

// ---------------------------------------------------------------------------
// importIpcStream
// ---------------------------------------------------------------------------

PJ::Status importIpcStream(
    DataWriter& writer, TopicId topic_id, PJ::Span<const uint8_t> ipc_stream,
    const std::vector<ArrowColumnMapping>& mappings, int timestamp_column) {
  ArrowIpcInputStream input;
  init_span_input_stream(&input, ipc_stream);

  nanoarrow::UniqueArrayStream stream;
  int rc = ArrowIpcArrayStreamReaderInit(stream.get(), &input, nullptr);
  if (rc != NANOARROW_OK) {
    return PJ::unexpected("Failed to initialize IPC stream reader");
  }

  nanoarrow::UniqueSchema schema;
  rc = stream->get_schema(stream.get(), schema.get());
  if (rc != NANOARROW_OK) {
    return PJ::unexpected("Failed to read schema from IPC stream");
  }

  return ingestBatchesFromStream(writer, topic_id, stream.get(), schema.get(), mappings, timestamp_column);
}

// ---------------------------------------------------------------------------
// importArrowStream  (v4 Arrow C Data Interface path)
// ---------------------------------------------------------------------------

PJ::Status importArrowStream(
    DataWriter& writer, TopicId topic_id, ArrowArrayStream* stream, const std::vector<ArrowColumnMapping>& mappings,
    int timestamp_column) {
  if (stream == nullptr || stream->get_schema == nullptr || stream->get_next == nullptr) {
    return PJ::unexpected("null ArrowArrayStream or missing callbacks");
  }

  nanoarrow::UniqueSchema schema;
  int rc = stream->get_schema(stream, schema.get());
  if (rc != NANOARROW_OK) {
    const char* err = stream->get_last_error != nullptr ? stream->get_last_error(stream) : nullptr;
    return PJ::unexpected(fmt::format("Failed to read schema from ArrowArrayStream: {}", err != nullptr ? err : ""));
  }

  return ingestBatchesFromStream(writer, topic_id, stream, schema.get(), mappings, timestamp_column);
}

}  // namespace PJ::arrow_import
