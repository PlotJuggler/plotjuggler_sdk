#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "pj_media_core/scene_decoder.h"

namespace PJ {
namespace {

// Minimal Protobuf wire-format reader for foxglove.ImageAnnotations.
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
//
// MVP: decodes PointsAnnotation fully (geometry + topology). CircleAnnotation
// and TextAnnotation are recognized as fields but their bodies are skipped —
// rendering for those primitives is deferred to a later phase.

struct Reader {
  const uint8_t* p;
  const uint8_t* end;

  bool eof() const noexcept { return p >= end; }
  size_t remaining() const noexcept { return static_cast<size_t>(end - p); }

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
        if (remaining() < 8) return false;
        p += 8;
        return true;
      }
      case 2: {  // LEN
        uint64_t len = 0;
        if (!readVarint(len)) return false;
        if (len > remaining()) return false;
        p += len;
        return true;
      }
      case 5: {  // I32
        if (remaining() < 4) return false;
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
    case 1: return AnnotationTopology::kPoints;
    case 2: return AnnotationTopology::kLineLoop;
    case 3: return AnnotationTopology::kLineStrip;
    case 4: return AnnotationTopology::kLineList;
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
    if (!r.readVarint(tag)) return false;
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7u);
    if (field == 1 && wire == 1) {
      if (!r.readDouble(out.x)) return false;
    } else if (field == 2 && wire == 1) {
      if (!r.readDouble(out.y)) return false;
    } else {
      if (!r.skipField(wire)) return false;
    }
  }
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
    if (!r.readVarint(tag)) return false;
    uint32_t field = static_cast<uint32_t>(tag >> 3);
    uint32_t wire = static_cast<uint32_t>(tag & 0x7u);

    if (field == 2 && wire == 0) {
      uint64_t type_val = 0;
      if (!r.readVarint(type_val)) return false;
      out.topology = mapTopology(type_val);
    } else if (field == 3 && wire == 2) {
      uint64_t pt_len = 0;
      if (!r.readVarint(pt_len)) return false;
      Point2 pt;
      if (!decodePoint2(r, pt_len, pt)) return false;
      out.points.push_back(pt);
    } else if (field == 7 && wire == 1) {
      if (!r.readDouble(out.thickness)) return false;
    } else {
      if (!r.skipField(wire)) return false;
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
      } else {
        // CircleAnnotation (field 1) and TextAnnotation (field 3) are skipped
        // for this iteration — they are decoded but not yet rendered.
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
