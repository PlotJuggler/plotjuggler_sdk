// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/plot_markers_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

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
