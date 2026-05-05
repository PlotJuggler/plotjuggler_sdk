#include "pj_scene_protocol/scene_decoder.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "pj_scene_protocol/image_annotation.h"

namespace PJ {
namespace {

TEST(SceneDecoderTest, FactoryReturnsNullForUnknownSchema) {
  auto dec = makeSceneDecoder("nonsense/Schema");
  EXPECT_EQ(dec.get(), nullptr);
}

// ---------------------------------------------------------------------------
// Protobuf decoder tests (foxglove.ImageAnnotations) — the canonical wire
// format. Per-source-format conversion (CDR vision_msgs, yolo, …) is
// loader-side and tested elsewhere (see pj_media/demos/cdr_to_image_annotation_test).
// ---------------------------------------------------------------------------

namespace pb {
// Tiny encoder helpers for tests — protobuf wire format.

inline void appendVarint(std::vector<uint8_t>& out, uint64_t v) {
  while (v >= 0x80) {
    out.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
    v >>= 7;
  }
  out.push_back(static_cast<uint8_t>(v));
}

inline void appendTag(std::vector<uint8_t>& out, uint32_t field, uint32_t wire) {
  appendVarint(out, (static_cast<uint64_t>(field) << 3) | wire);
}

inline void appendDouble(std::vector<uint8_t>& out, double v) {
  uint64_t bits = 0;
  std::memcpy(&bits, &v, 8);
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu));
  }
}

inline void appendLenDelim(std::vector<uint8_t>& out, const std::vector<uint8_t>& body) {
  appendVarint(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
}

}  // namespace pb

// Build a Foxglove Point2 sub-message: {1: double x, 2: double y}
std::vector<uint8_t> encodePoint2(double x, double y) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 1, 1);
  pb::appendDouble(body, x);  // x = double
  pb::appendTag(body, 2, 1);
  pb::appendDouble(body, y);  // y = double
  return body;
}

// Build a Foxglove PointsAnnotation: {2: type, 3: repeated points, 7: thickness}
std::vector<uint8_t> encodePointsAnnotation(
    uint32_t type, const std::vector<std::pair<double, double>>& pts, double thickness) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 2, 0);
  pb::appendVarint(body, type);
  for (const auto& [x, y] : pts) {
    pb::appendTag(body, 3, 2);
    pb::appendLenDelim(body, encodePoint2(x, y));
  }
  pb::appendTag(body, 7, 1);
  pb::appendDouble(body, thickness);
  return body;
}

// Build a Foxglove ImageAnnotations: {2: repeated PointsAnnotation}
std::vector<uint8_t> encodeImageAnnotations(const std::vector<std::vector<uint8_t>>& point_annotations) {
  std::vector<uint8_t> out;
  for (const auto& pa : point_annotations) {
    pb::appendTag(out, 2, 2);
    pb::appendLenDelim(out, pa);
  }
  return out;
}

// Build a Foxglove Color sub-message: {1: r, 2: g, 3: b, 4: a} (all double in [0,1]).
std::vector<uint8_t> encodeColor(double r, double g, double b, double a) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 1, 1);
  pb::appendDouble(body, r);
  pb::appendTag(body, 2, 1);
  pb::appendDouble(body, g);
  pb::appendTag(body, 3, 1);
  pb::appendDouble(body, b);
  pb::appendTag(body, 4, 1);
  pb::appendDouble(body, a);
  return body;
}

// Build a Foxglove CircleAnnotation: {2: position, 3: diameter, 4: thickness,
// 5: fill_color, 6: outline_color}
std::vector<uint8_t> encodeCircleAnnotation(
    double x, double y, double diameter, double thickness, const std::vector<uint8_t>& fill,
    const std::vector<uint8_t>& outline) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 2, 2);
  pb::appendLenDelim(body, encodePoint2(x, y));
  pb::appendTag(body, 3, 1);
  pb::appendDouble(body, diameter);
  pb::appendTag(body, 4, 1);
  pb::appendDouble(body, thickness);
  pb::appendTag(body, 5, 2);
  pb::appendLenDelim(body, fill);
  pb::appendTag(body, 6, 2);
  pb::appendLenDelim(body, outline);
  return body;
}

TEST(SceneDecoderProtobufTest, FactoryReturnsDecoderForImageAnnotations) {
  auto dec = makeSceneDecoder("foxglove.ImageAnnotations");
  ASSERT_NE(dec.get(), nullptr);
}

