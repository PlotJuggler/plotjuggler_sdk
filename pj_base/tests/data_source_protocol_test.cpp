#include "pj_base/data_source_protocol.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace {

static_assert(std::is_standard_layout_v<PJ_parser_binding_handle_t>);
static_assert(std::is_standard_layout_v<PJ_parser_binding_request_t>);
static_assert(std::is_standard_layout_v<PJ_data_source_runtime_host_t>);
static_assert(std::is_standard_layout_v<PJ_data_source_vtable_t>);

TEST(DataSourceProtocolTest, CapabilityFlagsAreDistinctBits) {
  EXPECT_NE(PJ_DATA_SOURCE_CAPABILITY_FINITE_IMPORT, PJ_DATA_SOURCE_CAPABILITY_CONTINUOUS_STREAM);
  EXPECT_NE(PJ_DATA_SOURCE_CAPABILITY_DIRECT_INGEST, PJ_DATA_SOURCE_CAPABILITY_DELEGATED_INGEST);
  EXPECT_NE(PJ_DATA_SOURCE_CAPABILITY_SUPPORTS_PAUSE, PJ_DATA_SOURCE_CAPABILITY_HAS_DIALOG);
}

TEST(DataSourceProtocolTest, StateEnumValuesAreStable) {
  EXPECT_EQ(PJ_DATA_SOURCE_STATE_IDLE, 0);
  EXPECT_EQ(PJ_DATA_SOURCE_STATE_RUNNING, 3);
  EXPECT_EQ(PJ_DATA_SOURCE_STATE_FAILED, 7);
}

TEST(DataSourceProtocolTest, RuntimeHostCanBeValueInitialized) {
  PJ_data_source_runtime_host_t host{};
  EXPECT_EQ(host.ctx, nullptr);
  EXPECT_EQ(host.vtable, nullptr);
}

TEST(DataSourceProtocolTest, ParserBindingRequestStoresViewsWithoutOwnership) {
  const char topic[] = "topic";
  const char encoding[] = "json";
  const uint8_t schema[] = {1, 2, 3};

  PJ_parser_binding_request_t request{
      .topic_name = {topic, 5},
      .parser_encoding = {encoding, 4},
      .type_name = {nullptr, 0},
      .schema = {schema, 3},
      .parser_config_json = {nullptr, 0},
  };

  EXPECT_EQ(request.topic_name.size, 5U);
  EXPECT_EQ(request.parser_encoding.size, 4U);
  EXPECT_EQ(request.schema.size, 3U);
}

TEST(DataSourceProtocolTest, RuntimeHostVtableHasIsStopRequested) {
  PJ_data_source_runtime_host_vtable_t vtable{};
  vtable.is_stop_requested = [](void*) noexcept -> bool { return true; };
  EXPECT_TRUE(vtable.is_stop_requested(nullptr));
}

}  // namespace
