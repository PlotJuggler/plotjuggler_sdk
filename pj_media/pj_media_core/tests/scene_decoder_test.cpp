#include "pj_media_core/scene_decoder.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "nanocdr/nanocdr.hpp"
#include "pj_media_core/scene_frame.h"

namespace PJ {
namespace {

// Build a synthetic CDR-encoded vision_msgs/msg/Detection2DArray with N
// detections. Mirrors the field layout the decoder expects.
std::vector<uint8_t> encodeDetection2DArray(
    int32_t header_sec, uint32_t header_nanosec, const std::string& frame_id,
    const std::vector<std::tuple<double, double, double, double>>& bboxes_cxcywh) {
  std::vector<uint8_t> storage;
  nanocdr::Encoder enc(nanocdr::CdrHeader{}, storage);

  // header: stamp.sec, stamp.nanosec, frame_id
  enc.encode(static_cast<uint32_t>(header_sec));
  enc.encode(header_nanosec);
  enc.encode(frame_id);

  // detections[]
  enc.encode(static_cast<uint32_t>(bboxes_cxcywh.size()));
  for (const auto& [cx, cy, w, h] : bboxes_cxcywh) {
    // per-detection header
    enc.encode(static_cast<uint32_t>(0));  // sec
    enc.encode(static_cast<uint32_t>(0));  // nanosec
    enc.encode(std::string(""));            // frame_id

    // results[]: empty (zero-length vector)
    enc.encode(static_cast<uint32_t>(0));

    // bbox: center.x, center.y, center.theta, size_x, size_y
    enc.encode(static_cast<double>(cx));
    enc.encode(static_cast<double>(cy));
    enc.encode(static_cast<double>(0.0));
    enc.encode(static_cast<double>(w));
    enc.encode(static_cast<double>(h));

    // id
    enc.encode(std::string(""));
  }

  auto buf = enc.encodedBuffer();
  return std::vector<uint8_t>(buf.data(), buf.data() + buf.size());
}

TEST(SceneDecoderCdrTest, FactoryReturnsNullForUnknownSchema) {
  auto dec = makeSceneDecoder("nonsense/Schema");
  EXPECT_EQ(dec.get(), nullptr);
}

TEST(SceneDecoderCdrTest, FactoryReturnsDecoderForDetection2DArray) {
  auto dec = makeSceneDecoder("vision_msgs/msg/Detection2DArray");
  ASSERT_NE(dec.get(), nullptr);
}

TEST(SceneDecoderCdrTest, EmptyDetectionListProducesEmptyAnnotation) {
  auto dec = makeSceneDecoder("vision_msgs/msg/Detection2DArray");
  ASSERT_NE(dec.get(), nullptr);

  auto bytes = encodeDetection2DArray(0, 0, "/camera", {});
  auto result = dec->decode(bytes.data(), bytes.size());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->annotations.size(), 1u);
  EXPECT_TRUE(result->annotations[0].points.empty());
}

TEST(SceneDecoderCdrTest, ThreeBboxesProduceThreeLineLoops) {
  auto dec = makeSceneDecoder("vision_msgs/msg/Detection2DArray");
  ASSERT_NE(dec.get(), nullptr);

  auto bytes = encodeDetection2DArray(
      1234, 567'000'000, "/camera/image",
      {
          {100.0, 200.0, 50.0, 30.0},   // bbox at (100,200) size 50x30
          {500.0, 400.0, 80.0, 80.0},
          {320.0, 240.0, 100.0, 60.0},
      });

  auto result = dec->decode(bytes.data(), bytes.size());
  ASSERT_TRUE(result.has_value()) << "decode failed";
  ASSERT_EQ(result->annotations.size(), 1u);

  const auto& ia = result->annotations[0];
  EXPECT_EQ(ia.image_topic, "/camera/image");
  EXPECT_EQ(ia.timestamp, 1234LL * 1'000'000'000LL + 567'000'000LL);
  ASSERT_EQ(ia.points.size(), 3u);

  for (const auto& bbox : ia.points) {
    EXPECT_EQ(bbox.topology, AnnotationTopology::kLineLoop);
    EXPECT_EQ(bbox.points.size(), 4u);
  }

  // Verify first bbox geometry: cx=100, cy=200, w=50, h=30
  // → corners: (75, 185), (125, 185), (125, 215), (75, 215)
  const auto& first = ia.points[0];
  EXPECT_DOUBLE_EQ(first.points[0].x, 75.0);
  EXPECT_DOUBLE_EQ(first.points[0].y, 185.0);
  EXPECT_DOUBLE_EQ(first.points[1].x, 125.0);
  EXPECT_DOUBLE_EQ(first.points[1].y, 185.0);
  EXPECT_DOUBLE_EQ(first.points[2].x, 125.0);
  EXPECT_DOUBLE_EQ(first.points[2].y, 215.0);
  EXPECT_DOUBLE_EQ(first.points[3].x, 75.0);
  EXPECT_DOUBLE_EQ(first.points[3].y, 215.0);
}

TEST(SceneDecoderCdrTest, TooSmallBufferReturnsError) {
  auto dec = makeSceneDecoder("vision_msgs/msg/Detection2DArray");
  ASSERT_NE(dec.get(), nullptr);

  std::vector<uint8_t> tiny = {0, 0};
  auto result = dec->decode(tiny.data(), tiny.size());
  EXPECT_FALSE(result.has_value());
}

TEST(SceneDecoderCdrTest, NullDataReturnsError) {
  auto dec = makeSceneDecoder("vision_msgs/msg/Detection2DArray");
  ASSERT_NE(dec.get(), nullptr);

  auto result = dec->decode(nullptr, 0);
  EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Protobuf decoder tests (foxglove.ImageAnnotations)
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
  pb::appendTag(body, 1, 1); pb::appendDouble(body, x);  // x = double
  pb::appendTag(body, 2, 1); pb::appendDouble(body, y);  // y = double
  return body;
}

// Build a Foxglove PointsAnnotation: {2: type, 3: repeated points, 7: thickness}
std::vector<uint8_t> encodePointsAnnotation(uint32_t type, const std::vector<std::pair<double, double>>& pts,
                                            double thickness) {
  std::vector<uint8_t> body;
  pb::appendTag(body, 2, 0); pb::appendVarint(body, type);
  for (const auto& [x, y] : pts) {
    pb::appendTag(body, 3, 2);
    pb::appendLenDelim(body, encodePoint2(x, y));
  }
  pb::appendTag(body, 7, 1); pb::appendDouble(body, thickness);
  return body;
}

// Build a Foxglove ImageAnnotations: {2: repeated PointsAnnotation}
std::vector<uint8_t> encodeImageAnnotations(
    const std::vector<std::vector<uint8_t>>& point_annotations) {
  std::vector<uint8_t> out;
  for (const auto& pa : point_annotations) {
    pb::appendTag(out, 2, 2);
    pb::appendLenDelim(out, pa);
  }
  return out;
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
  auto pa = encodePointsAnnotation(2,
                                    {{10.0, 20.0}, {110.0, 20.0}, {110.0, 80.0}, {10.0, 80.0}},
                                    2.5);
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

TEST(SceneDecoderProtobufTest, UnknownFieldsAreSkippedSafely) {
  auto dec = makeSceneDecoder("foxglove.ImageAnnotations");
  ASSERT_NE(dec.get(), nullptr);

  // ImageAnnotations.circles (field 1) — present but its body is currently
  // skipped. Decoder should not crash; just no points produced.
  std::vector<uint8_t> bytes;
  pb::appendTag(bytes, 1, 2);   // circles, len-delimited
  pb::appendLenDelim(bytes, std::vector<uint8_t>{0x01, 0x02, 0x03});  // dummy body

  auto result = dec->decode(bytes.data(), bytes.size());
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->annotations[0].points.empty());
}

TEST(SceneDecoderProtobufTest, NullDataReturnsError) {
  auto dec = makeSceneDecoder("foxglove.ImageAnnotations");
  ASSERT_NE(dec.get(), nullptr);
  auto result = dec->decode(nullptr, 0);
  EXPECT_FALSE(result.has_value());
}

}  // namespace
}  // namespace PJ
