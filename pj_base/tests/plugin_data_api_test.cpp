// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/plugin_data_api.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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

bool parserAppendRecord(void*, int64_t, const PJ_named_field_value_t*, uint64_t, PJ_error_t*) noexcept {
  return true;
}

bool parserAppendBoundRecord(void* ctx, int64_t, const PJ_bound_field_value_t*, uint64_t, PJ_error_t*) noexcept {
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

// --- Catalog snapshot v2 --------------------------------------------------------

// A host serving catalog v2 through the shared host-side writer, with one dataset
// and one object topic. `returned_size`/`count overrides` model a broken host.
struct CatalogV2Host {
  int release_count = 0;
  uint32_t seen_struct_size = 0;
  PJ_data_source_info_t source{.handle = {7}, .name = {"bag", 3}, .first_topic = 0, .topic_count = 1};
  PJ_topic_info_t topic{.handle = {3}, .source = {7}, .name = {"imu", 3}, .first_field = 0, .field_count = 1};
  PJ_field_info_t field{.handle = {{3}, 1}, .name = {"x", 1}, .type = PJ_PRIMITIVE_TYPE_FLOAT64};
  PJ_object_topic_info_t object_topic{
      .handle = {11},
      .source = {7},
      .name = {"lidar/points", 12},
      .builtin_object_type = {"kPointCloud", 11},
      .metadata_json = {"{}", 2},
      .entry_count = 3,
      .time_min_ns = -5,
      .time_max_ns = 9,
      .reserved = {0, 0}};
  void (*tamper)(PJ_catalog_snapshot_v2_t&) = nullptr;  // applied after the store
  void (*tamper_entries)(CatalogV2Host&) = nullptr;     // applied before the store
};

void releaseCatalogV2(void* release_ctx) noexcept {
  static_cast<CatalogV2Host*>(release_ctx)->release_count++;
}

PJ_catalog_snapshot_v2_t filledCatalogV2(CatalogV2Host& host) {
  PJ_catalog_snapshot_v2_t filled{};
  filled.data_sources = &host.source;
  filled.data_source_count = 1;
  filled.topics = &host.topic;
  filled.topic_count = 1;
  filled.fields = &host.field;
  filled.field_count = 1;
  filled.object_topics = &host.object_topic;
  filled.object_topic_count = 1;
  filled.release_ctx = &host;
  filled.release = releaseCatalogV2;
  return filled;
}

bool acquireCatalogV2(void* ctx, PJ_catalog_snapshot_v2_t* out_snapshot, PJ_error_t* out_error) noexcept {
  auto* host = static_cast<CatalogV2Host*>(ctx);
  host->seen_struct_size = out_snapshot->struct_size;
  if (host->tamper_entries != nullptr) {
    host->tamper_entries(*host);
  }
  auto stored = sdk::storeCatalogSnapshotV2(out_snapshot, filledCatalogV2(*host));
  if (!stored) {
    sdk::fillError(out_error, 1, "catalog", stored.error());
    return false;
  }
  if (host->tamper != nullptr) {
    host->tamper(*out_snapshot);
  }
  return true;
}

PJ_toolbox_host_vtable_t catalogV2Vtable() {
  return PJ_toolbox_host_vtable_t{
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_toolbox_host_vtable_t),
      .acquire_catalog_snapshot_v2 = acquireCatalogV2,
  };
}

TEST(CatalogSnapshotV2Test, HasCatalogSnapshotV2ReflectsTheTailSlot) {
  CatalogV2Host host;
  auto vtable = catalogV2Vtable();
  EXPECT_TRUE(sdk::ToolboxHostView(PJ_toolbox_host_t{.ctx = &host, .vtable = &vtable}).hasCatalogSnapshotV2());

  vtable.struct_size = offsetof(PJ_toolbox_host_vtable_t, acquire_catalog_snapshot_v2);  // an older host
  EXPECT_FALSE(sdk::ToolboxHostView(PJ_toolbox_host_t{.ctx = &host, .vtable = &vtable}).hasCatalogSnapshotV2());

  vtable.struct_size = sizeof(PJ_toolbox_host_vtable_t);
  vtable.acquire_catalog_snapshot_v2 = nullptr;
  EXPECT_FALSE(sdk::ToolboxHostView(PJ_toolbox_host_t{.ctx = &host, .vtable = &vtable}).hasCatalogSnapshotV2());

  EXPECT_FALSE(sdk::ToolboxHostView().hasCatalogSnapshotV2());
}

