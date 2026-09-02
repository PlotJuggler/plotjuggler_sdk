// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/parser_module_abi.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "pj_base/builtin_object_abi.h"

namespace {

PJ::Span<const uint8_t> bytesOf(std::string_view value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}

bool spansEqual(PJ::Span<const uint8_t> lhs, PJ::Span<const uint8_t> rhs) {
  return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

const std::vector<uint8_t> kBindingInfoGolden{
    0x01, 0x00, 0x02, 0x00,                          // version, object route
    0x04, 0x03, 0x02, 0x01,                          // claim index
    0x03, 0x00, 0x00, 0x00,                          // expected object type, reserved
    0x06, 0x00, 0x00, 0x00,                          // field count
    0x40, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,  // encoding
    0x43, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,  // type name
    0x44, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,  // schema
    0x46, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,  // claim id
    0x47, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,  // config JSON
    0x49, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // schema digest
    0x63, 0x64, 0x72, 0x54, 0x00, 0xFF, 0x63, 0x7B, 0x7D,
};

const std::vector<uint8_t> kParseInputGolden{
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // flags, padding
    0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // timestamp -2
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // payload length
    0xAA, 0x00, 0xFF,
};

const std::vector<uint8_t> kObjectOutputGolden{
    0x01, 0x00, 0x02, 0x00,                          // version, object route, reserved
    0x03, 0x00, 0x01, 0x00,                          // object type, splice count
    0x09, 0x00, 0x00, 0x00,                          // splice field number
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // splice offset
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // splice length
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // wire length
    0x08, 0x01,
};

const std::vector<uint8_t> kScalarOutputGolden{
    0x01, 0x00, 0x01, 0x00,                                // version, scalar route, reserved
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,        // timestamp flag, padding
    0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,        // timestamp -2
    0x05, 0x00, 0x00, 0x00,                                // field count
    0x64, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,  // name a, f64
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x3F,        // 1.5
    0x65, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,  // name b, i64
    0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,        // -2
    0x66, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,  // name c, u64
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x67, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03,                    // name d, bool
    0x01, 0x68, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04,  // name e, string
    0x02, 0x00, 0x00, 0x00, 0x68, 0x69,                          // "hi"
    0x61, 0x62, 0x63, 0x64, 0x65,                                // field names
};

TEST(ParserModuleAbi, BindingInfoV1MatchesGoldenAndRoundTrips) {
  const std::array<uint8_t, 2> schema{0x00, 0xFF};
  const PJ::parser_module::BindingInfoV1 input{
      .route = PJ::parser_module::Route::kObject,
      .claim_index = 0x01020304,
      .expected_object_type = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
      .encoding = bytesOf("cdr"),
      .type_name = bytesOf("T"),
      .schema = schema,
      .claim_id = bytesOf("c"),
      .config_json = bytesOf("{}"),
      .schema_digest = {},
  };

  const auto written = PJ::parser_module::writeBindingInfoV1(input);
  ASSERT_TRUE(written) << written.error();
  EXPECT_EQ(*written, kBindingInfoGolden);

  const auto read = PJ::parser_module::readBindingInfoV1(kBindingInfoGolden);
  ASSERT_TRUE(read) << read.error();
  EXPECT_EQ(read->route, PJ::parser_module::Route::kObject);
  EXPECT_EQ(read->claim_index, 0x01020304U);
  EXPECT_EQ(read->expected_object_type, PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD);
  EXPECT_TRUE(spansEqual(bytesOf("cdr"), read->encoding));
  EXPECT_TRUE(spansEqual(bytesOf("T"), read->type_name));
  EXPECT_TRUE(spansEqual(PJ::Span<const uint8_t>(schema), read->schema));
  EXPECT_TRUE(spansEqual(bytesOf("c"), read->claim_id));
  EXPECT_TRUE(spansEqual(bytesOf("{}"), read->config_json));
  EXPECT_TRUE(read->schema_digest.empty());
}

TEST(ParserModuleAbi, BindingInfoReaderIgnoresValidatedTrailingFields) {
  std::vector<uint8_t> with_extra = kBindingInfoGolden;
  with_extra[12] = 7;
  for (size_t index = 0; index < 6; ++index) {
    with_extra[16 + index * 8] = static_cast<uint8_t>(with_extra[16 + index * 8] + 8);
  }
  with_extra.insert(with_extra.begin() + 64, {0x51, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00});
  with_extra.push_back(0x78);

  const auto read = PJ::parser_module::readBindingInfoV1(with_extra);
  ASSERT_TRUE(read) << read.error();
  EXPECT_TRUE(spansEqual(bytesOf("cdr"), read->encoding));
  EXPECT_TRUE(spansEqual(bytesOf("{}"), read->config_json));
}

TEST(ParserModuleAbi, BindingInfoReaderRejectsTruncationAndMalformedRanges) {
  for (size_t size = 0; size < kBindingInfoGolden.size(); ++size) {
    EXPECT_FALSE(PJ::parser_module::readBindingInfoV1({kBindingInfoGolden.data(), size})) << "prefix size " << size;
  }

  auto malformed = kBindingInfoGolden;
  malformed[16] = 0xFF;
  EXPECT_FALSE(PJ::parser_module::readBindingInfoV1(malformed));
  malformed = kBindingInfoGolden;
  malformed[12] = 5;
  EXPECT_FALSE(PJ::parser_module::readBindingInfoV1(malformed));
  malformed = kBindingInfoGolden;
  malformed[0] = 2;
  EXPECT_FALSE(PJ::parser_module::readBindingInfoV1(malformed));
}

TEST(ParserModuleAbi, ParseInputV1MatchesGoldenAndRoundTrips) {
  const std::array<uint8_t, 3> payload{0xAA, 0x00, 0xFF};
  const auto written = PJ::parser_module::writeParseInputV1(
      PJ::parser_module::ParseInputV1{.has_timestamp = true, .timestamp_ns = -2, .payload = payload});
  ASSERT_TRUE(written) << written.error();
  EXPECT_EQ(*written, kParseInputGolden);

  const auto read = PJ::parser_module::readParseInputV1(kParseInputGolden);
  ASSERT_TRUE(read) << read.error();
  EXPECT_TRUE(read->has_timestamp);
  EXPECT_EQ(read->timestamp_ns, -2);
  EXPECT_TRUE(spansEqual(PJ::Span<const uint8_t>(payload), read->payload));
}

TEST(ParserModuleAbi, ParseInputReaderRejectsTruncationFlagsAndLengthMismatch) {
  for (size_t size = 0; size < kParseInputGolden.size(); ++size) {
    EXPECT_FALSE(PJ::parser_module::readParseInputV1({kParseInputGolden.data(), size})) << "prefix size " << size;
  }
  auto malformed = kParseInputGolden;
  malformed[0] = 2;
  EXPECT_FALSE(PJ::parser_module::readParseInputV1(malformed));
  malformed = kParseInputGolden;
  malformed[16] = 2;
  EXPECT_FALSE(PJ::parser_module::readParseInputV1(malformed));
  malformed.push_back(0);
  EXPECT_FALSE(PJ::parser_module::readParseInputV1(malformed));
}

TEST(ParserModuleAbi, ObjectOutputV1MatchesGoldenAndRoundTrips) {
  const std::array<uint8_t, 2> wire{0x08, 0x01};
  const PJ::parser_module::OutputDescriptorV1 output = PJ::parser_module::ObjectOutputV1{
      .object_type = PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD,
      .splice = PJ::parser_module::ObjectSpliceV1{.field_number = 9, .input_offset = 2, .input_length = 3},
      .wire = wire,
  };
  const auto written = PJ::parser_module::writeOutputDescriptorV1(output);
  ASSERT_TRUE(written) << written.error();
  EXPECT_EQ(*written, kObjectOutputGolden);

  const auto read = PJ::parser_module::readOutputDescriptorV1(kObjectOutputGolden);
  ASSERT_TRUE(read) << read.error();
  const auto* object = std::get_if<PJ::parser_module::ObjectOutputV1>(&*read);
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(object->object_type, PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD);
  ASSERT_TRUE(object->splice.has_value());
  EXPECT_EQ(object->splice->field_number, 9U);
  EXPECT_EQ(object->splice->input_offset, 2U);
  EXPECT_EQ(object->splice->input_length, 3U);
  EXPECT_TRUE(spansEqual(PJ::Span<const uint8_t>(wire), object->wire));
}

TEST(ParserModuleAbi, ScalarOutputV1MatchesGoldenAndRoundTripsEveryValueKind) {
  PJ::parser_module::ScalarOutputV1 scalar{
      .has_timestamp = true,
      .timestamp_ns = -2,
      .fields = {},
  };
  scalar.fields = {
      {.name = "a", .value = 1.5},
      {.name = "b", .value = int64_t{-2}},
      {.name = "c", .value = UINT64_C(0x0102030405060708)},
      {.name = "d", .value = true},
      {.name = "e", .value = std::string_view("hi")},
  };
  const PJ::parser_module::OutputDescriptorV1 output = scalar;
  const auto written = PJ::parser_module::writeOutputDescriptorV1(output);
  ASSERT_TRUE(written) << written.error();
  EXPECT_EQ(*written, kScalarOutputGolden);

  const auto read = PJ::parser_module::readOutputDescriptorV1(kScalarOutputGolden);
  ASSERT_TRUE(read) << read.error();
  const auto* decoded = std::get_if<PJ::parser_module::ScalarOutputV1>(&*read);
  ASSERT_NE(decoded, nullptr);
  EXPECT_TRUE(decoded->has_timestamp);
  EXPECT_EQ(decoded->timestamp_ns, -2);
  ASSERT_EQ(decoded->fields.size(), 5U);
  EXPECT_EQ(decoded->fields[0].name, "a");
  EXPECT_DOUBLE_EQ(std::get<double>(decoded->fields[0].value), 1.5);
  EXPECT_EQ(std::get<int64_t>(decoded->fields[1].value), -2);
  EXPECT_EQ(std::get<uint64_t>(decoded->fields[2].value), UINT64_C(0x0102030405060708));
  EXPECT_TRUE(std::get<bool>(decoded->fields[3].value));
  EXPECT_EQ(std::get<std::string_view>(decoded->fields[4].value), "hi");
}

TEST(ParserModuleAbi, OutputReaderRejectsTruncationAndMalformedDescriptors) {
  for (size_t size = 0; size < kObjectOutputGolden.size(); ++size) {
    EXPECT_FALSE(PJ::parser_module::readOutputDescriptorV1({kObjectOutputGolden.data(), size}))
        << "object prefix size " << size;
  }
  for (size_t size = 0; size < kScalarOutputGolden.size(); ++size) {
    EXPECT_FALSE(PJ::parser_module::readOutputDescriptorV1({kScalarOutputGolden.data(), size}))
        << "scalar prefix size " << size;
  }

  auto malformed = kObjectOutputGolden;
  malformed[6] = 2;
  EXPECT_FALSE(PJ::parser_module::readOutputDescriptorV1(malformed));
  malformed = kObjectOutputGolden;
  malformed[2] = 3;
  EXPECT_FALSE(PJ::parser_module::readOutputDescriptorV1(malformed));
  auto malformed_scalar = kScalarOutputGolden;
  malformed_scalar[32] = 9;
  EXPECT_FALSE(PJ::parser_module::readOutputDescriptorV1(malformed_scalar));
  malformed_scalar = kScalarOutputGolden;
  malformed_scalar[24] = 0xFF;
  EXPECT_FALSE(PJ::parser_module::readOutputDescriptorV1(malformed_scalar));
}

TEST(BuiltinObjectSpliceTable, ContainsOnlyUnambiguousTopLevelBulkByteFields) {
  const std::array<PJ_builtin_object_splice_field_v1_t, 10> expected{
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_IMAGE, 0, 7},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD, 0, 9},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_DEPTH_IMAGE, 0, 5},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_OCCUPANCY_GRID, 0, 7},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_COMPRESSED_POINTCLOUD, 0, 4},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_MESH3D, 0, 7},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_VIDEO_FRAME, 0, 3},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_OCCUPANCY_GRID_UPDATE, 0, 7},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_VOXEL_GRID, 0, 12},
      PJ_builtin_object_splice_field_v1_t{PJ_BUILTIN_OBJECT_TYPE_GRID_MAP, 0, 10},
  };
  uint32_t count = 0;
  const auto* table = pj_builtin_object_splice_fields_v1(&count);
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(count, expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(table[index].object_type, expected[index].object_type);
    EXPECT_EQ(table[index].reserved, 0);
    EXPECT_EQ(table[index].field_number, expected[index].field_number);
    uint32_t field_number = 0;
    EXPECT_TRUE(pj_builtin_object_splice_field_number_v1(expected[index].object_type, &field_number));
    EXPECT_EQ(field_number, expected[index].field_number);
  }

  for (const uint16_t absent : {
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_NONE},
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_IMAGE_ANNOTATIONS},
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_FRAME_TRANSFORMS},
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_SCENE_ENTITIES},
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_ROBOT_DESCRIPTION},
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_CAMERA_INFO},
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_LOG},
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_POSES_IN_FRAME},
           uint16_t{PJ_BUILTIN_OBJECT_TYPE_PLOT_MARKERS},
       }) {
    uint32_t field_number = 99;
    EXPECT_FALSE(pj_builtin_object_splice_field_number_v1(absent, &field_number));
    EXPECT_EQ(field_number, 99U);
  }
  EXPECT_FALSE(pj_builtin_object_splice_field_number_v1(PJ_BUILTIN_OBJECT_TYPE_IMAGE, nullptr));
}

}  // namespace
