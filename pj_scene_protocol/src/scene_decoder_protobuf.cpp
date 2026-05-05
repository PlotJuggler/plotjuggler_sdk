#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "pj_scene_protocol/scene_decoder.h"

namespace PJ {
namespace {

// Minimal Protobuf wire-format reader for foxglove.ImageAnnotations. Decodes
// PointsAnnotation, CircleAnnotation, and TextAnnotation in full. Round-trips
// against the sibling writer at `src/image_annotation_codec.cpp` are covered
// by `tests/image_annotation_codec_test.cpp`.
//
// Spec reference: https://protobuf.dev/programming-guides/encoding/
// Wire types we need: VARINT(0), I64(1), LEN(2). I32(5) skipped if encountered.
//
// Foxglove schemas (https://github.com/foxglove/schemas, foxglove/proto/):
//   ImageAnnotations { circles=1, points=2, texts=3 }
//   PointsAnnotation { timestamp=1, type=2 (enum: 0/1/2/3/4),
//                      points=3 (repeated Point2),
//                      outline_color=4, outline_colors=5,
//                      fill_color=6, thickness=7 }
//   Point2 { x=1, y=2 }   (both double)
//   Time   { sec=1 (int64), nanosec=2 (uint32) }

struct Reader {
  const uint8_t* p;
  const uint8_t* end;

  bool eof() const noexcept {
    return p >= end;
  }
  size_t remaining() const noexcept {
    return static_cast<size_t>(end - p);
  }

  // Returns false on overflow / end-of-buffer.
  bool readVarint(uint64_t& out) {
    out = 0;
    int shift = 0;
    while (p < end) {
      uint8_t b = *p++;
      out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
      if ((b & 0x80u) == 0) {
        return true;
      }
      shift += 7;
      if (shift > 63) {
        return false;
      }
    }
    return false;
  }

  bool readFixed64(uint64_t& out) {
    if (remaining() < 8) {
      return false;
    }
    std::memcpy(&out, p, 8);  // little-endian on x86_64; protobuf is also LE
    p += 8;
    return true;
  }

  bool readDouble(double& out) {
    uint64_t bits = 0;
    if (!readFixed64(bits)) {
      return false;
    }
    std::memcpy(&out, &bits, 8);
    return true;
  }

  // Skip a single field given its wire type. Advances p past the field body.
  bool skipField(uint32_t wire_type) {
    switch (wire_type) {
      case 0: {  // VARINT
        uint64_t dummy = 0;
        return readVarint(dummy);
      }
      case 1: {  // I64
        if (remaining() < 8) {
          return false;
        }
        p += 8;
        return true;
      }
      case 2: {  // LEN
        uint64_t len = 0;
        if (!readVarint(len)) {
          return false;
        }
        if (len > remaining()) {
          return false;
        }
        p += len;
        return true;
      }
      case 5: {  // I32
        if (remaining() < 4) {
          return false;
        }
        p += 4;
        return true;
      }
      default:
        return false;  // groups (3/4) deprecated, not expected
    }
  }
};

// Map Foxglove PointsAnnotation.Type enum values to our AnnotationTopology.
AnnotationTopology mapTopology(uint64_t type) {
  switch (type) {
    case 1:
      return AnnotationTopology::kPoints;
    case 2:
      return AnnotationTopology::kLineLoop;
    case 3:
      return AnnotationTopology::kLineStrip;
    case 4:
      return AnnotationTopology::kLineList;
    case 0:
    default:
      return AnnotationTopology::kPoints;  // UNKNOWN → safe default
  }
}

// Decode a Point2 sub-message: {1: double x, 2: double y}.
bool decodePoint2(Reader& r, size_t len, Point2& out) {
  const uint8_t* sub_end = r.p + len;
  if (sub_end > r.end) {
    return false;
  }
  while (r.p < sub_end) {
    uint64_t tag = 0;
    if (!r.readVarint(tag)) {
      return false;
    }
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7u);
    if (field == 1 && wire == 1) {
      if (!r.readDouble(out.x)) {
        return false;
      }
    } else if (field == 2 && wire == 1) {
      if (!r.readDouble(out.y)) {
        return false;
      }
    } else {
      if (!r.skipField(wire)) {
        return false;
      }
    }
  }
  return true;
}

// Decode a foxglove.Color sub-message: {1: double r, 2: double g, 3: double b, 4: double a}
// with components in [0, 1]. Output is uint8 RGBA in [0, 255].
bool decodeColor(Reader& r, size_t len, ColorRGBA& out) {
  const uint8_t* sub_end = r.p + len;
  if (sub_end > r.end) {
    return false;
  }
  double rd = 0.0;
  double gd = 0.0;
  double bd = 0.0;
  double ad = 1.0;
  while (r.p < sub_end) {
    uint64_t tag = 0;
    if (!r.readVarint(tag)) {
      return false;
    }
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7u);
    if (wire == 1 && field >= 1 && field <= 4) {
      double v = 0.0;
      if (!r.readDouble(v)) {
        return false;
      }
      switch (field) {
        case 1:
          rd = v;
          break;
        case 2:
          gd = v;
          break;
        case 3:
          bd = v;
          break;
        case 4:
          ad = v;
          break;
        default:
          break;
      }
    } else {
      if (!r.skipField(wire)) {
        return false;
      }
    }
  }
  auto to_byte = [](double v) {
    if (v < 0.0) {
      v = 0.0;
    }
    if (v > 1.0) {
      v = 1.0;
    }
    return static_cast<uint8_t>(v * 255.0 + 0.5);
  };
  out.r = to_byte(rd);
  out.g = to_byte(gd);
  out.b = to_byte(bd);
  out.a = to_byte(ad);
  return true;
}