TEST(CatalogSnapshotV2Test, ObjectTopicsRoundTripAndReleaseOnce) {
  CatalogV2Host host;
  const auto vtable = catalogV2Vtable();
  sdk::ToolboxHostView view(PJ_toolbox_host_t{.ctx = &host, .vtable = &vtable});
  {
    auto snapshot = view.catalogSnapshotV2();
    ASSERT_TRUE(snapshot) << snapshot.error();
    EXPECT_EQ(host.seen_struct_size, sizeof(PJ_catalog_snapshot_v2_t)) << "the client announces its own size";
    ASSERT_EQ(snapshot->dataSources().size(), 1u);
    EXPECT_EQ(snapshot->dataSources()[0].handle.id, 7u);
    ASSERT_EQ(snapshot->topics().size(), 1u);
    EXPECT_EQ(sdk::toStringView(snapshot->topics()[0].name), "imu");
    ASSERT_EQ(snapshot->fields().size(), 1u);
    EXPECT_EQ(sdk::toStringView(snapshot->fields()[0].name), "x");
    ASSERT_EQ(snapshot->objectTopics().size(), 1u);
    const auto& topic = snapshot->objectTopics()[0];
    EXPECT_EQ(topic.handle.id, 11u);
    EXPECT_EQ(topic.source.id, 7u);
    EXPECT_EQ(sdk::toStringView(topic.name), "lidar/points");
    EXPECT_EQ(sdk::toStringView(topic.builtin_object_type), "kPointCloud");
    EXPECT_EQ(topic.entry_count, 3u);
    EXPECT_EQ(topic.time_min_ns, -5);
    EXPECT_EQ(topic.time_max_ns, 9);
    EXPECT_EQ(host.release_count, 0);

    auto moved = std::move(*snapshot);
    EXPECT_EQ(moved.objectTopics().size(), 1u);
  }
  EXPECT_EQ(host.release_count, 1);
}

TEST(CatalogSnapshotV2Test, OldHostReportsNotSupportedWithoutCalling) {
  CatalogV2Host host;
  auto vtable = catalogV2Vtable();
  vtable.struct_size = offsetof(PJ_toolbox_host_vtable_t, acquire_catalog_snapshot_v2);
  sdk::ToolboxHostView view(PJ_toolbox_host_t{.ctx = &host, .vtable = &vtable});

  auto snapshot = view.catalogSnapshotV2();
  ASSERT_FALSE(snapshot);
  EXPECT_NE(snapshot.error().find("acquire_catalog_snapshot_v2"), std::string::npos);
  EXPECT_EQ(host.seen_struct_size, 0u);
}

// The host-side writer: a caller struct_size below the minimum is refused without
// touching the caller's storage, and the host's snapshot is released.
TEST(CatalogSnapshotV2Test, StoreRejectsShortAndNullCallerAndReleases) {
  CatalogV2Host host;
  PJ_catalog_snapshot_v2_t out{};
  out.struct_size = PJ_CATALOG_SNAPSHOT_V2_MIN_SIZE - 1;
  auto stored = sdk::storeCatalogSnapshotV2(&out, filledCatalogV2(host));
  ASSERT_FALSE(stored);
  EXPECT_NE(stored.error().find("struct_size"), std::string::npos);
  EXPECT_EQ(out.struct_size, PJ_CATALOG_SNAPSHOT_V2_MIN_SIZE - 1);
  EXPECT_EQ(out.object_topics, nullptr);
  EXPECT_EQ(host.release_count, 1);

  EXPECT_FALSE(sdk::storeCatalogSnapshotV2(nullptr, filledCatalogV2(host)));
  EXPECT_EQ(host.release_count, 2);
}

