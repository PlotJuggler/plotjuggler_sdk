// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/builtin/image_annotations_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "protobuf_wire_test_helpers.hpp"

namespace PJ {
namespace {

using sdk::AnnotationTopology;
using sdk::CircleAnnotation;
using sdk::ColorRGBA;
using sdk::ImageAnnotations;
using sdk::Point2;
using sdk::PointsAnnotation;
using sdk::TextAnnotation;

namespace pb = ::PJ::test_pb;

// Decode the bytes produced by serializeImageAnnotations back into an
// sdk::ImageAnnotations.
sdk::ImageAnnotations roundTrip(const sdk::ImageAnnotations& input) {
  auto bytes = serializeImageAnnotations(input);
  auto result = deserializeImageAnnotations(bytes.data(), bytes.size());
  EXPECT_TRUE(result.has_value());
  return *result;
}

// Compare two ColorRGBA values allowing 1-LSB drift on each channel from the
// double-quantization round-trip (uint8 -> double in [0,1] -> uint8).
::testing::AssertionResult ColorEq(const ColorRGBA& a, const ColorRGBA& b) {
  auto near = [](uint8_t x, uint8_t y) { return x > y ? (x - y) <= 1 : (y - x) <= 1; };
  if (near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.a, b.a)) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << "Color mismatch: got {" << +b.r << "," << +b.g << "," << +b.b << "," << +b.a
                                       << "}, expected {" << +a.r << "," << +a.g << "," << +a.b << "," << +a.a << "}";
}

// -----------------------------------------------------------------------------
// 1. Empty input produces empty bytes
// -----------------------------------------------------------------------------

TEST(ImageAnnotationCodecTest, EmptyAnnotationProducesEmptyBytes) {
  sdk::ImageAnnotations ia;
  auto bytes = serializeImageAnnotations(ia);
  EXPECT_TRUE(bytes.empty());
}

// -----------------------------------------------------------------------------
// 2. Golden-byte test: pins the wire format itself, not just round-trip behavior.
// -----------------------------------------------------------------------------

TEST(ImageAnnotationCodecTest, GoldenBytes_SinglePointsAnnotation) {
  // Build the canonical input: one PointsAnnotation, kLineLoop, two points,
  // outline color = pure red (255, 0, 0, 255), fill = transparent default,
  // thickness = 2.0.
  sdk::ImageAnnotations ia;
  PointsAnnotation pa;
  pa.topology = AnnotationTopology::kLineLoop;
  pa.points = {{1.0, 2.0}, {3.0, 4.0}};
  pa.color = {255, 0, 0, 255};
  pa.fill_color = {0, 0, 0, 0};
  pa.thickness = 2.0;
  ia.points.push_back(std::move(pa));

  // Hand-build the expected byte sequence using the standalone pb helpers.
  // This is the inverse encoding the reader expects, so getting it byte-for-
  // byte right is the strongest available proof.

  // Point2 sub-messages.
  std::vector<uint8_t> p1;
  pb::appendTag(p1, 1, 1);
  pb::appendDouble(p1, 1.0);
  pb::appendTag(p1, 2, 1);
  pb::appendDouble(p1, 2.0);

  std::vector<uint8_t> p2;
  pb::appendTag(p2, 1, 1);
  pb::appendDouble(p2, 3.0);
  pb::appendTag(p2, 2, 1);
  pb::appendDouble(p2, 4.0);

  // Color sub-messages.
  std::vector<uint8_t> outline_color;
  pb::appendTag(outline_color, 1, 1);
  pb::appendDouble(outline_color, 1.0);  // r = 255/255
  pb::appendTag(outline_color, 2, 1);
  pb::appendDouble(outline_color, 0.0);
  pb::appendTag(outline_color, 3, 1);
  pb::appendDouble(outline_color, 0.0);
  pb::appendTag(outline_color, 4, 1);
  pb::appendDouble(outline_color, 1.0);  // a = 255/255

  std::vector<uint8_t> fill_color;
  pb::appendTag(fill_color, 1, 1);
  pb::appendDouble(fill_color, 0.0);
  pb::appendTag(fill_color, 2, 1);
  pb::appendDouble(fill_color, 0.0);
  pb::appendTag(fill_color, 3, 1);
  pb::appendDouble(fill_color, 0.0);
  pb::appendTag(fill_color, 4, 1);
  pb::appendDouble(fill_color, 0.0);

  // PointsAnnotation body.
  std::vector<uint8_t> pa_body;
  pb::appendTag(pa_body, 2, 0);
  pb::appendVarint(pa_body, 2);  // kLineLoop = 2
  pb::appendTag(pa_body, 3, 2);
  pb::appendLenDelim(pa_body, p1);
  pb::appendTag(pa_body, 3, 2);
  pb::appendLenDelim(pa_body, p2);
  pb::appendTag(pa_body, 4, 2);
  pb::appendLenDelim(pa_body, outline_color);
  pb::appendTag(pa_body, 6, 2);
  pb::appendLenDelim(pa_body, fill_color);
  pb::appendTag(pa_body, 7, 1);
  pb::appendDouble(pa_body, 2.0);

  // Top-level ImageAnnotations: one PointsAnnotation at field 2.
  std::vector<uint8_t> expected;
  pb::appendTag(expected, 2, 2);
  pb::appendLenDelim(expected, pa_body);

  auto actual = serializeImageAnnotations(ia);
  EXPECT_EQ(actual, expected) << "wire format mismatch";
}

// -----------------------------------------------------------------------------
// 3. Round-trip tests: build, serialize, read, compare.
// -----------------------------------------------------------------------------

TEST(ImageAnnotationCodecTest, RoundTrip_LineLoopFourPoints) {
  sdk::ImageAnnotations in;
  PointsAnnotation pa;
  pa.topology = AnnotationTopology::kLineLoop;
  pa.points = {{75.0, 185.0}, {125.0, 185.0}, {125.0, 215.0}, {75.0, 215.0}};
  pa.color = {0, 255, 0, 255};
  pa.fill_color = {0, 0, 0, 0};
  pa.thickness = 2.5;
  in.points.push_back(std::move(pa));

  auto out = roundTrip(in);
  ASSERT_EQ(out.points.size(), 1u);
  EXPECT_EQ(out.points[0].topology, AnnotationTopology::kLineLoop);
  EXPECT_EQ(out.points[0].points, in.points[0].points);
  EXPECT_TRUE(ColorEq(in.points[0].color, out.points[0].color));
  EXPECT_TRUE(ColorEq(in.points[0].fill_color, out.points[0].fill_color));
  EXPECT_DOUBLE_EQ(out.points[0].thickness, 2.5);
}

TEST(ImageAnnotationCodecTest, RoundTrip_AllTopologies) {
  for (auto topology :
       {AnnotationTopology::kPoints, AnnotationTopology::kLineList, AnnotationTopology::kLineStrip,
        AnnotationTopology::kLineLoop}) {
    sdk::ImageAnnotations in;
    PointsAnnotation pa;
    pa.topology = topology;
    pa.points = {{0.0, 0.0}, {10.0, 10.0}};
    pa.color = {0, 255, 0, 255};
    pa.fill_color = {0, 0, 0, 0};
    pa.thickness = 2.0;
    in.points.push_back(std::move(pa));

    auto out = roundTrip(in);
    ASSERT_EQ(out.points.size(), 1u) << "topology=" << static_cast<int>(topology);
    EXPECT_EQ(out.points[0].topology, topology);
  }
}

TEST(ImageAnnotationCodecTest, RoundTrip_CirclePreservesDiameterRadiusInverse) {
  sdk::ImageAnnotations in;
  CircleAnnotation ca;
  ca.center = {50.0, 60.0};
  ca.radius = 10.0;  // wire emits diameter = 20; reader halves back to 10.
  ca.thickness = 1.5;
  ca.color = {0, 255, 0, 255};
  ca.fill_color = {255, 0, 0, 128};  // semi-transparent red
  in.circles.push_back(std::move(ca));

  auto out = roundTrip(in);
  ASSERT_EQ(out.circles.size(), 1u);
  EXPECT_DOUBLE_EQ(out.circles[0].center.x, 50.0);
  EXPECT_DOUBLE_EQ(out.circles[0].center.y, 60.0);
  EXPECT_DOUBLE_EQ(out.circles[0].radius, 10.0);
  EXPECT_DOUBLE_EQ(out.circles[0].thickness, 1.5);
  EXPECT_TRUE(ColorEq(in.circles[0].color, out.circles[0].color));
  EXPECT_TRUE(ColorEq(in.circles[0].fill_color, out.circles[0].fill_color));
}

TEST(ImageAnnotationCodecTest, RoundTrip_TextUtf8) {
  sdk::ImageAnnotations in;
  TextAnnotation ta;
  ta.position = {320.5, 240.25};
  ta.font_size = 14.0;
  ta.color = {255, 255, 255, 255};
  ta.text = "person 0.95 \xe2\x80\x94 \xc3\xa1\xc3\xa9\xc3\xad";  // UTF-8 text
  in.texts.push_back(std::move(ta));

  auto out = roundTrip(in);
  ASSERT_EQ(out.texts.size(), 1u);
  EXPECT_EQ(out.texts[0].text, in.texts[0].text);
  EXPECT_DOUBLE_EQ(out.texts[0].position.x, 320.5);
  EXPECT_DOUBLE_EQ(out.texts[0].position.y, 240.25);
  EXPECT_DOUBLE_EQ(out.texts[0].font_size, 14.0);
  EXPECT_TRUE(ColorEq(in.texts[0].color, out.texts[0].color));
}

TEST(ImageAnnotationCodecTest, RoundTrip_FullImageAnnotationAllThreeKinds) {
  sdk::ImageAnnotations in;

  // Two points annotations.
  PointsAnnotation pa1;
  pa1.topology = AnnotationTopology::kLineLoop;
  pa1.points = {{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}};
  pa1.color = {255, 128, 64, 255};
  pa1.thickness = 3.0;
  in.points.push_back(pa1);

  PointsAnnotation pa2;
  pa2.topology = AnnotationTopology::kLineStrip;
  pa2.points = {{50.0, 50.0}, {150.0, 100.0}, {200.0, 200.0}};
  pa2.color = {64, 255, 128, 200};
  pa2.thickness = 1.0;
  in.points.push_back(pa2);

  // One circle.
  CircleAnnotation ca;
  ca.center = {320.0, 240.0};
  ca.radius = 5.0;
  ca.thickness = 2.0;
  ca.color = {0, 0, 255, 255};
  ca.fill_color = {0, 0, 255, 64};
  in.circles.push_back(ca);

  // One text.
  TextAnnotation ta;
  ta.position = {10.0, 10.0};
  ta.font_size = 12.0;
  ta.color = {255, 255, 0, 255};
  ta.text = "label";
  in.texts.push_back(ta);

  auto out = roundTrip(in);
  ASSERT_EQ(out.points.size(), 2u);
  ASSERT_EQ(out.circles.size(), 1u);
  ASSERT_EQ(out.texts.size(), 1u);

  EXPECT_EQ(out.points[0].topology, AnnotationTopology::kLineLoop);
  EXPECT_EQ(out.points[1].topology, AnnotationTopology::kLineStrip);
  EXPECT_EQ(out.points[0].points.size(), 4u);
  EXPECT_EQ(out.points[1].points.size(), 3u);
  EXPECT_DOUBLE_EQ(out.circles[0].radius, 5.0);
  EXPECT_EQ(out.texts[0].text, "label");
}

TEST(ImageAnnotationCodecTest, EmptyColorsVectorDoesNotInjectDefaultEntry) {
  // A PointsAnnotation with empty `colors` must round-trip to empty `colors`.
  // If the writer emitted a default Color for the empty vector, the reader
  // would push a phantom entry, breaking per-vertex coloring semantics.
  sdk::ImageAnnotations in;
  PointsAnnotation pa;
  pa.topology = AnnotationTopology::kPoints;
  pa.points = {{1.0, 1.0}, {2.0, 2.0}};
  pa.colors = {};  // explicitly empty
  pa.color = {0, 255, 0, 255};
  pa.thickness = 2.0;
  in.points.push_back(std::move(pa));

  auto out = roundTrip(in);
  ASSERT_EQ(out.points.size(), 1u);
  EXPECT_TRUE(out.points[0].colors.empty()) << "writer must not emit phantom field-5 entries";
}

TEST(ImageAnnotationCodecTest, RoundTrip_PerVertexColors) {
  sdk::ImageAnnotations in;
  PointsAnnotation pa;
  pa.topology = AnnotationTopology::kLineStrip;
  pa.points = {{0.0, 0.0}, {10.0, 10.0}, {20.0, 0.0}};
  pa.colors = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
  pa.color = {255, 255, 255, 255};
  pa.thickness = 2.0;
  in.points.push_back(std::move(pa));

  auto out = roundTrip(in);
  ASSERT_EQ(out.points.size(), 1u);
  ASSERT_EQ(out.points[0].colors.size(), 3u);
  EXPECT_TRUE(ColorEq(in.points[0].colors[0], out.points[0].colors[0]));
  EXPECT_TRUE(ColorEq(in.points[0].colors[1], out.points[0].colors[1]));
  EXPECT_TRUE(ColorEq(in.points[0].colors[2], out.points[0].colors[2]));
}

}  // namespace
}  // namespace PJ
