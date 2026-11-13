// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/plot_markers_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::ColorRGBA;
using sdk::MarkerKind;
using sdk::MarkerProperty;
using sdk::MarkerSeverity;
using sdk::MarkerStatus;
using sdk::PlotMarker;
using sdk::PlotMarkers;

// Decode the bytes produced by serializePlotMarkers back into a sdk::PlotMarkers.
sdk::PlotMarkers roundTrip(const sdk::PlotMarkers& input) {
  auto bytes = serializePlotMarkers(input);
  auto result = deserializePlotMarkers(bytes.data(), bytes.size());
  EXPECT_TRUE(result.has_value());
  return result.has_value() ? *result : sdk::PlotMarkers{};
}

// Compare two ColorRGBA values allowing 1-LSB drift per channel from the
// uint8 -> double-in-[0,1] -> uint8 quantization round-trip.
::testing::AssertionResult ColorEq(const ColorRGBA& a, const ColorRGBA& b) {
  auto near = [](uint8_t x, uint8_t y) { return x > y ? (x - y) <= 1 : (y - x) <= 1; };
  if (near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.a, b.a)) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << "Color mismatch";
}

// -----------------------------------------------------------------------------
// Empty handling
// -----------------------------------------------------------------------------

TEST(PlotMarkersCodecTest, EmptySetProducesEmptyBytes) {
  PlotMarkers markers;
  EXPECT_TRUE(serializePlotMarkers(markers).empty());
}

// An empty buffer is the canonical encoding of an empty set (the replace-only
// store's "clear" tombstone), so it round-trips instead of erroring.
TEST(PlotMarkersCodecTest, EmptyBufferDecodesToEmptySet) {
  const Expected<PlotMarkers> null_empty = deserializePlotMarkers(nullptr, 0);
  ASSERT_TRUE(null_empty.has_value());
  EXPECT_TRUE(null_empty->markers.empty());
  const uint8_t byte = 0;
  const Expected<PlotMarkers> ptr_empty = deserializePlotMarkers(&byte, 0);
  ASSERT_TRUE(ptr_empty.has_value());
  EXPECT_TRUE(ptr_empty->markers.empty());
}

TEST(PlotMarkersCodecTest, NullBufferWithSizeIsError) {
  EXPECT_FALSE(deserializePlotMarkers(nullptr, 4).has_value());
}

// -----------------------------------------------------------------------------
// Round-trip per kind (default color a=0 round-trips exactly)
// -----------------------------------------------------------------------------

TEST(PlotMarkersCodecTest, RegionRoundTrip) {
  PlotMarker m;
  m.kind = MarkerKind::kRegion;
  m.t_start = 1'000'000'000;  // 1s in ns
  m.t_end = 2'000'000'000;
  m.status = MarkerStatus::kFail;
  m.severity = MarkerSeverity::kError;
  m.category = "overspeed";
  m.label = "joint_2 velocity > 1 rad/s";
  m.description = "sustained above threshold";
  m.metadata = {{"peak", "1.83"}};

  PlotMarkers in;
  in.markers = {m};
  EXPECT_TRUE(roundTrip(in) == in);
}

TEST(PlotMarkersCodecTest, EventRoundTrip) {
  PlotMarker m;
  m.kind = MarkerKind::kEvent;
  m.t_start = 1'905'100'000;
  m.value_low = 42.5;
  m.has_value = true;
  m.status = MarkerStatus::kFail;
  m.severity = MarkerSeverity::kCritical;
  m.category = "state_transition";
  m.label = "OK -> ERROR";
  m.metadata = {{"from", "OK"}, {"to", "ERROR"}};

  PlotMarkers in;
  in.markers = {m};
  EXPECT_TRUE(roundTrip(in) == in);
}

TEST(PlotMarkersCodecTest, ValueBandRoundTrip) {
  PlotMarker m;
  m.kind = MarkerKind::kValueBand;
  m.value_low = -0.5;
  m.value_high = 0.5;
  m.severity = MarkerSeverity::kInfo;
  m.label = "operating range";

  PlotMarkers in;
  in.markers = {m};
  EXPECT_TRUE(roundTrip(in) == in);
}

TEST(PlotMarkersCodecTest, LabelRoundTrip) {
  PlotMarker m;
  m.kind = MarkerKind::kLabel;
  m.t_start = 500'000'000;
  m.label = "sensor recalibrated";

  PlotMarkers in;
  in.markers = {m};
  EXPECT_TRUE(roundTrip(in) == in);
}

TEST(PlotMarkersCodecTest, MultipleMarkersPreserveOrder) {
  PlotMarker a;
  a.kind = MarkerKind::kRegion;
  a.t_start = 10;
  a.t_end = 20;
  a.label = "a";
  PlotMarker b;
  b.kind = MarkerKind::kEvent;
  b.t_start = 30;
  b.label = "b";

  PlotMarkers in;
  in.markers = {a, b};
  auto out = roundTrip(in);
  ASSERT_EQ(out.markers.size(), 2u);
  EXPECT_TRUE(out == in);
}