// An older (or exact-size) client: the host writes exactly the caller's prefix.
TEST(CatalogSnapshotV2Test, StoreWritesOnlyTheCallersPrefix) {
  CatalogV2Host host;
  struct Padded {
    PJ_catalog_snapshot_v2_t snapshot;
    unsigned char guard[16];
  };
  Padded buffer{};
  // Poison every byte past the caller's prefix, fields appended later included.
  auto* bytes = reinterpret_cast<unsigned char*>(&buffer);
  std::memset(bytes + PJ_CATALOG_SNAPSHOT_V2_MIN_SIZE, 0xAB, sizeof(buffer) - PJ_CATALOG_SNAPSHOT_V2_MIN_SIZE);
  buffer.snapshot.struct_size = PJ_CATALOG_SNAPSHOT_V2_MIN_SIZE;
  ASSERT_TRUE(sdk::storeCatalogSnapshotV2(&buffer.snapshot, filledCatalogV2(host)));
  EXPECT_EQ(buffer.snapshot.struct_size, PJ_CATALOG_SNAPSHOT_V2_MIN_SIZE);
  EXPECT_EQ(buffer.snapshot.object_topic_count, 1u);
  for (std::size_t offset = PJ_CATALOG_SNAPSHOT_V2_MIN_SIZE; offset < sizeof(buffer); ++offset) {
    EXPECT_EQ(bytes[offset], 0xAB) << "byte " << offset;
  }
  EXPECT_EQ(host.release_count, 0);
}

// A newer client with a larger struct: the host writes the size it knows and says so;
// the client treats every field past that size as absent.
TEST(CatalogSnapshotV2Test, StoreToLargerCallerWritesTheHostSize) {
  CatalogV2Host host;
  struct Larger {
    PJ_catalog_snapshot_v2_t snapshot;
    uint64_t future_field;
  };
  Larger buffer{};
  buffer.future_field = 0x1234;
  buffer.snapshot.struct_size = sizeof(Larger);
  ASSERT_TRUE(sdk::storeCatalogSnapshotV2(&buffer.snapshot, filledCatalogV2(host)));
  EXPECT_EQ(buffer.snapshot.struct_size, sizeof(PJ_catalog_snapshot_v2_t));
  EXPECT_EQ(buffer.future_field, 0x1234u);
  EXPECT_EQ(buffer.snapshot.release, &releaseCatalogV2);
}

TEST(CatalogSnapshotV2Test, StoreWritesReservedAsZero) {
  CatalogV2Host host;
  auto filled = filledCatalogV2(host);
  filled.reserved = 7;
  PJ_catalog_snapshot_v2_t out{};
  out.struct_size = sizeof(out);
  ASSERT_TRUE(sdk::storeCatalogSnapshotV2(&out, filled));
  EXPECT_EQ(out.reserved, 0u);
}

TEST(CatalogSnapshotV2Test, MoveAssignOverALiveSnapshotReleasesTheOldOneOnce) {
  CatalogV2Host first;
  CatalogV2Host second;
  const auto vtable = catalogV2Vtable();
  auto kept = sdk::ToolboxHostView(PJ_toolbox_host_t{.ctx = &first, .vtable = &vtable}).catalogSnapshotV2();
  auto incoming = sdk::ToolboxHostView(PJ_toolbox_host_t{.ctx = &second, .vtable = &vtable}).catalogSnapshotV2();
  ASSERT_TRUE(kept && incoming);
  *kept = std::move(*incoming);
  EXPECT_EQ(first.release_count, 1);
  EXPECT_EQ(second.release_count, 0);
  *kept = std::move(*kept);  // self-move keeps the snapshot
  EXPECT_EQ(second.release_count, 0);
  kept = sdk::CatalogSnapshotV2{};
  EXPECT_EQ(first.release_count, 1);
  EXPECT_EQ(second.release_count, 1);
}

// A reply too short to cover its own release fields is unusable: the client neither
// reads nor calls them (the host cleans up such a reply itself).
TEST(CatalogSnapshotV2Test, ClientNeverReadsTheReleaseOfAnUndersizedReply) {
  CatalogV2Host host;
  host.tamper = [](PJ_catalog_snapshot_v2_t& raw) { raw.struct_size = 8; };
  const auto vtable = catalogV2Vtable();
  auto snapshot = sdk::ToolboxHostView(PJ_toolbox_host_t{.ctx = &host, .vtable = &vtable}).catalogSnapshotV2();
  ASSERT_FALSE(snapshot);
  EXPECT_NE(snapshot.error().find("undersized"), std::string::npos);
  EXPECT_EQ(host.release_count, 0);
}

