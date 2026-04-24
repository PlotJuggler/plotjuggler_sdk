/**
 * @file parser_write_recorder.hpp
 * @brief Test helper: a PJ_parser_write_host_t that captures written rows.
 *
 * Every parser test (json, protobuf, ros, data_tamer, the core mock JSON
 * parser test) used to define its own ~60 line `ParserWriteRecorder` struct
 * with three identical vtable trampolines + a `makeWriteHost()` factory. This
 * header lifts that boilerplate into the SDK so parser authors can
 * concentrate on their decoder instead of Arrow-ABI glue.
 *
 * Usage sketch:
 *
 *   PJ::sdk::testing::ParserWriteRecorder recorder;
 *   PJ::ServiceRegistryBuilder registry;
 *   registry.registerService<PJ::sdk::ParserWriteHostService>(recorder.makeHost());
 *   ASSERT_TRUE(handle.bind(registry.view()));
 *
 *   // ... run parser ...
 *
 *   ASSERT_EQ(recorder.rows().size(), 1u);
 *   EXPECT_EQ(recorder.rows()[0].timestamp, 1000);
 *   EXPECT_EQ(recorder.rows()[0].fields[0].name, "temperature");
 *   EXPECT_DOUBLE_EQ(recorder.rows()[0].fields[0].numeric, 23.5);
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pj_base/plugin_data_api.h"
#include "pj_base/types.hpp"

namespace PJ::sdk::testing {

/// One field inside a recorded row.
///
/// The three value slots (`numeric`, `bool_value`, `string_value`) are
/// populated based on `type`. For numeric types (all signed/unsigned int
/// widths + float32/float64) the value is converted to `double` so assertions
/// can use `EXPECT_DOUBLE_EQ` uniformly. `bool` goes into `bool_value`
/// (boolean types don't round-trip losslessly through `double`). String
/// values are copied into `string_value` so they outlive the parse call.
struct RecordedField {
  std::string name;
  PrimitiveType type = PrimitiveType::kUnspecified;
  bool is_null = false;

  double numeric = 0.0;      // numeric types (int{8,16,32,64}, uint{8,16,32,64}, float{32,64})
  bool bool_value = false;   // bool
  std::string string_value;  // string
};

struct RecordedRow {
  int64_t timestamp = 0;
  std::vector<RecordedField> fields;
};

/// Captures every `append_record` / `append_bound_record` call into an
/// in-memory vector of `RecordedRow`s. Thread-unsafe — intended for single-
/// threaded parser unit tests.
class ParserWriteRecorder {
 public:
  ParserWriteRecorder() = default;

  ParserWriteRecorder(const ParserWriteRecorder&) = delete;
  ParserWriteRecorder& operator=(const ParserWriteRecorder&) = delete;
  ParserWriteRecorder(ParserWriteRecorder&&) = delete;
  ParserWriteRecorder& operator=(ParserWriteRecorder&&) = delete;

  /// Build a PJ_parser_write_host_t whose context points at *this*. The
  /// recorder must outlive the host handle.
  [[nodiscard]] PJ_parser_write_host_t makeHost() noexcept {
    static const PJ_parser_write_host_vtable_t vtable = {
        .abi_version = PJ_PLUGIN_DATA_API_VERSION,
        .struct_size = sizeof(PJ_parser_write_host_vtable_t),
        .ensure_field = &ParserWriteRecorder::trampolineEnsureField,
        .append_record = &ParserWriteRecorder::trampolineAppendRecord,
        .append_bound_record = &ParserWriteRecorder::trampolineAppendBoundRecord,
    };
    return PJ_parser_write_host_t{.ctx = this, .vtable = &vtable};
  }

  [[nodiscard]] const std::vector<RecordedRow>& rows() const noexcept {
    return rows_;
  }

  [[nodiscard]] std::vector<RecordedRow>& rows() noexcept {
    return rows_;
  }

  void clear() noexcept {
    rows_.clear();
    field_names_.clear();
    next_field_id_ = 0;
  }

  /// Helper: look up a field by name inside a specific row (returns nullptr
  /// if the field isn't present).
  [[nodiscard]] static const RecordedField* findField(const RecordedRow& row, std::string_view name) noexcept {
    for (const auto& f : row.fields) {
      if (f.name == name) {
        return &f;
      }
    }
    return nullptr;
  }

 private:
  std::vector<RecordedRow> rows_;
  std::unordered_map<uint32_t, std::string> field_names_;
  uint32_t next_field_id_ = 0;

  static void extractValue(const PJ_scalar_value_t& v, RecordedField& out) noexcept {
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
        // Also populate `numeric` (1.0 / 0.0) so bool columns can be
        // asserted the same way as other numeric types. Matches the shape
        // many pre-existing parser tests already use.
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

  static bool trampolineEnsureField(
      void* ctx, PJ_string_view_t name, PJ_primitive_type_t, PJ_field_handle_t* out_field, PJ_error_t*) noexcept {
    auto* self = static_cast<ParserWriteRecorder*>(ctx);
    uint32_t id = self->next_field_id_++;
    self->field_names_.emplace(id, std::string(name.data == nullptr ? "" : name.data, name.size));
    *out_field = PJ_field_handle_t{PJ_topic_handle_t{1}, id};
    return true;
  }

  static bool trampolineAppendRecord(
      void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count, PJ_error_t*) noexcept {
    auto* self = static_cast<ParserWriteRecorder*>(ctx);
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
    self->rows_.push_back(std::move(row));
    return true;
  }

  static bool trampolineAppendBoundRecord(
      void* ctx, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count, PJ_error_t*) noexcept {
    auto* self = static_cast<ParserWriteRecorder*>(ctx);
    RecordedRow row;
    row.timestamp = timestamp;
    row.fields.reserve(field_count);
    for (size_t i = 0; i < field_count; ++i) {
      RecordedField f;
      auto it = self->field_names_.find(fields[i].field.id);
      f.name = (it != self->field_names_.end()) ? it->second : std::string{"<unknown>"};
      f.is_null = fields[i].is_null;
      extractValue(fields[i].value, f);
      row.fields.push_back(std::move(f));
    }
    self->rows_.push_back(std::move(row));
    return true;
  }
};

}  // namespace PJ::sdk::testing
