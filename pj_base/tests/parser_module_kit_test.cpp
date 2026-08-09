// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "pj_base/parser_module/module.hpp"

namespace {

void alignCdr(std::vector<uint8_t>& bytes, size_t alignment) {
  const size_t relative = bytes.size() - 4;
  const size_t padding = (alignment - (relative % alignment)) % alignment;
  bytes.insert(bytes.end(), padding, 0);
}

void cdrU16(std::vector<uint8_t>& bytes, uint16_t value, bool little = true) {
  alignCdr(bytes, 2);
  if (little) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
  } else {
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value));
  }
}

void cdrU32(std::vector<uint8_t>& bytes, uint32_t value, bool little = true) {
  alignCdr(bytes, 4);
  for (size_t index = 0; index < 4; ++index) {
    const size_t shift = little ? index * 8U : (3 - index) * 8U;
    bytes.push_back(static_cast<uint8_t>(value >> shift));
  }
}

void cdrString(std::vector<uint8_t>& bytes, std::string_view value) {
  cdrU32(bytes, static_cast<uint32_t>(value.size() + 1));
  bytes.insert(bytes.end(), value.begin(), value.end());
  bytes.push_back(0);
}

std::vector<uint8_t> cdrFixture() {
  std::vector<uint8_t> bytes{0, 1, 0, 0};
  cdrU32(bytes, 99);
  cdrU32(bytes, 7);
  cdrU32(bytes, 8);
  cdrString(bytes, "map");
  cdrU32(bytes, 2);
  cdrU32(bytes, 3);
  bytes.insert(bytes.end(), {10, 20, 30});
  cdrU16(bytes, 42);
  return bytes;
}

constexpr std::string_view kCdrSchema = R"(uint32 prefix
std_msgs/Header header
uint32 width
uint8[] payload
uint16 tail
================================================================================
MSG: std_msgs/Header
builtin_interfaces/Time stamp
string frame_id
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec
)";

TEST(ParserModuleCdrReader, ReadsEndianAwarePrimitivesArraysSequencesAndStrings) {
  std::vector<uint8_t> little{0, 1, 0, 0};
  cdrU16(little, 0x1234);
  cdrU16(little, 0x5678);
  cdrU32(little, 3);
  little.insert(little.end(), {1, 2, 3});
  cdrString(little, "ok");

  pj::CdrReader reader({little.data(), little.size()});
  std::array<uint16_t, 2> fixed{};
  ASSERT_TRUE(reader.readFixedArray(fixed).isOk());
  EXPECT_EQ(fixed, (std::array<uint16_t, 2>{0x1234, 0x5678}));
  auto sequence = reader.readByteSequence();
  ASSERT_TRUE(sequence.hasValue()) << sequence.status().message();
  EXPECT_EQ(std::vector<uint8_t>(sequence->data, sequence->data + sequence->size), (std::vector<uint8_t>{1, 2, 3}));
  auto text = reader.readString();
  ASSERT_TRUE(text.hasValue()) << text.status().message();
  EXPECT_EQ(*text, "ok");

  std::vector<uint8_t> big{0, 0, 0, 0};
  cdrU32(big, 0x01020304, false);
  pj::CdrReader big_reader({big.data(), big.size()});
  auto value = big_reader.readU32();
  ASSERT_TRUE(value.hasValue()) << value.status().message();
  EXPECT_EQ(*value, 0x01020304U);
}

TEST(ParserModuleCdrReader, RejectsMalformedLengthsTruncationAndDepthOverflow) {
  for (const std::vector<uint8_t>& bytes : {
           std::vector<uint8_t>{},
           std::vector<uint8_t>{0, 1, 0},
           std::vector<uint8_t>{0, 6, 0, 0},
           std::vector<uint8_t>{0, 1, 0, 0, 4, 0, 0, 0, 'x'},
       }) {
    pj::CdrReader reader({bytes.data(), bytes.size()});
    if (bytes.size() == 9) {
      EXPECT_FALSE(reader.readString().hasValue());
    } else {
      EXPECT_FALSE(reader.status().isOk());
    }
  }

  std::vector<uint8_t> bytes{0, 1, 0, 0};
  cdrU32(bytes, 100);
  pj::CdrReader sequence_reader({bytes.data(), bytes.size()});
  EXPECT_FALSE(sequence_reader.readSequenceLength(4).hasValue());

  pj::CdrReader depth_reader({bytes.data(), bytes.size()});
  for (size_t depth = 0; depth < pj::CdrReader::kMaxTraversalDepth; ++depth) {
    ASSERT_TRUE(depth_reader.enterStruct().isOk());
  }
  EXPECT_FALSE(depth_reader.enterStruct().isOk());
}