// A string view inside an entry is checked like the arrays: NULL only when empty, and a
// size that fits an object (a wasm32 size_t cannot wrap it).
TEST(CatalogSnapshotV2Test, ClientRejectsInvalidEntryStringsAndReleases) {
  constexpr uint64_t kHuge = static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) + 1;
  const std::pair<const char*, void (*)(CatalogV2Host&)> cases[] = {
      {"data source name", [](CatalogV2Host& host) { host.source.name = {nullptr, 3}; }},
      {"topic name", [](CatalogV2Host& host) { host.topic.name = {nullptr, 1}; }},
      {"field name", [](CatalogV2Host& host) { host.field.name.size = kHuge; }},
      {"object topic name", [](CatalogV2Host& host) { host.object_topic.name = {nullptr, 64}; }},
      {"object topic type", [](CatalogV2Host& host) { host.object_topic.builtin_object_type.size = kHuge; }},
      {"object topic metadata", [](CatalogV2Host& host) { host.object_topic.metadata_json = {nullptr, 2}; }},
  };
  for (const auto& [name, tamper] : cases) {
    SCOPED_TRACE(name);
    CatalogV2Host host;
    host.tamper_entries = tamper;
    const auto vtable = catalogV2Vtable();
    auto snapshot = sdk::ToolboxHostView(PJ_toolbox_host_t{.ctx = &host, .vtable = &vtable}).catalogSnapshotV2();
    ASSERT_FALSE(snapshot);
    EXPECT_NE(snapshot.error().find(name), std::string::npos) << snapshot.error();
    EXPECT_EQ(host.release_count, 1);
  }
}

// What the client wrapper refuses from a complete but malformed reply: it releases the
// snapshot and never builds a span over the arrays.
TEST(CatalogSnapshotV2Test, ClientRejectsMalformedHostSnapshotsAndReleases) {
  struct Case {
    const char* name;
    void (*tamper)(PJ_catalog_snapshot_v2_t&);
    const char* error;
  };
  constexpr uint64_t kTooManyObjectTopics =
      static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(PJ_object_topic_info_t) + 1;
  constexpr uint64_t kTooManyFields =
      static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(PJ_field_info_t) + 1;
  const Case cases[] = {
      {"null object topics", [](PJ_catalog_snapshot_v2_t& raw) { raw.object_topics = nullptr; }, "object_topics"},
      {"null topics", [](PJ_catalog_snapshot_v2_t& raw) { raw.topics = nullptr; }, "topics"},
      {"object topic count overflow",
       [](PJ_catalog_snapshot_v2_t& raw) { raw.object_topic_count = kTooManyObjectTopics; }, "object_topics"},
      {"field count overflow",
       [](PJ_catalog_snapshot_v2_t& raw) {
         raw.fields = reinterpret_cast<const PJ_field_info_t*>(raw.object_topics);  // never read
         raw.field_count = kTooManyFields;
       },
       "fields"},
      {"data source count overflow", [](PJ_catalog_snapshot_v2_t& raw) { raw.data_source_count = UINT64_MAX; },
       "data_sources"},
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    CatalogV2Host host;
    host.tamper = test_case.tamper;
    const auto vtable = catalogV2Vtable();
    sdk::ToolboxHostView view(PJ_toolbox_host_t{.ctx = &host, .vtable = &vtable});
    auto snapshot = view.catalogSnapshotV2();
    ASSERT_FALSE(snapshot);
    EXPECT_NE(snapshot.error().find(test_case.error), std::string::npos) << snapshot.error();
    EXPECT_EQ(host.release_count, 1);
  }
}

// A host failure is passed through and leaves nothing to release.
TEST(CatalogSnapshotV2Test, HostRefusalSurfacesItsMessage) {
  const PJ_toolbox_host_vtable_t vtable = {
      .abi_version = PJ_PLUGIN_DATA_API_VERSION,
      .struct_size = sizeof(PJ_toolbox_host_vtable_t),
      .acquire_catalog_snapshot_v2 =
          [](void*, PJ_catalog_snapshot_v2_t*, PJ_error_t* out_error) noexcept {
            sdk::fillError(out_error, 1, "catalog", "catalog busy");
            return false;
          },
  };
  int dummy = 0;
  auto snapshot = sdk::ToolboxHostView(PJ_toolbox_host_t{.ctx = &dummy, .vtable = &vtable}).catalogSnapshotV2();
  ASSERT_FALSE(snapshot);
  EXPECT_NE(snapshot.error().find("catalog busy"), std::string::npos);
}

}  // namespace
}  // namespace PJ
