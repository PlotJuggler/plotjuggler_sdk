#include "pj_datastore/plugin_host_read.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "pj_base/expected.hpp"
#include "pj_base/type_tree.hpp"
#include "pj_base/types.hpp"
#include "pj_datastore/chunk.hpp"
#include "pj_datastore/column_buffer.hpp"
#include "pj_datastore/engine.hpp"
#include "pj_datastore/plugin_host_types.hpp"
#include "pj_datastore/query.hpp"
#include "pj_datastore/topic_storage.hpp"

namespace PJ {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct PluginHostRead::Impl {
  explicit Impl(const DataEngine& eng) : engine_(eng) {}

  const DataEngine& engine_;

  // Catalog snapshot storage — owned buffers that back the CatalogView spans
  std::vector<DataSourceInfoView> ds_views_;
  std::vector<TopicInfoView> topic_views_;
  std::vector<FieldInfoView> field_views_;
};

// ---------------------------------------------------------------------------
// Construction / destruction / move
// ---------------------------------------------------------------------------

PluginHostRead::PluginHostRead(const DataEngine& engine) : impl_(std::make_unique<Impl>(engine)) {}
PluginHostRead::~PluginHostRead() = default;
PluginHostRead::PluginHostRead(PluginHostRead&&) noexcept = default;
PluginHostRead& PluginHostRead::operator=(PluginHostRead&&) noexcept = default;

// ---------------------------------------------------------------------------
// Column descriptor resolution (mirrors writer's 3-tier fallback)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

namespace {

// Resolve column descriptors for a topic, falling back to sealed chunks
// when the persisted layout is empty (e.g. schema-backed topics).
Span<const ColumnDescriptor> resolveColumnDescriptors(const TopicStorage& storage) {
  const auto& stored = storage.columnDescriptors();
  if (!stored.empty()) {
    return stored;
  }
  // Fall back to first sealed chunk's column descriptors.
  // This covers schema-backed topics where the writer built columns from the
  // TypeRegistry but never persisted them via setColumnDescriptors().
  const auto& chunks = storage.sealedChunks();
  if (!chunks.empty()) {
    return chunks[0].column_descriptors;
  }
  return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// Catalog enumeration
// ---------------------------------------------------------------------------

CatalogView PluginHostRead::catalogView() {
  impl_->ds_views_.clear();
  impl_->topic_views_.clear();
  impl_->field_views_.clear();

  auto dataset_ids = impl_->engine_.listDatasets();
  std::sort(dataset_ids.begin(), dataset_ids.end());

  for (const DatasetId ds_id : dataset_ids) {
    const auto* dataset = impl_->engine_.getDataset(ds_id);
    if (dataset == nullptr) {
      continue;
    }

    const auto topic_start = static_cast<uint32_t>(impl_->topic_views_.size());
    const auto topic_ids = impl_->engine_.listTopics(ds_id);

    for (const TopicId tid : topic_ids) {
      const auto* storage = impl_->engine_.getTopicStorage(tid);
      if (storage == nullptr) {
        continue;
      }

      const auto field_start = static_cast<uint32_t>(impl_->field_views_.size());
      const auto col_descs = resolveColumnDescriptors(*storage);

      for (const auto& col : col_descs) {
        FieldInfoView fv;
        fv.handle = FieldHandle{.topic = TopicHandle{.id = tid}, .id = col.field_id};
        fv.name = col.field_path;
        fv.type = toFieldTypeWidened(col.logical_type);
        impl_->field_views_.push_back(fv);
      }

      const auto field_count = static_cast<uint32_t>(impl_->field_views_.size()) - field_start;

      TopicInfoView tv;
      tv.handle = TopicHandle{.id = tid};
      tv.source = DataSourceHandle{.id = ds_id};
      tv.name = storage->descriptor().name;
      tv.first_field = field_start;
      tv.field_count = field_count;
      impl_->topic_views_.push_back(tv);
    }

    const auto topic_count = static_cast<uint32_t>(impl_->topic_views_.size()) - topic_start;

    DataSourceInfoView dv;
    dv.handle = DataSourceHandle{.id = ds_id};
    dv.name = dataset->source_name;
    dv.first_topic = topic_start;
    dv.topic_count = topic_count;
    impl_->ds_views_.push_back(dv);
  }

  CatalogView view;
  view.data_sources = impl_->ds_views_;
  view.topics = impl_->topic_views_;
  view.fields = impl_->field_views_;
  return view;
}

// ---------------------------------------------------------------------------
// Materialized field read
// ---------------------------------------------------------------------------

namespace {

// Find the column index in a chunk that has the target field_id.
// Returns -1 if not found (schema evolution — old chunk lacks the field).
int findColumnIndex(const TopicChunk& chunk, FieldId target_field_id) {
  for (std::size_t i = 0; i < chunk.column_descriptors.size(); ++i) {
    if (chunk.column_descriptors[i].field_id == target_field_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace

Expected<MaterializedSeries> PluginHostRead::readSeries(FieldHandle field) const {
  // Validate topic exists
  const auto* storage = impl_->engine_.getTopicStorage(field.topic.id);
  if (storage == nullptr) {
    return PJ::unexpected(absl::StrCat("readSeries: topic ", field.topic.id, " not found"));
  }

  // Find the field in the topic's column descriptors to determine its type
  const auto col_descs = resolveColumnDescriptors(*storage);
  PrimitiveType prim_type = PrimitiveType::kFloat64;
  bool found = false;
  for (const auto& col : col_descs) {
    if (col.field_id == field.id) {
      prim_type = col.logical_type;
      found = true;
      break;
    }
  }
  if (!found) {
    return PJ::unexpected(absl::StrCat("readSeries: field ", field.id, " not found in topic ", field.topic.id));
  }

  // Determine the FieldType and prepare the result
  const FieldType ft = toFieldTypeWidened(prim_type);
  const DatasetId ds_id = storage->descriptor().dataset_id;

  MaterializedSeries result;
  result.source = DataSourceHandle{.id = ds_id};
  result.topic = field.topic;
  result.field = field;
  result.type = ft;

  // Count total rows across all chunks (for this field)
  const auto& chunks = storage->sealedChunks();
  std::size_t total_rows = 0;
  for (const auto& chunk : chunks) {
    const int col_idx = findColumnIndex(chunk, field.id);
    if (col_idx >= 0) {
      total_rows += chunk.stats.row_count;
    }
  }

  if (total_rows == 0) {
    // Initialize empty variant of the right type
    switch (ft) {
      case FieldType::kFloat32: result.values = std::vector<float>{}; break;
      case FieldType::kFloat64: result.values = std::vector<double>{}; break;
      case FieldType::kInt32: result.values = std::vector<int32_t>{}; break;
      case FieldType::kInt64: result.values = std::vector<int64_t>{}; break;
      case FieldType::kUint64: result.values = std::vector<uint64_t>{}; break;
      case FieldType::kBool: result.values = BoolSeriesValues{}; break;
      case FieldType::kString: result.values = StringSeriesValues{}; break;
    }
    return result;
  }

  result.timestamps.reserve(total_rows);

  // Allocate validity bits (packed: 1 bit per row, rounded up to bytes)
  const std::size_t validity_bytes = (total_rows + 7) / 8;
  result.validity_bits.resize(validity_bytes, 0xFF);  // all valid initially

  // Materialize per type
  switch (ft) {
    case FieldType::kFloat32: {
      auto& vals = result.values.emplace<std::vector<float>>();
      vals.reserve(total_rows);
      std::size_t global_row = 0;
      for (const auto& chunk : chunks) {
        const int col_idx = findColumnIndex(chunk, field.id);
        if (col_idx < 0) {
          continue;
        }
        for (uint32_t r = 0; r < chunk.stats.row_count; ++r) {
          result.timestamps.push_back(chunk.readTimestamp(r));
          if (chunk.isNull(static_cast<std::size_t>(col_idx), r)) {
            vals.push_back(0.0F);
            result.validity_bits[global_row / 8] &= static_cast<uint8_t>(~(1U << (global_row % 8)));
          } else {
            vals.push_back(
                static_cast<float>(chunk.readNumericAsDouble(static_cast<std::size_t>(col_idx), r)));
          }
          ++global_row;
        }
      }
      break;
    }
    case FieldType::kFloat64: {
      auto& vals = result.values.emplace<std::vector<double>>();
      vals.reserve(total_rows);
      std::size_t global_row = 0;
      for (const auto& chunk : chunks) {
        const int col_idx = findColumnIndex(chunk, field.id);
        if (col_idx < 0) {
          continue;
        }
        for (uint32_t r = 0; r < chunk.stats.row_count; ++r) {
          result.timestamps.push_back(chunk.readTimestamp(r));
          if (chunk.isNull(static_cast<std::size_t>(col_idx), r)) {
            vals.push_back(0.0);
            result.validity_bits[global_row / 8] &= static_cast<uint8_t>(~(1U << (global_row % 8)));
          } else {
            vals.push_back(chunk.readNumericAsDouble(static_cast<std::size_t>(col_idx), r));
          }
          ++global_row;
        }
      }
      break;
    }
    case FieldType::kInt32: {
      auto& vals = result.values.emplace<std::vector<int32_t>>();
      vals.reserve(total_rows);
      std::size_t global_row = 0;
      for (const auto& chunk : chunks) {
        const int col_idx = findColumnIndex(chunk, field.id);
        if (col_idx < 0) {
          continue;
        }
        for (uint32_t r = 0; r < chunk.stats.row_count; ++r) {
          result.timestamps.push_back(chunk.readTimestamp(r));
          if (chunk.isNull(static_cast<std::size_t>(col_idx), r)) {
            vals.push_back(0);
            result.validity_bits[global_row / 8] &= static_cast<uint8_t>(~(1U << (global_row % 8)));
          } else {
            vals.push_back(
                static_cast<int32_t>(chunk.readNumericAsDouble(static_cast<std::size_t>(col_idx), r)));
          }
          ++global_row;
        }
      }
      break;
    }
    case FieldType::kInt64: {
      auto& vals = result.values.emplace<std::vector<int64_t>>();
      vals.reserve(total_rows);
      std::size_t global_row = 0;
      for (const auto& chunk : chunks) {
        const int col_idx = findColumnIndex(chunk, field.id);
        if (col_idx < 0) {
          continue;
        }
        for (uint32_t r = 0; r < chunk.stats.row_count; ++r) {
          result.timestamps.push_back(chunk.readTimestamp(r));
          if (chunk.isNull(static_cast<std::size_t>(col_idx), r)) {
            vals.push_back(0);
            result.validity_bits[global_row / 8] &= static_cast<uint8_t>(~(1U << (global_row % 8)));
          } else {
            vals.push_back(
                static_cast<int64_t>(chunk.readNumericAsDouble(static_cast<std::size_t>(col_idx), r)));
          }
          ++global_row;
        }
      }
      break;
    }
    case FieldType::kUint64: {
      auto& vals = result.values.emplace<std::vector<uint64_t>>();
      vals.reserve(total_rows);
      std::size_t global_row = 0;
      for (const auto& chunk : chunks) {
        const int col_idx = findColumnIndex(chunk, field.id);
        if (col_idx < 0) {
          continue;
        }
        for (uint32_t r = 0; r < chunk.stats.row_count; ++r) {
          result.timestamps.push_back(chunk.readTimestamp(r));
          if (chunk.isNull(static_cast<std::size_t>(col_idx), r)) {
            vals.push_back(0);
            result.validity_bits[global_row / 8] &= static_cast<uint8_t>(~(1U << (global_row % 8)));
          } else {
            vals.push_back(
                static_cast<uint64_t>(chunk.readNumericAsDouble(static_cast<std::size_t>(col_idx), r)));
          }
          ++global_row;
        }
      }
      break;
    }
    case FieldType::kBool: {
      auto& bsv = result.values.emplace<BoolSeriesValues>();
      bsv.values.reserve(total_rows);
      std::size_t global_row = 0;
      for (const auto& chunk : chunks) {
        const int col_idx = findColumnIndex(chunk, field.id);
        if (col_idx < 0) {
          continue;
        }
        for (uint32_t r = 0; r < chunk.stats.row_count; ++r) {
          result.timestamps.push_back(chunk.readTimestamp(r));
          if (chunk.isNull(static_cast<std::size_t>(col_idx), r)) {
            bsv.values.push_back(0);
            result.validity_bits[global_row / 8] &= static_cast<uint8_t>(~(1U << (global_row % 8)));
          } else {
            bsv.values.push_back(chunk.readBool(static_cast<std::size_t>(col_idx), r) ? 1 : 0);
          }
          ++global_row;
        }
      }
      break;
    }
    case FieldType::kString: {
      auto& ssv = result.values.emplace<StringSeriesValues>();
      ssv.offsets.reserve(total_rows + 1);
      ssv.offsets.push_back(0);
      std::size_t global_row = 0;
      for (const auto& chunk : chunks) {
        const int col_idx = findColumnIndex(chunk, field.id);
        if (col_idx < 0) {
          continue;
        }
        for (uint32_t r = 0; r < chunk.stats.row_count; ++r) {
          result.timestamps.push_back(chunk.readTimestamp(r));
          if (chunk.isNull(static_cast<std::size_t>(col_idx), r)) {
            ssv.offsets.push_back(static_cast<uint32_t>(ssv.bytes.size()));
            result.validity_bits[global_row / 8] &= static_cast<uint8_t>(~(1U << (global_row % 8)));
          } else {
            const std::string_view sv = chunk.readString(static_cast<std::size_t>(col_idx), r);
            ssv.bytes.insert(ssv.bytes.end(), sv.begin(), sv.end());
            ssv.offsets.push_back(static_cast<uint32_t>(ssv.bytes.size()));
          }
          ++global_row;
        }
      }
      break;
    }
  }

  return result;
}

}  // namespace PJ
