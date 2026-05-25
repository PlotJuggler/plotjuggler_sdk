// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/plugin_data_api.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "pj_base/sdk/plugin_data_api.hpp"

namespace PJ {
namespace {

static_assert(std::is_standard_layout_v<PJ_data_source_handle_t>);
static_assert(std::is_standard_layout_v<PJ_topic_handle_t>);
static_assert(std::is_standard_layout_v<PJ_field_handle_t>);
static_assert(std::is_standard_layout_v<PJ_scalar_value_t>);
static_assert(std::is_standard_layout_v<PJ_source_write_host_t>);
static_assert(std::is_standard_layout_v<PJ_parser_write_host_t>);
static_assert(std::is_standard_layout_v<PJ_toolbox_host_t>);
static_assert(
    offsetof(PJ_parser_write_host_vtable_t, append_arrow_stream) ==
        offsetof(PJ_parser_write_host_vtable_t, append_bound_record) +
            sizeof(static_cast<PJ_parser_write_host_vtable_t*>(nullptr)->append_bound_record),
    "Parser append_arrow_stream must stay appended after the v4.0 baseline");
static_assert(
    sizeof(PJ_parser_write_host_vtable_t) ==
        offsetof(PJ_parser_write_host_vtable_t, append_arrow_stream) +
            sizeof(static_cast<PJ_parser_write_host_vtable_t*>(nullptr)->append_arrow_stream),
    "Parser write host size should only grow by tail appends");

struct TailSlotRecorder {
  bool called = false;
};

bool sourceAppendArrowStream(
    void* ctx, PJ_topic_handle_t, struct ArrowArrayStream*, PJ_string_view_t, PJ_error_t*) noexcept {
  static_cast<TailSlotRecorder*>(ctx)->called = true;
  return true;
}

bool parserEnsureField(
    void*, PJ_string_view_t, PJ_primitive_type_t, PJ_field_handle_t* out_field, PJ_error_t*) noexcept {
  *out_field = PJ_field_handle_t{{1}, 2};
  return true;
}

bool parserAppendRecord(void*, int64_t, const PJ_named_field_value_t*, std::size_t, PJ_error_t*) noexcept {
  return true;
}

bool parserAppendBoundRecord(void* ctx, int64_t, const PJ_bound_field_value_t*, std::size_t, PJ_error_t*) noexcept {
  static_cast<TailSlotRecorder*>(ctx)->called = true;
  return true;
}

bool parserAppendArrowStream(void* ctx, struct ArrowArrayStream*, PJ_string_view_t, PJ_error_t*) noexcept {
  static_cast<TailSlotRecorder*>(ctx)->called = true;
  return true;
}

bool toolboxAppendArrowStream(
    void* ctx, PJ_topic_handle_t, struct ArrowArrayStream*, PJ_string_view_t, PJ_error_t*) noexcept {
  static_cast<TailSlotRecorder*>(ctx)->called = true;
  return true;
}

bool toolboxAcquireCatalogSnapshot(void* ctx, PJ_catalog_snapshot_t* out_snapshot, PJ_error_t*) noexcept {
  static_cast<TailSlotRecorder*>(ctx)->called = true;
  *out_snapshot = PJ_catalog_snapshot_t{};
  return true;
}

bool toolboxReadSeriesArrow(
    void* ctx, PJ_field_handle_t, struct ArrowSchema*, struct ArrowArray*, PJ_error_t*) noexcept {
  static_cast<TailSlotRecorder*>(ctx)->called = true;
  return true;
}

TEST(PluginDataApiTest, PrimitiveTypeRoundTripsThroughAbiEnum) {
  EXPECT_EQ(sdk::fromAbiType(sdk::toAbiType(PrimitiveType::kFloat32)), PrimitiveType::kFloat32);
  EXPECT_EQ(sdk::fromAbiType(sdk::toAbiType(PrimitiveType::kInt8)), PrimitiveType::kInt8);
  EXPECT_EQ(sdk::fromAbiType(sdk::toAbiType(PrimitiveType::kUint32)), PrimitiveType::kUint32);
  EXPECT_EQ(sdk::fromAbiType(sdk::toAbiType(PrimitiveType::kString)), PrimitiveType::kString);
}

TEST(PluginDataApiTest, ValueRefRetainsExactPrimitiveType) {
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{int8_t{-1}}), PrimitiveType::kInt8);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{uint16_t{5}}), PrimitiveType::kUint16);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{uint32_t{9}}), PrimitiveType::kUint32);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{std::string_view("abc")}), PrimitiveType::kString);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{NullValue{}}), PrimitiveType::kUnspecified);
  EXPECT_EQ(sdk::typeOf(sdk::ValueRef{sdk::TypedNull{PrimitiveType::kFloat64}}), PrimitiveType::kFloat64);
}