// Decode one PointsAnnotation sub-message.
bool decodePointsAnnotation(Reader& r, size_t len, PointsAnnotation& out) {
  const uint8_t* sub_end = r.p + len;
  if (sub_end > r.end) {
    return false;
  }
  while (r.p < sub_end) {
    uint64_t tag = 0;
    if (!r.readVarint(tag)) {
      return false;
    }
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7u);

    if (field == 2 && wire == 0) {
      uint64_t type_val = 0;
      if (!r.readVarint(type_val)) {
        return false;
      }
      out.topology = mapTopology(type_val);
    } else if (field == 3 && wire == 2) {
      uint64_t pt_len = 0;
      if (!r.readVarint(pt_len)) {
        return false;
      }
      Point2 pt;
      if (!decodePoint2(r, pt_len, pt)) {
        return false;
      }
      out.points.push_back(pt);
    } else if (field == 4 && wire == 2) {
      uint64_t c_len = 0;
      if (!r.readVarint(c_len)) {
        return false;
      }
      if (!decodeColor(r, c_len, out.color)) {
        return false;
      }
    } else if (field == 5 && wire == 2) {
      uint64_t c_len = 0;
      if (!r.readVarint(c_len)) {
        return false;
      }
      ColorRGBA c{};
      if (!decodeColor(r, c_len, c)) {
        return false;
      }
      out.colors.push_back(c);
    } else if (field == 6 && wire == 2) {
      uint64_t c_len = 0;
      if (!r.readVarint(c_len)) {
        return false;
      }
      if (!decodeColor(r, c_len, out.fill_color)) {
        return false;
      }
    } else if (field == 7 && wire == 1) {
      if (!r.readDouble(out.thickness)) {
        return false;
      }
    } else {
      if (!r.skipField(wire)) {
        return false;
      }
    }
  }
  return true;
}

// Decode one foxglove.CircleAnnotation sub-message:
//   timestamp(1)=Time, position(2)=Point2, diameter(3)=double, thickness(4)=double,
//   fill_color(5)=Color, outline_color(6)=Color
// We map diameter/2 -> radius and outline_color -> color (the C++ struct has no
// separate outline field; .color IS the outline).
bool decodeCircleAnnotation(Reader& r, size_t len, CircleAnnotation& out) {
  const uint8_t* sub_end = r.p + len;
  if (sub_end > r.end) {
    return false;
  }
  // Defaults match pj_scene_protocol/image_annotation.h.
  out.color = {0, 255, 0, 255};
  out.fill_color = {0, 0, 0, 0};
  out.thickness = 2.0;
  out.radius = 1.0;
  while (r.p < sub_end) {
    uint64_t tag = 0;
    if (!r.readVarint(tag)) {
      return false;
    }
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7u);
    if (field == 2 && wire == 2) {
      uint64_t p_len = 0;
      if (!r.readVarint(p_len)) {
        return false;
      }
      if (!decodePoint2(r, p_len, out.center)) {
        return false;
      }
    } else if (field == 3 && wire == 1) {
      double diameter = 0.0;
      if (!r.readDouble(diameter)) {
        return false;
      }
      out.radius = diameter * 0.5;
    } else if (field == 4 && wire == 1) {
      if (!r.readDouble(out.thickness)) {
        return false;
      }
    } else if (field == 5 && wire == 2) {
      uint64_t c_len = 0;
      if (!r.readVarint(c_len)) {
        return false;
      }
      if (!decodeColor(r, c_len, out.fill_color)) {
        return false;
      }
    } else if (field == 6 && wire == 2) {
      uint64_t c_len = 0;
      if (!r.readVarint(c_len)) {
        return false;
      }
      if (!decodeColor(r, c_len, out.color)) {
        return false;
      }
    } else {
      if (!r.skipField(wire)) {
        return false;
      }
    }
  }
  return true;
}