TEST(ParserModuleCore, BumpArenaReportsAlignmentErrorsAndReusesStorage) {
  pj::BumpArena arena;
  auto empty = arena.allocate(0);
  ASSERT_TRUE(empty.hasValue()) << empty.status().message();
  EXPECT_EQ(empty->data, nullptr);
  EXPECT_EQ(arena.used(), 0U);
  EXPECT_FALSE(arena.allocate(4, 3).hasValue());
  EXPECT_FALSE(arena.allocate(4, alignof(std::max_align_t) * 2).hasValue());

  auto first = arena.allocate(3, 1);
  auto second = arena.allocate(4, 4);
  ASSERT_TRUE(first.hasValue()) << first.status().message();
  ASSERT_TRUE(second.hasValue()) << second.status().message();
  EXPECT_EQ(reinterpret_cast<uintptr_t>(second->data) % 4U, 0U);
  EXPECT_EQ(arena.used(), 8U);
  arena.reset();
  EXPECT_EQ(arena.used(), 0U);
  EXPECT_TRUE(arena.allocate(8, 8).hasValue());
}

TEST(ParserModuleCdrFieldLocator, CompilesNestedPlanAndCachesOneMessageTraversal) {
  pj::CdrFieldLocator locator(kCdrSchema);
  ASSERT_TRUE(locator.status().isOk()) << locator.status().message();
  auto plan =
      locator.locate({"header.stamp.sec", "header.stamp.nanosec", "header.frame_id", "width", "payload", "tail"});
  ASSERT_TRUE(plan.hasValue()) << plan.status().message();

  const auto sec = plan->field("header.stamp.sec");
  const auto nanos = plan->field("header.stamp.nanosec");
  const auto frame = plan->field("header.frame_id");
  const auto width = plan->field("width");
  const auto payload = plan->field("payload");
  const auto tail = plan->field("tail");
  ASSERT_TRUE(sec && nanos && frame && width && payload && tail);

  const auto bytes = cdrFixture();
  pj::CdrReader reader({bytes.data(), bytes.size()}, *plan);
  auto last = reader.u16(*tail);
  ASSERT_TRUE(last.hasValue()) << last.status().message();
  EXPECT_EQ(*last, 42U);
  EXPECT_EQ(reader.traversalCount(), 1U);
  EXPECT_EQ(*reader.u32(*width), 2U);
  EXPECT_EQ(*reader.string(*frame), "map");
  EXPECT_EQ(*reader.i32(*sec), 7);
  EXPECT_EQ(*reader.u32(*nanos), 8U);
  auto data = reader.bytes(*payload);
  ASSERT_TRUE(data.hasValue());
  EXPECT_EQ(std::vector<uint8_t>(data->data, data->data + data->size), (std::vector<uint8_t>{10, 20, 30}));
  auto reference = reader.spanRef(*payload);
  ASSERT_TRUE(reference.hasValue());
  EXPECT_EQ(reference->offset, static_cast<uint64_t>(data->data - bytes.data()));
  EXPECT_EQ(reference->length, 3U);
  EXPECT_EQ(reader.traversalCount(), 1U);
}

TEST(ParserModuleCdrFieldLocator, AcceptsBoundedAndStringContainersAndRejectsMissingFieldsTruncationAndDeepPaths) {
  pj::CdrFieldLocator bounded("uint8[<=8] data\n");
  EXPECT_TRUE(bounded.status().isOk()) << bounded.status().message();

  pj::CdrFieldLocator valid(kCdrSchema);
  EXPECT_FALSE(valid.locate({"missing"}).hasValue());
  auto plan = valid.locate({"payload"});
  ASSERT_TRUE(plan.hasValue());
  auto bytes = cdrFixture();
  bytes.pop_back();
  pj::CdrReader truncated({bytes.data(), bytes.size()}, *plan);
  EXPECT_FALSE(truncated.bytes(*plan->field("payload")).hasValue());

  std::string schema = "T1 next\n";
  std::string path = "next";
  for (size_t depth = 1; depth <= 64; ++depth) {
    schema += "MSG: T" + std::to_string(depth) + "\n";
    if (depth < 64) {
      schema += "T" + std::to_string(depth + 1) + " next\n";
      path += ".next";
    } else {
      schema += "uint32 value\n";
      path += ".value";
    }
  }
  pj::CdrFieldLocator deep(schema);
  EXPECT_FALSE(deep.status().isOk());
}

