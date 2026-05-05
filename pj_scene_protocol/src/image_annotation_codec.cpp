#include "pj_scene_protocol/image_annotation_codec.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace PJ {
namespace {

// Hand-rolled Protobuf wire emission. Mirror of the reader at
// `src/scene_decoder_protobuf.cpp` (same module).
//
// Wire format spec: https://protobuf.dev/programming-guides/encoding/
// Wire types we emit: VARINT(0), I64(1), LEN(2). I32(5) is unused here.
//
// Sub-messages are length-delimited: write the body to a scratch buffer, then
// write the parent's tag + length + body. Bodies are bounded (≤ a few hundred
// bytes for typical annotations), so the extra allocation is fine.

void writeVarint(std::vector<uint8_t>& out, uint64_t v) {
  while (v >= 0x80u) {
    out.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
    v >>= 7;
  }
  out.push_back(static_cast<uint8_t>(v));
}

void writeTag(std::vector<uint8_t>& out, uint32_t field, uint32_t wire) {
  writeVarint(out, (static_cast<uint64_t>(field) << 3) | (wire & 0x7u));
}

void writeDouble(std::vector<uint8_t>& out, double v) {
  uint64_t bits = 0;
  std::memcpy(&bits, &v, 8);
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu));
  }
}

void writeLenDelim(std::vector<uint8_t>& out, const std::vector<uint8_t>& body) {
  writeVarint(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
}

void writeString(std::vector<uint8_t>& out, std::string_view s) {
  writeVarint(out, s.size());
  out.insert(out.end(), s.begin(), s.end());
}

// foxglove.Point2 { 1: double x, 2: double y }
std::vector<uint8_t> buildPoint2(const Point2& p) {
  std::vector<uint8_t> body;
  writeTag(body, 1, 1);
  writeDouble(body, p.x);
  writeTag(body, 2, 1);
  writeDouble(body, p.y);
  return body;
}

// foxglove.Color { 1: double r, 2: double g, 3: double b, 4: double a }
// Components in [0, 1]. We hold uint8 [0, 255] and convert by v/255.0.
std::vector<uint8_t> buildColor(const ColorRGBA& c) {
  std::vector<uint8_t> body;
  writeTag(body, 1, 1);
  writeDouble(body, static_cast<double>(c.r) / 255.0);
  writeTag(body, 2, 1);
  writeDouble(body, static_cast<double>(c.g) / 255.0);
  writeTag(body, 3, 1);
  writeDouble(body, static_cast<double>(c.b) / 255.0);
  writeTag(body, 4, 1);
  writeDouble(body, static_cast<double>(c.a) / 255.0);
  return body;
}

// AnnotationTopology -> Foxglove enum. Inverse of `mapTopology` in the reader.
// kPoints=1, kLineLoop=2, kLineStrip=3, kLineList=4. The Foxglove enum reserves
// 0 for UNKNOWN; we never emit 0.
uint32_t topologyToEnum(AnnotationTopology t) {
  switch (t) {
    case AnnotationTopology::kPoints:
      return 1;
    case AnnotationTopology::kLineLoop:
      return 2;
    case AnnotationTopology::kLineStrip:
      return 3;
    case AnnotationTopology::kLineList:
      return 4;
  }
  return 1;  // unreachable; defensive
}

// foxglove.PointsAnnotation
//   { 2: type (varint enum), 3: repeated Point2, 4: outline_color,
//     5: repeated outline_colors, 6: fill_color, 7: thickness (double) }
std::vector<uint8_t> buildPointsAnnotation(const PointsAnnotation& pa) {
  std::vector<uint8_t> body;

  writeTag(body, 2, 0);
  writeVarint(body, topologyToEnum(pa.topology));

  for (const auto& pt : pa.points) {
    writeTag(body, 3, 2);
    writeLenDelim(body, buildPoint2(pt));
  }

  writeTag(body, 4, 2);
  writeLenDelim(body, buildColor(pa.color));

  // Per-vertex colors: emit one field-5 entry per element. An empty `colors`
  // vector emits zero entries — critical, because the reader pushes_back on
  // every field-5 occurrence, so emitting an empty Color would smuggle a
  // default-constructed entry into out.colors.
  for (const auto& c : pa.colors) {
    writeTag(body, 5, 2);
    writeLenDelim(body, buildColor(c));
  }

  writeTag(body, 6, 2);
  writeLenDelim(body, buildColor(pa.fill_color));

  writeTag(body, 7, 1);
  writeDouble(body, pa.thickness);

  return body;
}

// foxglove.CircleAnnotation
//   { 2: position (Point2), 3: diameter (double), 4: thickness (double),
//     5: fill_color, 6: outline_color }
// Note: our struct holds `radius`; we emit `diameter = radius * 2`.
std::vector<uint8_t> buildCircleAnnotation(const CircleAnnotation& ca) {
  std::vector<uint8_t> body;

  writeTag(body, 2, 2);
  writeLenDelim(body, buildPoint2(ca.center));

  writeTag(body, 3, 1);
  writeDouble(body, ca.radius * 2.0);

  writeTag(body, 4, 1);
  writeDouble(body, ca.thickness);

  writeTag(body, 5, 2);
  writeLenDelim(body, buildColor(ca.fill_color));

  writeTag(body, 6, 2);
  writeLenDelim(body, buildColor(ca.color));

  return body;
}

// foxglove.TextAnnotation
//   { 2: position (Point2), 3: text (string), 4: font_size (double),
//     5: text_color }
// background_color (field 6) is intentionally NOT emitted — the C++ struct has
// no equivalent field. The reader skips it on read.
std::vector<uint8_t> buildTextAnnotation(const TextAnnotation& ta) {
  std::vector<uint8_t> body;

  writeTag(body, 2, 2);
  writeLenDelim(body, buildPoint2(ta.position));

  writeTag(body, 3, 2);
  writeString(body, ta.text);

  writeTag(body, 4, 1);
  writeDouble(body, ta.font_size);

  writeTag(body, 5, 2);
  writeLenDelim(body, buildColor(ta.color));

  return body;
}

}  // namespace

// foxglove.ImageAnnotations { 1: repeated CircleAnnotation,
//                             2: repeated PointsAnnotation,
//                             3: repeated TextAnnotation }
std::vector<uint8_t> serializeImageAnnotation(const ImageAnnotation& ia) {
  std::vector<uint8_t> out;

  for (const auto& c : ia.circles) {
    writeTag(out, 1, 2);
    writeLenDelim(out, buildCircleAnnotation(c));
  }
  for (const auto& p : ia.points) {
    writeTag(out, 2, 2);
    writeLenDelim(out, buildPointsAnnotation(p));
  }
  for (const auto& t : ia.texts) {
    writeTag(out, 3, 2);
    writeLenDelim(out, buildTextAnnotation(t));
  }

  return out;
}

}  // namespace PJ