TEST(PluginDataApiTest, HandleEqualityAndInequality) {
  // DataSourceHandle
  EXPECT_TRUE(sdk::operator==(sdk::DataSourceHandle{.id = 1}, sdk::DataSourceHandle{.id = 1}));
  EXPECT_TRUE(sdk::operator!=(sdk::DataSourceHandle{.id = 1}, sdk::DataSourceHandle{.id = 2}));

  // TopicHandle
  EXPECT_TRUE(sdk::operator==(sdk::TopicHandle{.id = 5}, sdk::TopicHandle{.id = 5}));
  EXPECT_TRUE(sdk::operator!=(sdk::TopicHandle{.id = 5}, sdk::TopicHandle{.id = 6}));

  // FieldHandle — equal requires both topic and id to match
  const sdk::FieldHandle fh_1_2{.topic = {.id = 1}, .id = 2};
  EXPECT_TRUE(sdk::operator==(fh_1_2, sdk::FieldHandle{.topic = {.id = 1}, .id = 2}));
  EXPECT_TRUE(sdk::operator!=(fh_1_2, sdk::FieldHandle{.topic = {.id = 1}, .id = 3}));
  EXPECT_TRUE(sdk::operator!=(fh_1_2, sdk::FieldHandle{.topic = {.id = 9}, .id = 2}));
}

TEST(PluginDataApiTest, ToStringViewHandlesNullData) {
  // Null data pointer with zero size → empty string
  const PJ_string_view_t null_zero{nullptr, 0};
  EXPECT_EQ(sdk::toStringView(null_zero), "");
  EXPECT_EQ(sdk::toStringView(null_zero).size(), 0U);

  // Null data pointer with non-zero size → still uses "" base but with the given size
  // The SDK does: std::string_view(view.data == nullptr ? "" : view.data, view.size)
  // With nullptr and size=5, it constructs std::string_view("", 5) which has size 5
  // but that reads past the empty string literal — this tests current behavior
  const PJ_string_view_t null_nonzero{nullptr, 5};
  const auto result = sdk::toStringView(null_nonzero);
  // The key thing: it doesn't crash (null guard prevents UB from nullptr dereference)
  EXPECT_NE(result.data(), nullptr);
}

TEST(PluginDataApiTest, SourceWriteHostViewRejectsMissingAppendArrowStreamTailSlot) {
  TailSlotRecorder recorder;
  const PJ_source_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = offsetof(PJ_source_write_host_vtable_t, append_arrow_stream),
      .append_arrow_stream = sourceAppendArrowStream,
  };
  sdk::SourceWriteHostView view(PJ_source_write_host_t{.ctx = &recorder, .vtable = &vtable});

  ArrowArrayStream stream{};
  auto status = view.appendArrowStream(PJ_topic_handle_t{1}, &stream);

  EXPECT_FALSE(status);
  EXPECT_FALSE(recorder.called);
  EXPECT_NE(status.error().find("append_arrow_stream"), std::string::npos);
}

TEST(PluginDataApiTest, ParserWriteHostViewCurrentBaselineStillWorks) {
  TailSlotRecorder recorder;
  const PJ_parser_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_parser_write_host_vtable_t),
      .ensure_field = parserEnsureField,
      .append_record = parserAppendRecord,
      .append_bound_record = parserAppendBoundRecord,
  };
  sdk::ParserWriteHostView view(PJ_parser_write_host_t{.ctx = &recorder, .vtable = &vtable});

  auto status = view.appendBoundRecord(123, {});

  EXPECT_TRUE(status) << status.error();
  EXPECT_TRUE(recorder.called);
}

