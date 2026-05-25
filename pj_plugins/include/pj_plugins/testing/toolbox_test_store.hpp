/**
 * @file toolbox_test_store.hpp
 * @brief Test helper: an in-memory store that speaks the v4 toolbox host
 *        ABI, including the Arrow C Data Interface read path.
 *
 * Before this header, every toolbox unit test had to hand-roll ~130 lines
 * of Arrow C Data Interface plumbing — disjoint ArrowSchema / ArrowArray
 * payload blocks, release callbacks, buffer arrays — just to feed fake
 * data into `read_series_arrow`. This helper encapsulates all of that
 * behind a small builder-style API.
 *
 * Usage sketch:
 *
 *   PJ::testing::ToolboxTestStore store;
 *   store
 *     .addTopic("quat")
 *     .addField("quat", "x", timestamps, xs)
 *     .addField("quat", "y", timestamps, ys);
 *
 *   PJ::ServiceRegistryBuilder registry;
 *   registry.registerService<PJ::sdk::ToolboxHostService>(store.makeHost());
 *   registry.registerService<PJ::sdk::ToolboxRuntimeHostService>(store.makeRuntimeHost());
 *   ASSERT_TRUE(handle.bind(registry.view()));
 *
 *   // ... run toolbox ...
 *
 *   EXPECT_EQ(store.writtenRecords().size(), N);
 *   EXPECT_EQ(store.notifyDataChangedCalls(), 1);
 *
 * The store captures `append_record` / `append_bound_record` writes as
 * `PJ::sdk::testing::RecordedRow` (reusing the parser-write recorder shape)
 * and counts `create_data_source` / `notify_data_changed` invocations.
 *
 * Internally the read path emits the two-column Arrow struct layout
 * expected by `ToolboxHostView::readSeries()`: children[0] = int64
 * timestamp, children[1] = float64 value. Schema and array payloads
 * have disjoint ownership so the `ArrowSchemaHolder` and
 * `ArrowArrayHolder` destructors can fire in either order without
 * double-free.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/sdk/plugin_data_api.hpp"
#include "pj_base/sdk/service_traits.hpp"
#include "pj_base/sdk/testing/parser_write_recorder.hpp"
#include "pj_base/sdk/toolbox_plugin_base.hpp"

namespace PJ::testing {

using ::PJ::sdk::testing::RecordedField;
using ::PJ::sdk::testing::RecordedRow;

/// Fake toolbox host + runtime host, driven by hand-populated fields.
///
/// Thread-unsafe — intended for single-threaded toolbox unit tests.
/// Construct one per test, populate with `addTopic()` / `addField()`,
/// then hand `makeHost()` / `makeRuntimeHost()` to a
/// `ServiceRegistryBuilder`.
class ToolboxTestStore {
 public:
  ToolboxTestStore() = default;

  ToolboxTestStore(const ToolboxTestStore&) = delete;
  ToolboxTestStore& operator=(const ToolboxTestStore&) = delete;
  ToolboxTestStore(ToolboxTestStore&&) = delete;
  ToolboxTestStore& operator=(ToolboxTestStore&&) = delete;

  // ---------------------------------------------------------------------
  // Population API — call before handing the store to a plugin.
  // ---------------------------------------------------------------------

  ToolboxTestStore& addTopic(std::string_view name) {
    TopicEntry t;
    t.name = std::string(name);
    t.topic_id = next_topic_id_++;
    t.first_field = static_cast<uint32_t>(fields_.size());
    t.field_count = 0;
    topics_.push_back(std::move(t));
    return *this;
  }

  /// Add a float64 field under the named topic (must exist). Captures the
  /// caller-supplied timestamps + values by value so the store outlives
  /// the population call.
  ToolboxTestStore& addField(
      std::string_view topic_name, std::string_view field_name, std::vector<int64_t> timestamps,
      std::vector<double> values) {
    TopicEntry* topic = findTopicMut(topic_name);
    if (topic == nullptr) {
      return *this;
    }
    FieldEntry f;
    f.name = std::string(field_name);
    f.handle = PJ_field_handle_t{PJ_topic_handle_t{topic->topic_id}, next_field_id_++};
    f.timestamps = std::move(timestamps);
    f.values = std::move(values);
    fields_.push_back(std::move(f));
    ++topic->field_count;
    return *this;
  }

  /// Append additional (timestamp, value) samples to an existing field.
  /// Useful for simulating incremental data arrival between repeated
  /// toolbox invocations.
  ToolboxTestStore& extendField(
      std::string_view field_name, std::vector<int64_t> extra_timestamps, std::vector<double> extra_values) {
    for (auto& f : fields_) {
      if (f.name == field_name) {
        f.timestamps.insert(f.timestamps.end(), extra_timestamps.begin(), extra_timestamps.end());
        f.values.insert(f.values.end(), extra_values.begin(), extra_values.end());
        return *this;
      }
    }
    return *this;
  }

  // ---------------------------------------------------------------------
  // Service-host construction.
  // ---------------------------------------------------------------------

  [[nodiscard]] PJ_toolbox_host_t makeHost() noexcept {
    static const PJ_toolbox_host_vtable_t vtable = {
        .abi_version = PJ_PLUGIN_DATA_API_VERSION,
        .struct_size = sizeof(PJ_toolbox_host_vtable_t),
        .create_data_source = &ToolboxTestStore::trampolineCreateDataSource,
        .ensure_topic = &ToolboxTestStore::trampolineEnsureTopic,
        .ensure_field = &ToolboxTestStore::trampolineEnsureField,
        .append_record = &ToolboxTestStore::trampolineAppendRecord,
        .append_bound_record = &ToolboxTestStore::trampolineAppendBoundRecord,
        .append_arrow_stream = &ToolboxTestStore::trampolineAppendArrowStream,
        .acquire_catalog_snapshot = &ToolboxTestStore::trampolineAcquireCatalogSnapshot,
        .read_series_arrow = &ToolboxTestStore::trampolineReadSeriesArrow,
        // Tail-only object-topic slots — the in-memory test store doesn't
        // back an ObjectStore. SDK-side tail-slot checks turn the missing
        // slots into clear "older host" errors.
        .register_object_topic = nullptr,
        .push_owned_object = nullptr,
    };
    return PJ_toolbox_host_t{.ctx = this, .vtable = &vtable};
  }

  [[nodiscard]] PJ_toolbox_runtime_host_t makeRuntimeHost() noexcept {
    static const PJ_toolbox_runtime_host_vtable_t vtable = {
        .protocol_version = PJ_TOOLBOX_PLUGIN_PROTOCOL_VERSION,
        .struct_size = sizeof(PJ_toolbox_runtime_host_vtable_t),
        .report_message = &ToolboxTestStore::trampolineReportMessage,
        .notify_data_changed = &ToolboxTestStore::trampolineNotifyDataChanged,
    };
    return PJ_toolbox_runtime_host_t{.ctx = this, .vtable = &vtable};
  }

  // ---------------------------------------------------------------------
  // Assertion API.
  // ---------------------------------------------------------------------

  [[nodiscard]] const std::vector<RecordedRow>& writtenRecords() const noexcept {
    return written_;
  }

  /// Flattened view: one entry per (row, field) pair. Useful when tests
  /// prefer to iterate a single list rather than nested row→fields.
  struct FlatRecord {
    int64_t timestamp;
    const std::string& field_name;
    double numeric;
  };

  [[nodiscard]] std::vector<FlatRecord> flatRecords() const {
    std::vector<FlatRecord> out;
    for (const auto& row : written_) {
      for (const auto& f : row.fields) {
        out.push_back(FlatRecord{row.timestamp, f.name, f.numeric});
      }
    }
    return out;
  }

  [[nodiscard]] int createDataSourceCalls() const noexcept {
    return create_data_source_calls_;
  }

  [[nodiscard]] int notifyDataChangedCalls() const noexcept {
    return notify_data_changed_calls_;
  }

 private:
  struct TopicEntry {
    std::string name;
    uint32_t topic_id = 0;
    uint32_t first_field = 0;
    uint32_t field_count = 0;
  };

  struct FieldEntry {
    std::string name;
    PJ_field_handle_t handle{};
    std::vector<int64_t> timestamps;
    std::vector<double> values;
  };

  // --- Arrow read-path payloads ---------------------------------------
  // Schema and array hold separate heap blocks so their release callbacks
  // are independent. This lets MaterializedSeriesView destroy its
  // ArrowArrayHolder before its ArrowSchemaHolder (the default order) with
  // no double-free risk.

  struct ArrowSchemaPayload {
    ArrowSchema child_ts{};
    ArrowSchema child_val{};
    ArrowSchema* child_ptrs[2]{};
  };

  struct ArrowArrayPayload {
    std::vector<int64_t> timestamps;
    std::vector<double> values;
    ArrowArray child_ts{};
    ArrowArray child_val{};
    ArrowArray* child_ptrs[2]{};
    const void* ts_buffers[2]{};
    const void* val_buffers[2]{};
  };

  struct CatalogRelease {
    PJ_topic_info_t* topics;
    PJ_field_info_t* fields;
  };

  // ------------------------------------------------------------------
  // Lookup helpers.
  // ------------------------------------------------------------------

  TopicEntry* findTopicMut(std::string_view name) noexcept {
    for (auto& t : topics_) {
      if (t.name == name) {
        return &t;
      }
    }
    return nullptr;
  }

  const FieldEntry* findField(PJ_field_handle_t h) const noexcept {
    for (const auto& f : fields_) {
      if (f.handle.topic.id == h.topic.id && f.handle.id == h.id) {
        return &f;
      }
    }
    return nullptr;
  }

  // ------------------------------------------------------------------
  // Record-capture helpers (re-use ParserWriteRecorder's extraction).
  // ------------------------------------------------------------------

  static void extractValue(const PJ_scalar_value_t& v, RecordedField& out) noexcept {
    // Delegate to the parser recorder's extractor: same type -> value
    // mapping with numeric/bool_value/string_value slots. We can't share
    // the private static directly, so duplicate the dispatch here.
    out.type = static_cast<PrimitiveType>(v.type);
    switch (v.type) {
      case PJ_PRIMITIVE_TYPE_FLOAT64:
        out.numeric = v.data.as_float64;
        break;
      case PJ_PRIMITIVE_TYPE_FLOAT32:
        out.numeric = static_cast<double>(v.data.as_float32);
        break;
      case PJ_PRIMITIVE_TYPE_INT8:
        out.numeric = static_cast<double>(v.data.as_int8);
        break;
      case PJ_PRIMITIVE_TYPE_INT16:
        out.numeric = static_cast<double>(v.data.as_int16);
        break;
      case PJ_PRIMITIVE_TYPE_INT32:
        out.numeric = static_cast<double>(v.data.as_int32);
        break;
      case PJ_PRIMITIVE_TYPE_INT64:
        out.numeric = static_cast<double>(v.data.as_int64);
        break;
      case PJ_PRIMITIVE_TYPE_UINT8:
        out.numeric = static_cast<double>(v.data.as_uint8);
        break;
      case PJ_PRIMITIVE_TYPE_UINT16:
        out.numeric = static_cast<double>(v.data.as_uint16);
        break;
      case PJ_PRIMITIVE_TYPE_UINT32:
        out.numeric = static_cast<double>(v.data.as_uint32);
        break;
      case PJ_PRIMITIVE_TYPE_UINT64:
        out.numeric = static_cast<double>(v.data.as_uint64);
        break;
      case PJ_PRIMITIVE_TYPE_BOOL:
        out.bool_value = (v.data.as_bool != 0);
        out.numeric = out.bool_value ? 1.0 : 0.0;
        break;
      case PJ_PRIMITIVE_TYPE_STRING:
        if (v.data.as_string.data != nullptr) {
          out.string_value.assign(v.data.as_string.data, v.data.as_string.size);
        }
        break;
      default:
        break;
    }
  }

  // ------------------------------------------------------------------
  // Arrow release callbacks.
  // ------------------------------------------------------------------

  static void releaseArrowSchema(ArrowSchema* schema) noexcept {
    auto* p = static_cast<ArrowSchemaPayload*>(schema->private_data);
    delete p;
    schema->release = nullptr;
    schema->private_data = nullptr;
    schema->children = nullptr;
    schema->n_children = 0;
  }

  static void releaseArrowArray(ArrowArray* array) noexcept {
    auto* p = static_cast<ArrowArrayPayload*>(array->private_data);
    delete p;
    array->release = nullptr;
    array->private_data = nullptr;
    array->children = nullptr;
    array->n_children = 0;
  }

  static void buildArrowSeries(
      const std::vector<int64_t>& ts, const std::vector<double>& vals, ArrowSchema* out_schema, ArrowArray* out_array) {
    auto* sp = new ArrowSchemaPayload{};
    sp->child_ts = ArrowSchema{
        .format = "l",
        .name = "timestamp",
        .metadata = nullptr,
        .flags = 0,
        .n_children = 0,
        .children = nullptr,
        .dictionary = nullptr,
        .release = [](ArrowSchema* s) noexcept { s->release = nullptr; },
        .private_data = nullptr};
    sp->child_val = ArrowSchema{
        .format = "g",
        .name = "value",
        .metadata = nullptr,
        .flags = 0,
        .n_children = 0,
        .children = nullptr,
        .dictionary = nullptr,
        .release = [](ArrowSchema* s) noexcept { s->release = nullptr; },
        .private_data = nullptr};
    sp->child_ptrs[0] = &sp->child_ts;
    sp->child_ptrs[1] = &sp->child_val;

    *out_schema = ArrowSchema{
        .format = "+s",
        .name = "",
        .metadata = nullptr,
        .flags = 0,
        .n_children = 2,
        .children = sp->child_ptrs,
        .dictionary = nullptr,
        .release = releaseArrowSchema,
        .private_data = sp};

    auto* ap = new ArrowArrayPayload{};
    ap->timestamps = ts;
    ap->values = vals;
    ap->ts_buffers[0] = nullptr;
    ap->ts_buffers[1] = ap->timestamps.data();
    ap->val_buffers[0] = nullptr;
    ap->val_buffers[1] = ap->values.data();

    const int64_t length = static_cast<int64_t>(ap->values.size());
    ap->child_ts = ArrowArray{
        .length = length,
        .null_count = 0,
        .offset = 0,
        .n_buffers = 2,
        .n_children = 0,
        .buffers = ap->ts_buffers,
        .children = nullptr,
        .dictionary = nullptr,
        .release = [](ArrowArray* a) noexcept { a->release = nullptr; },
        .private_data = nullptr};
    ap->child_val = ArrowArray{
        .length = length,
        .null_count = 0,
        .offset = 0,
        .n_buffers = 2,
        .n_children = 0,
        .buffers = ap->val_buffers,
        .children = nullptr,
        .dictionary = nullptr,
        .release = [](ArrowArray* a) noexcept { a->release = nullptr; },
        .private_data = nullptr};
    ap->child_ptrs[0] = &ap->child_ts;
    ap->child_ptrs[1] = &ap->child_val;

    *out_array = ArrowArray{
        .length = length,
        .null_count = 0,
        .offset = 0,
        .n_buffers = 0,
        .n_children = 2,
        .buffers = nullptr,
        .children = ap->child_ptrs,
        .dictionary = nullptr,
        .release = releaseArrowArray,
        .private_data = ap};
  }

  // ------------------------------------------------------------------
  // Toolbox host vtable trampolines.
  // ------------------------------------------------------------------

  static bool trampolineCreateDataSource(
      void* ctx, PJ_string_view_t, PJ_data_source_handle_t* out, PJ_error_t*) noexcept {
    auto* self = static_cast<ToolboxTestStore*>(ctx);
    ++self->create_data_source_calls_;
    *out = PJ_data_source_handle_t{1};
    return true;
  }

  static bool trampolineEnsureTopic(
      void*, PJ_data_source_handle_t, PJ_string_view_t, PJ_topic_handle_t* out, PJ_error_t*) noexcept {
    *out = PJ_topic_handle_t{100};
    return true;
  }

  static bool trampolineEnsureField(
      void*, PJ_topic_handle_t, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out, PJ_error_t*) noexcept {
    *out = PJ_field_handle_t{PJ_topic_handle_t{100}, 1};
    return true;
  }

  static bool trampolineAppendRecord(
      void* ctx, PJ_topic_handle_t, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count,
      PJ_error_t*) noexcept {
    auto* self = static_cast<ToolboxTestStore*>(ctx);
    RecordedRow row;
    row.timestamp = timestamp;
    row.fields.reserve(field_count);
    for (size_t i = 0; i < field_count; ++i) {
      RecordedField f;
      if (fields[i].name.data != nullptr) {
        f.name.assign(fields[i].name.data, fields[i].name.size);
      }
      f.is_null = fields[i].is_null;
      extractValue(fields[i].value, f);
      row.fields.push_back(std::move(f));
    }
    self->written_.push_back(std::move(row));
    return true;
  }

  static bool trampolineAppendBoundRecord(
      void*, PJ_topic_handle_t, int64_t, const PJ_bound_field_value_t*, size_t, PJ_error_t*) noexcept {
    // Bound writes are currently not captured — toolboxes that need them
    // can extend this later. Returning true keeps the write path happy.
    return true;
  }

  static bool trampolineAppendArrowStream(
      void*, PJ_topic_handle_t, struct ArrowArrayStream* stream, PJ_string_view_t, PJ_error_t*) noexcept {
    // Accept and immediately release any stream handed in. Toolboxes that
    // want to assert on batch contents should use appendRecord paths, or
    // extend this helper.
    if (stream != nullptr && stream->release != nullptr) {
      stream->release(stream);
    }
    return true;
  }

  static bool trampolineAcquireCatalogSnapshot(void* ctx, PJ_catalog_snapshot_t* out, PJ_error_t*) noexcept {
    auto* self = static_cast<ToolboxTestStore*>(ctx);

    auto* field_infos = new PJ_field_info_t[self->fields_.size()];
    for (size_t i = 0; i < self->fields_.size(); ++i) {
      field_infos[i].handle = self->fields_[i].handle;
      field_infos[i].name = PJ_string_view_t{self->fields_[i].name.data(), self->fields_[i].name.size()};
      field_infos[i].type = PJ_PRIMITIVE_TYPE_FLOAT64;
    }

    auto* topic_infos = new PJ_topic_info_t[self->topics_.size()];
    for (size_t i = 0; i < self->topics_.size(); ++i) {
      topic_infos[i].handle = PJ_topic_handle_t{self->topics_[i].topic_id};
      topic_infos[i].source = PJ_data_source_handle_t{1};
      topic_infos[i].name = PJ_string_view_t{self->topics_[i].name.data(), self->topics_[i].name.size()};
      topic_infos[i].first_field = self->topics_[i].first_field;
      topic_infos[i].field_count = self->topics_[i].field_count;
    }

    out->data_sources = nullptr;
    out->data_source_count = 0;
    out->topics = topic_infos;
    out->topic_count = self->topics_.size();
    out->fields = field_infos;
    out->field_count = self->fields_.size();

    auto* rel = new CatalogRelease{topic_infos, field_infos};
    out->release_ctx = rel;
    out->release = [](void* p) {
      auto* r = static_cast<CatalogRelease*>(p);
      delete[] r->topics;
      delete[] r->fields;
      delete r;
    };
    return true;
  }

  static bool trampolineReadSeriesArrow(
      void* ctx, PJ_field_handle_t h, ArrowSchema* out_schema, ArrowArray* out_array, PJ_error_t*) noexcept {
    auto* self = static_cast<ToolboxTestStore*>(ctx);
    const FieldEntry* entry = self->findField(h);
    if (entry == nullptr || entry->values.empty()) {
      return false;
    }
    buildArrowSeries(entry->timestamps, entry->values, out_schema, out_array);
    return true;
  }

  // ------------------------------------------------------------------
  // Runtime host vtable trampolines.
  // ------------------------------------------------------------------

  static void trampolineReportMessage(void*, PJ_toolbox_message_level_t, PJ_string_view_t) noexcept {}

  static void trampolineNotifyDataChanged(void* ctx) noexcept {
    auto* self = static_cast<ToolboxTestStore*>(ctx);
    ++self->notify_data_changed_calls_;
  }

  // ------------------------------------------------------------------
  // State.
  // ------------------------------------------------------------------

  std::vector<TopicEntry> topics_;
  std::vector<FieldEntry> fields_;
  uint32_t next_topic_id_ = 1;
  uint32_t next_field_id_ = 1;

  std::vector<RecordedRow> written_;
  int create_data_source_calls_ = 0;
  int notify_data_changed_calls_ = 0;
};

}  // namespace PJ::testing