TEST(ParserModuleCdrFieldLocator, TraversesBoundedStringsAndArraysOrSequencesOfStrings) {
  constexpr std::string_view schema =
      "uint8[<=4] data\nstring<=4 label\nstring[2] names\nstring[] aliases\nuint16 tail\n";
  pj::CdrFieldLocator locator(schema);
  ASSERT_TRUE(locator.status().isOk()) << locator.status().message();
  auto plan = locator.locate({"data", "label", "tail"});
  ASSERT_TRUE(plan.hasValue()) << plan.status().message();

  std::vector<uint8_t> bytes{0, 1, 0, 0};
  cdrU32(bytes, 3);
  bytes.insert(bytes.end(), {1, 2, 3});
  cdrString(bytes, "tag");
  cdrString(bytes, "one");
  cdrString(bytes, "two");
  cdrU32(bytes, 2);
  cdrString(bytes, "a");
  cdrString(bytes, "b");
  cdrU16(bytes, 77);

  pj::CdrReader reader({bytes.data(), bytes.size()}, *plan);
  EXPECT_EQ(*reader.string(*plan->field("label")), "tag");
  EXPECT_EQ(*reader.u16(*plan->field("tail")), 77U);
  auto data = reader.bytes(*plan->field("data"));
  ASSERT_TRUE(data.hasValue());
  EXPECT_EQ(data->size, 3U);

  auto too_many = bytes;
  too_many[4] = 5;
  pj::CdrReader invalid({too_many.data(), too_many.size()}, *plan);
  EXPECT_FALSE(invalid.bytes(*plan->field("data")).hasValue());
}

TEST(ParserModuleCdrFieldLocator, RejectsCyclicSchemasAtBindAndZeroConsumptionSequencesAtParse) {
  pj::CdrFieldLocator cyclic(
      "pkg/Node[] children\n================================================================================\n"
      "MSG: pkg/Node\npkg/Node[] children\n");
  EXPECT_FALSE(cyclic.status().isOk());
  EXPECT_NE(cyclic.status().message().find("cyclic"), std::string_view::npos);

  pj::CdrFieldLocator empty_sequence(
      "pkg/Empty[] entries\nuint16 "
      "tail\n================================================================================\n"
      "MSG: pkg/Empty\nuint32 CONSTANT=1\n");
  ASSERT_TRUE(empty_sequence.status().isOk()) << empty_sequence.status().message();
  auto plan = empty_sequence.locate({"tail"});
  ASSERT_TRUE(plan.hasValue()) << plan.status().message();
  std::vector<uint8_t> bytes{0, 1, 0, 0};
  cdrU32(bytes, 1);
  cdrU16(bytes, 9);
  pj::CdrReader reader({bytes.data(), bytes.size()}, *plan);
  EXPECT_FALSE(reader.u16(*plan->field("tail")).hasValue());
  EXPECT_NE(reader.status().message().find("zero serialized minimum"), std::string_view::npos);
}

TEST(ParserModuleCdrFieldLocator, PlannedTraversalRewindsAfterStreamingReads) {
  pj::CdrFieldLocator locator(kCdrSchema);
  auto plan = locator.locate({"tail"});
  ASSERT_TRUE(plan.hasValue()) << plan.status().message();
  const auto bytes = cdrFixture();
  pj::CdrReader reader({bytes.data(), bytes.size()}, *plan);
  ASSERT_TRUE(reader.readU32().hasValue());
  auto tail = reader.u16(*plan->field("tail"));
  ASSERT_TRUE(tail.hasValue()) << tail.status().message();
  EXPECT_EQ(*tail, 42U);
  EXPECT_EQ(reader.traversalCount(), 1U);
}