TEST(PluginDataApiTest, ParserWriteHostViewRejectsMissingAppendArrowStreamTailSlot) {
  TailSlotRecorder recorder;
  const PJ_parser_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = offsetof(PJ_parser_write_host_vtable_t, append_arrow_stream),
      .ensure_field = parserEnsureField,
      .append_record = parserAppendRecord,
      .append_bound_record = parserAppendBoundRecord,
      .append_arrow_stream = parserAppendArrowStream,
  };
  sdk::ParserWriteHostView view(PJ_parser_write_host_t{.ctx = &recorder, .vtable = &vtable});

  ArrowArrayStream stream{};
  auto status = view.appendArrowStream(&stream);

  EXPECT_FALSE(status);
  EXPECT_FALSE(recorder.called);
  EXPECT_NE(status.error().find("append_arrow_stream"), std::string::npos);
}

TEST(PluginDataApiTest, ParserWriteHostViewRoutesAppendArrowStreamTailSlot) {
  TailSlotRecorder recorder;
  const PJ_parser_write_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_parser_write_host_vtable_t),
      .ensure_field = parserEnsureField,
      .append_record = parserAppendRecord,
      .append_bound_record = parserAppendBoundRecord,
      .append_arrow_stream = parserAppendArrowStream,
  };
  sdk::ParserWriteHostView view(PJ_parser_write_host_t{.ctx = &recorder, .vtable = &vtable});

  ArrowArrayStream stream{};
  auto status = view.appendArrowStream(&stream);

  EXPECT_TRUE(status) << status.error();
  EXPECT_TRUE(recorder.called);
}

TEST(PluginDataApiTest, ToolboxHostViewRejectsMissingAppendArrowStreamTailSlot) {
  TailSlotRecorder recorder;
  const PJ_toolbox_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = offsetof(PJ_toolbox_host_vtable_t, append_arrow_stream),
      .append_arrow_stream = toolboxAppendArrowStream,
  };
  sdk::ToolboxHostView view(PJ_toolbox_host_t{.ctx = &recorder, .vtable = &vtable});

  ArrowArrayStream stream{};
  auto status = view.appendArrowStream(PJ_topic_handle_t{1}, &stream);

  EXPECT_FALSE(status);
  EXPECT_FALSE(recorder.called);
  EXPECT_NE(status.error().find("append_arrow_stream"), std::string::npos);
}

TEST(PluginDataApiTest, ToolboxHostViewRejectsMissingCatalogSnapshotTailSlot) {
  TailSlotRecorder recorder;
  const PJ_toolbox_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = offsetof(PJ_toolbox_host_vtable_t, acquire_catalog_snapshot),
      .append_arrow_stream = toolboxAppendArrowStream,
      .acquire_catalog_snapshot = toolboxAcquireCatalogSnapshot,
  };
  sdk::ToolboxHostView view(PJ_toolbox_host_t{.ctx = &recorder, .vtable = &vtable});

  auto snapshot = view.catalogSnapshot();

  EXPECT_FALSE(snapshot);
  EXPECT_FALSE(recorder.called);
  EXPECT_NE(snapshot.error().find("acquire_catalog_snapshot"), std::string::npos);
}

TEST(PluginDataApiTest, ToolboxHostViewRejectsMissingReadSeriesTailSlot) {
  TailSlotRecorder recorder;
  const PJ_toolbox_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = offsetof(PJ_toolbox_host_vtable_t, read_series_arrow),
      .append_arrow_stream = toolboxAppendArrowStream,
      .acquire_catalog_snapshot = toolboxAcquireCatalogSnapshot,
      .read_series_arrow = toolboxReadSeriesArrow,
  };
  sdk::ToolboxHostView view(PJ_toolbox_host_t{.ctx = &recorder, .vtable = &vtable});

  ArrowSchema schema{};
  ArrowArray array{};
  auto status = view.readSeriesArrow(PJ_field_handle_t{{1}, 2}, &schema, &array);

  EXPECT_FALSE(status);
  EXPECT_FALSE(recorder.called);
  EXPECT_NE(status.error().find("read_series_arrow"), std::string::npos);
}

}  // namespace
}  // namespace PJ