TEST(SceneDecoderProtobufTest, EmptyMessageProducesEmptyAnnotation) {
  auto dec = makeSceneDecoder("foxglove.ImageAnnotations");
  ASSERT_NE(dec.get(), nullptr);

  std::vector<uint8_t> empty_body;
  auto result = dec->decode(empty_body.data(), empty_body.size());
  // Empty buffer is treated as error per the decoder's contract.
  EXPECT_FALSE(result.has_value());
}

TEST(SceneDecoderProtobufTest, SingleLineLoopFourPoints) {
  auto dec = makeSceneDecoder("foxglove.ImageAnnotations");
  ASSERT_NE(dec.get(), nullptr);

  // type=2 (LINE_LOOP), 4 corners, thickness=2.5
  auto pa = encodePointsAnnotation(2, {{10.0, 20.0}, {110.0, 20.0}, {110.0, 80.0}, {10.0, 80.0}}, 2.5);
  auto bytes = encodeImageAnnotations({pa});

  auto result = dec->decode(bytes.data(), bytes.size());
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->annotations.size(), 1u);
  const auto& ia = result->annotations[0];
  ASSERT_EQ(ia.points.size(), 1u);

  const auto& pts = ia.points[0];
  EXPECT_EQ(pts.topology, AnnotationTopology::kLineLoop);
  ASSERT_EQ(pts.points.size(), 4u);
  EXPECT_DOUBLE_EQ(pts.points[0].x, 10.0);
  EXPECT_DOUBLE_EQ(pts.points[0].y, 20.0);
  EXPECT_DOUBLE_EQ(pts.points[2].x, 110.0);
  EXPECT_DOUBLE_EQ(pts.points[2].y, 80.0);
  EXPECT_DOUBLE_EQ(pts.thickness, 2.5);
}

TEST(SceneDecoderProtobufTest, MultiplePointsAnnotations) {
  auto dec = makeSceneDecoder("foxglove.ImageAnnotations");
  ASSERT_NE(dec.get(), nullptr);

  auto pa1 = encodePointsAnnotation(2, {{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}}, 1.0);
  auto pa2 = encodePointsAnnotation(3, {{50.0, 50.0}, {150.0, 150.0}}, 3.0);  // LINE_STRIP
  auto bytes = encodeImageAnnotations({pa1, pa2});

  auto result = dec->decode(bytes.data(), bytes.size());
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->annotations[0].points.size(), 2u);
  EXPECT_EQ(result->annotations[0].points[0].topology, AnnotationTopology::kLineLoop);
  EXPECT_EQ(result->annotations[0].points[1].topology, AnnotationTopology::kLineStrip);
}

TEST(SceneDecoderProtobufTest, CircleAnnotationDecodes) {
  auto dec = makeSceneDecoder("foxglove.ImageAnnotations");
  ASSERT_NE(dec.get(), nullptr);

  // CircleAnnotation centered at (50, 60) with diameter 20 (radius 10), thickness 1.5,
  // semi-transparent red fill, opaque white outline.
  auto fill = encodeColor(1.0, 0.0, 0.0, 0.5);
  auto outline = encodeColor(1.0, 1.0, 1.0, 1.0);
  auto ca = encodeCircleAnnotation(50.0, 60.0, 20.0, 1.5, fill, outline);

  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 1, 2);
  pb::appendLenDelim(bytes, ca);

  auto result = dec->decode(bytes.data(), bytes.size());
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->annotations.size(), 1u);
  ASSERT_EQ(result->annotations[0].circles.size(), 1u);
  const auto& c = result->annotations[0].circles[0];
  EXPECT_DOUBLE_EQ(c.center.x, 50.0);
  EXPECT_DOUBLE_EQ(c.center.y, 60.0);
  EXPECT_DOUBLE_EQ(c.radius, 10.0);
  EXPECT_DOUBLE_EQ(c.thickness, 1.5);
  EXPECT_EQ(c.color.r, 255u);  // outline = white
  EXPECT_EQ(c.color.a, 255u);
  EXPECT_EQ(c.fill_color.r, 255u);  // fill = red, alpha 0.5 → 128
  EXPECT_EQ(c.fill_color.g, 0u);
  EXPECT_NEAR(c.fill_color.a, 128u, 1u);
}

TEST(SceneDecoderProtobufTest, NullDataReturnsError) {
  auto dec = makeSceneDecoder("foxglove.ImageAnnotations");
  ASSERT_NE(dec.get(), nullptr);
  auto result = dec->decode(nullptr, 0);
  EXPECT_FALSE(result.has_value());
}

}  // namespace
}  // namespace PJ