pj::Blob makeMessage(std::initializer_list<std::pair<uint32_t, uint64_t>> fields) {
  pj::WireWriter writer;
  for (const auto& field : fields) {
    EXPECT_TRUE(writer.varintField(field.first, field.second).isOk());
  }
  return writer.take();
}

pj::Blob descriptorField(
    std::string_view name, uint32_t number, uint32_t label, uint32_t type, std::string_view type_name = {}) {
  pj::WireWriter writer;
  EXPECT_TRUE(writer.stringField(1, name).isOk());
  EXPECT_TRUE(writer.varintField(3, number).isOk());
  EXPECT_TRUE(writer.varintField(4, label).isOk());
  EXPECT_TRUE(writer.varintField(5, type).isOk());
  if (!type_name.empty()) {
    EXPECT_TRUE(writer.stringField(6, type_name).isOk());
  }
  return writer.take();
}

pj::Blob descriptorSetFixture() {
  const auto child_count = descriptorField("count", 3, 1, 5);
  pj::WireWriter child;
  EXPECT_TRUE(child.stringField(1, "Child").isOk());
  EXPECT_TRUE(child.lengthDelimited(2, child_count.view()).isOk());

  const auto root_child = descriptorField("child", 1, 1, 11, ".example.Child");
  const auto root_value = descriptorField("value", 2, 1, 13);
  pj::WireWriter root;
  EXPECT_TRUE(root.stringField(1, "Root").isOk());
  EXPECT_TRUE(root.lengthDelimited(2, root_child.view()).isOk());
  EXPECT_TRUE(root.lengthDelimited(2, root_value.view()).isOk());

  pj::WireWriter file;
  EXPECT_TRUE(file.stringField(2, "example").isOk());
  EXPECT_TRUE(file.messageField(4, root).isOk());
  EXPECT_TRUE(file.messageField(4, child).isOk());
  pj::WireWriter set;
  EXPECT_TRUE(set.messageField(1, file).isOk());
  return set.take();
}

pj::Blob nestedDescriptor(size_t depth, size_t nested_count, const std::string& parent) {
  const std::string name = depth == 0 ? "Root" : "N" + std::to_string(depth);
  const std::string full_name = parent.empty() ? name : parent + "." + name;
  pj::WireWriter message;
  EXPECT_TRUE(message.stringField(1, name).isOk());
  if (depth == nested_count) {
    const auto value = descriptorField("value", 1, 1, 13);
    EXPECT_TRUE(message.lengthDelimited(2, value.view()).isOk());
  } else {
    const std::string child_name = "N" + std::to_string(depth + 1);
    const auto next = descriptorField("next", 1, 1, 11, "." + full_name + "." + child_name);
    const auto child = nestedDescriptor(depth + 1, nested_count, full_name);
    EXPECT_TRUE(message.lengthDelimited(2, next.view()).isOk());
    EXPECT_TRUE(message.lengthDelimited(3, child.view()).isOk());
  }
  return message.take();
}

pj::Blob descriptorSetWithRoot(pj::Blob root) {
  pj::WireWriter actual_file;
  EXPECT_TRUE(actual_file.lengthDelimited(4, root.view()).isOk());
  pj::WireWriter set;
  EXPECT_TRUE(set.messageField(1, actual_file).isOk());
  return set.take();
}

TEST(ParserModuleProtoReader, HandlesUnknownPackedUnpackedLastWinsAndMalformedInput) {
  pj::WireWriter writer;
  ASSERT_TRUE(writer.varintField(9, 123).isOk());
  ASSERT_TRUE(writer.varintField(1, 10).isOk());
  ASSERT_TRUE(writer.varintField(2, 1).isOk());
  pj::WireWriter packed;
  ASSERT_TRUE(packed.varint(2).isOk());
  ASSERT_TRUE(packed.varint(300).isOk());
  ASSERT_TRUE(writer.lengthDelimited(2, packed.view()).isOk());
  ASSERT_TRUE(writer.varintField(1, 20).isOk());

  const auto bytes = writer.take();
  pj::ProtoReader reader(bytes.view());
  auto scalar = reader.varint(1);
  ASSERT_TRUE(scalar.hasValue()) << scalar.status().message();
  EXPECT_EQ(*scalar, 20U);
  auto repeated = reader.repeatedVarints(2);
  ASSERT_TRUE(repeated.hasValue()) << repeated.status().message();
  EXPECT_EQ(*repeated, (std::vector<uint64_t>{1, 2, 300}));

  const std::array<uint8_t, 2> truncated_varint{0x08, 0x80};
  EXPECT_FALSE(pj::ProtoReader({truncated_varint.data(), truncated_varint.size()}).varint(1).hasValue());
  const std::array<uint8_t, 2> truncated_group{0x0B, 0x10};
  EXPECT_FALSE(pj::ProtoReader({truncated_group.data(), truncated_group.size()}).matching(2).hasValue());
  EXPECT_FALSE(pj::ProtoReader({nullptr, 1}).matching(1).hasValue());
  EXPECT_FALSE(pj::ProtoReader(bytes.view()).matching(UINT32_C(0x20000000)).hasValue());
}