TEST(PlotMarkersCodecTest, AllFourKindsRoundTrip) {
  PlotMarkers in;
  for (const MarkerKind kind : {MarkerKind::kRegion, MarkerKind::kEvent, MarkerKind::kValueBand, MarkerKind::kLabel}) {
    PlotMarker m;
    m.kind = kind;
    m.t_start = 10;
    m.label = "m";
    in.markers.push_back(m);
  }
  const PlotMarkers out = roundTrip(in);
  ASSERT_EQ(out.markers.size(), 4u);
  EXPECT_TRUE(out == in);
}

// -----------------------------------------------------------------------------
// Kind wire numbers, pinned independently of the codec's own mapping
// -----------------------------------------------------------------------------

// PlotMarkers { markers: [ PlotMarker { kind?, label } ... ] } built by hand. A nullopt kind omits the field, as proto3
// does for the zero value.
struct HandMarker {
  std::optional<uint64_t> kind;
  std::string label;
};

std::vector<uint8_t> handBuiltMarkers(const std::vector<HandMarker>& markers) {
  std::vector<uint8_t> out;
  for (const auto& m : markers) {
    std::vector<uint8_t> body;
    if (m.kind) {
      test_pb::appendTag(body, 1, 0);
      test_pb::appendVarint(body, *m.kind);
    }
    test_pb::appendTag(body, 10, 2);
    test_pb::appendString(body, m.label);
    test_pb::appendTag(out, 1, 2);
    test_pb::appendLenDelim(out, body);
  }
  return out;
}

TEST(PlotMarkersCodecTest, KindWireNumbersAreStable) {
  const std::pair<MarkerKind, uint8_t> kinds[] = {
      {MarkerKind::kRegion, 0}, {MarkerKind::kEvent, 1}, {MarkerKind::kValueBand, 2}, {MarkerKind::kLabel, 3}};
  for (const auto& [kind, wire] : kinds) {
    // decode: wire number -> kind
    const auto bytes = handBuiltMarkers({{wire, "m"}});
    const auto out = deserializePlotMarkers(bytes.data(), bytes.size());
    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->markers.size(), 1u);
    EXPECT_EQ(out->markers[0].kind, kind);

    // encode: kind -> wire number (PlotMarkers.markers[0] is 0x0A <len> 0x08 <kind> ...)
    PlotMarker m;
    m.kind = kind;
    PlotMarkers in;
    in.markers = {m};
    const auto encoded = serializePlotMarkers(in);
    ASSERT_GE(encoded.size(), 4u);
    EXPECT_EQ(encoded[0], 0x0A);
    ASSERT_LT(encoded[1], 0x80);  // one-byte length
    EXPECT_EQ(encoded[2], 0x08);
    EXPECT_EQ(encoded[3], wire);
  }
}

TEST(PlotMarkersCodecTest, MarkerWithoutKindFieldDecodesAsRegion) {
  const auto bytes = handBuiltMarkers({{std::nullopt, "no kind"}});
  const auto out = deserializePlotMarkers(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->markers.size(), 1u);
  EXPECT_EQ(out->markers[0].kind, MarkerKind::kRegion);
  EXPECT_EQ(out->markers[0].label, "no kind");
}

// A reader drops a marker whose kind it does not know (a later release may add kinds); the rest of the set decodes.
TEST(PlotMarkersCodecTest, MarkerWithUnknownKindIsDropped) {
  const auto bytes =
      handBuiltMarkers({{1, "before"}, {4, "future kind"}, {std::nullopt, "after"}, {1000, "far future"}});
  const auto out = deserializePlotMarkers(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  ASSERT_EQ(out->markers.size(), 2u);
  EXPECT_EQ(out->markers[0].kind, MarkerKind::kEvent);
  EXPECT_EQ(out->markers[0].label, "before");
  EXPECT_EQ(out->markers[1].kind, MarkerKind::kRegion);
  EXPECT_EQ(out->markers[1].label, "after");
}

// Every marker dropped leaves the empty set, like the empty buffer.
TEST(PlotMarkersCodecTest, AllMarkersDroppedDecodesToEmptySet) {
  const auto bytes = handBuiltMarkers({{4, "a"}, {5, "b"}});
  ASSERT_FALSE(bytes.empty());
  const auto out = deserializePlotMarkers(bytes.data(), bytes.size());
  ASSERT_TRUE(out.has_value());
  EXPECT_TRUE(out->markers.empty());
}

TEST(PlotMarkersCodecTest, ColorRoundTripWithinOneLsb) {
  PlotMarker m;
  m.kind = MarkerKind::kRegion;
  m.t_start = 0;
  m.t_end = 100;
  m.color = {255, 128, 0, 200};

  PlotMarkers in;
  in.markers = {m};
  auto out = roundTrip(in);
  ASSERT_EQ(out.markers.size(), 1u);
  EXPECT_TRUE(ColorEq(m.color, out.markers[0].color));
}

}  // namespace
}  // namespace PJ