// Decode one foxglove.TextAnnotation sub-message:
//   timestamp(1)=Time, position(2)=Point2, text(3)=string, font_size(4)=double,
//   text_color(5)=Color, background_color(6)=Color  (background_color skipped — not
//   present in pj_scene_protocol/image_annotation.h::TextAnnotation).
bool decodeTextAnnotation(Reader& r, size_t len, TextAnnotation& out) {
  const uint8_t* sub_end = r.p + len;
  if (sub_end > r.end) {
    return false;
  }
  out.color = {255, 255, 255, 255};
  out.font_size = 14.0;
  while (r.p < sub_end) {
    uint64_t tag = 0;
    if (!r.readVarint(tag)) {
      return false;
    }
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7u);
    if (field == 2 && wire == 2) {
      uint64_t p_len = 0;
      if (!r.readVarint(p_len)) {
        return false;
      }
      if (!decodePoint2(r, p_len, out.position)) {
        return false;
      }
    } else if (field == 3 && wire == 2) {
      uint64_t s_len = 0;
      if (!r.readVarint(s_len)) {
        return false;
      }
      if (s_len > r.remaining()) {
        return false;
      }
      out.text.assign(reinterpret_cast<const char*>(r.p), static_cast<size_t>(s_len));
      r.p += s_len;
    } else if (field == 4 && wire == 1) {
      if (!r.readDouble(out.font_size)) {
        return false;
      }
    } else if (field == 5 && wire == 2) {
      uint64_t c_len = 0;
      if (!r.readVarint(c_len)) {
        return false;
      }
      if (!decodeColor(r, c_len, out.color)) {
        return false;
      }
    } else {
      if (!r.skipField(wire)) {
        return false;
      }
    }
  }
  return true;
}

// Decode the top-level ImageAnnotations message.
class ProtobufImageAnnotationsDecoder final : public ISceneDecoder {
 public:
  Expected<SceneFrame> decode(const uint8_t* data, size_t size) override {
    if (data == nullptr || size == 0) {
      return unexpected(std::string("Protobuf ImageAnnotations: empty buffer"));
    }
    Reader r{data, data + size};

    ImageAnnotation ia;
    while (!r.eof()) {
      uint64_t tag = 0;
      if (!r.readVarint(tag)) {
        return unexpected(std::string("Protobuf ImageAnnotations: bad tag"));
      }
      uint32_t field = static_cast<uint32_t>(tag >> 3);
      uint32_t wire = static_cast<uint32_t>(tag & 0x7u);

      if (field == 2 && wire == 2) {
        uint64_t pa_len = 0;
        if (!r.readVarint(pa_len)) {
          return unexpected(std::string("Protobuf ImageAnnotations: bad PointsAnnotation length"));
        }
        PointsAnnotation pa;
        pa.color = {0, 255, 0, 255};
        pa.thickness = 2.0;
        if (!decodePointsAnnotation(r, pa_len, pa)) {
          return unexpected(std::string("Protobuf ImageAnnotations: PointsAnnotation decode failed"));
        }
        ia.points.push_back(std::move(pa));
      } else if (field == 1 && wire == 2) {
        uint64_t ca_len = 0;
        if (!r.readVarint(ca_len)) {
          return unexpected(std::string("Protobuf ImageAnnotations: bad CircleAnnotation length"));
        }
        CircleAnnotation ca;
        if (!decodeCircleAnnotation(r, ca_len, ca)) {
          return unexpected(std::string("Protobuf ImageAnnotations: CircleAnnotation decode failed"));
        }
        ia.circles.push_back(std::move(ca));
      } else if (field == 3 && wire == 2) {
        uint64_t ta_len = 0;
        if (!r.readVarint(ta_len)) {
          return unexpected(std::string("Protobuf ImageAnnotations: bad TextAnnotation length"));
        }
        TextAnnotation ta;
        if (!decodeTextAnnotation(r, ta_len, ta)) {
          return unexpected(std::string("Protobuf ImageAnnotations: TextAnnotation decode failed"));
        }
        ia.texts.push_back(std::move(ta));
      } else {
        if (!r.skipField(wire)) {
          return unexpected(std::string("Protobuf ImageAnnotations: skip failed"));
        }
      }
    }

    SceneFrame sf;
    sf.annotations.push_back(std::move(ia));
    return sf;
  }
};

}  // namespace

std::unique_ptr<ISceneDecoder> makeSceneDecoderProtobufImageAnnotations() {
  return std::make_unique<ProtobufImageAnnotationsDecoder>();
}

}  // namespace PJ