TEST(ParserModuleProtoReader, BoundsMatchingResultsWithoutThrowing) {
  std::vector<uint8_t> bytes;
  bytes.reserve((pj::ProtoReader::FieldList::kMaximumFields + 1) * 2);
  for (size_t index = 0; index <= pj::ProtoReader::FieldList::kMaximumFields; ++index) {
    bytes.push_back(0x08);
    bytes.push_back(0x00);
  }
  auto fields = pj::ProtoReader({bytes.data(), bytes.size()}).matching(1);
  ASSERT_FALSE(fields.hasValue());
  EXPECT_NE(fields.status().message().find("configured limit"), std::string_view::npos);
}

TEST(ParserModuleProtoReader, ReadsZigzagAndPackedOrUnpackedFixedValues) {
  pj::WireWriter writer;
  ASSERT_TRUE(writer.varintField(1, 3).isOk());
  ASSERT_TRUE(writer.fixed32Field(2, UINT32_C(0x01020304)).isOk());
  const std::array<uint8_t, 8> packed32{0x08, 0x07, 0x06, 0x05, 0x0C, 0x0B, 0x0A, 0x09};
  ASSERT_TRUE(writer.lengthDelimited(2, {packed32.data(), packed32.size()}).isOk());
  ASSERT_TRUE(writer.fixed64Field(3, UINT64_C(0x0102030405060708)).isOk());
  const std::array<uint8_t, 8> packed64{0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11};
  ASSERT_TRUE(writer.lengthDelimited(3, {packed64.data(), packed64.size()}).isOk());

  const auto bytes = writer.take();
  pj::ProtoReader reader(bytes.view());
  EXPECT_EQ(*reader.zigzag(1), -2);
  auto fixed32 = reader.repeatedFixed32(2);
  ASSERT_TRUE(fixed32.hasValue()) << fixed32.status().message();
  EXPECT_EQ(*fixed32, (std::vector<uint32_t>{UINT32_C(0x01020304), UINT32_C(0x05060708), UINT32_C(0x090A0B0C)}));
  auto fixed64 = reader.repeatedFixed64(3);
  ASSERT_TRUE(fixed64.hasValue()) << fixed64.status().message();
  EXPECT_EQ(*fixed64, (std::vector<uint64_t>{UINT64_C(0x0102030405060708), UINT64_C(0x1112131415161718)}));
}

TEST(ParserModuleProtoReader, EnforcesSubmessageDepthCap) {
  pj::Blob nested = makeMessage({{2, 1}});
  for (size_t depth = 0; depth < 65; ++depth) {
    pj::WireWriter wrapper;
    ASSERT_TRUE(wrapper.lengthDelimited(1, nested.view()).isOk());
    nested = wrapper.take();
  }
  pj::ProtoReader reader(nested.view());
  for (size_t depth = 0; depth < pj::ProtoReader::kMaxRecursionDepth; ++depth) {
    auto child = reader.submessage(1);
    ASSERT_TRUE(child.hasValue()) << depth << ": " << child.status().message();
    reader = *child;
  }
  EXPECT_FALSE(reader.submessage(1).hasValue());
}

TEST(ParserModuleProtoFieldLocator, CompilesFileDescriptorSetIntoFieldNumberPaths) {
  const auto descriptors = descriptorSetFixture();
  pj::ProtoFieldLocator locator(descriptors.view(), ".example.Root");
  ASSERT_TRUE(locator.status().isOk()) << locator.status().message();
  auto plan = locator.locate({"child.count", "value"});
  ASSERT_TRUE(plan.hasValue()) << plan.status().message();
  const auto child_count = plan->field("child.count");
  const auto value = plan->field("value");
  ASSERT_TRUE(child_count && value);
  EXPECT_EQ(*plan->numberPath(*child_count), (std::vector<uint32_t>{1, 3}));
  EXPECT_EQ(*plan->numberPath(*value), (std::vector<uint32_t>{2}));

  const auto child = makeMessage({{3, 17}});
  pj::WireWriter root;
  ASSERT_TRUE(root.lengthDelimited(1, child.view()).isOk());
  ASSERT_TRUE(root.varintField(2, 8).isOk());
  const auto root_bytes = root.take();
  pj::ProtoReader reader(root_bytes.view());
  EXPECT_EQ(plan->locate(reader, *child_count)->integer, 17U);
  EXPECT_EQ(plan->locate(reader, *value)->integer, 8U);
  EXPECT_FALSE(locator.locate({"missing"}).hasValue());
}

TEST(ParserModuleProtoFieldLocator, RejectsMalformedLabelsTruncationAndDescriptorDepthOverflow) {
  const auto invalid_field = descriptorField("value", 1, 4, 13);
  pj::WireWriter invalid_root;
  ASSERT_TRUE(invalid_root.stringField(1, "Root").isOk());
  ASSERT_TRUE(invalid_root.lengthDelimited(2, invalid_field.view()).isOk());
  const auto invalid_set = descriptorSetWithRoot(invalid_root.take());
  pj::ProtoFieldLocator invalid_label(invalid_set.view(), "Root");
  EXPECT_FALSE(invalid_label.status().isOk());

  auto valid_set = descriptorSetFixture();
  pj::ProtoFieldLocator truncated({valid_set.data(), valid_set.size() - 1}, ".example.Root");
  EXPECT_FALSE(truncated.status().isOk());

  auto deep_set = descriptorSetWithRoot(nestedDescriptor(0, 64, ""));
  pj::ProtoFieldLocator too_deep(deep_set.view(), "Root");
  EXPECT_FALSE(too_deep.status().isOk());

  const auto first = descriptorField("first", 1, 1, 13);
  const auto duplicate_name = descriptorField("first", 2, 1, 13);
  pj::WireWriter duplicate_name_root;
  ASSERT_TRUE(duplicate_name_root.stringField(1, "Root").isOk());
  ASSERT_TRUE(duplicate_name_root.lengthDelimited(2, first.view()).isOk());
  ASSERT_TRUE(duplicate_name_root.lengthDelimited(2, duplicate_name.view()).isOk());
  const auto duplicate_name_set = descriptorSetWithRoot(duplicate_name_root.take());
  EXPECT_FALSE(pj::ProtoFieldLocator(duplicate_name_set.view(), "Root").status().isOk());

  const auto duplicate_number = descriptorField("second", 1, 1, 13);
  pj::WireWriter duplicate_number_root;
  ASSERT_TRUE(duplicate_number_root.stringField(1, "Root").isOk());
  ASSERT_TRUE(duplicate_number_root.lengthDelimited(2, first.view()).isOk());
  ASSERT_TRUE(duplicate_number_root.lengthDelimited(2, duplicate_number.view()).isOk());
  const auto duplicate_number_set = descriptorSetWithRoot(duplicate_number_root.take());
  EXPECT_FALSE(pj::ProtoFieldLocator(duplicate_number_set.view(), "Root").status().isOk());
}

TEST(ParserModuleTime, RejectsInvalidAndOverflowingTimestamps) {
  EXPECT_EQ(*pj::readRosTime(2, 3), INT64_C(2000000003));
  EXPECT_FALSE(pj::readRosTime(0, UINT32_C(1000000000)).hasValue());
  EXPECT_EQ(*pj::readProtoTimestamp(-1, 500000000), INT64_C(-500000000));
  EXPECT_FALSE(pj::readProtoTimestamp(std::numeric_limits<int64_t>::max(), 0).hasValue());
  EXPECT_FALSE(pj::readProtoTimestamp(0, -1).hasValue());
}

}  // namespace
