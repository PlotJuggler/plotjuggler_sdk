#include "pj_base/builtin/image_annotations_codec.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "protobuf_wire.h"

namespace PJ {
namespace {

using builtin_wire::Reader;
using builtin_wire::Tag;
using builtin_wire::WireType;
using builtin_wire::Writer;
using sdk::AnnotationTopology;
using sdk::CircleAnnotation;
using sdk::ColorRGBA;
using sdk::ImageAnnotations;
using sdk::Point2;
using sdk::PointsAnnotation;
using sdk::TextAnnotation;

void writePoint2(Writer& writer, const Point2& point) {
  writer.doubleField(1, point.x);
  writer.doubleField(2, point.y);
}

void writeColor(Writer& writer, const ColorRGBA& color) {
  writer.doubleField(1, static_cast<double>(color.r) / 255.0);
  writer.doubleField(2, static_cast<double>(color.g) / 255.0);
  writer.doubleField(3, static_cast<double>(color.b) / 255.0);
  writer.doubleField(4, static_cast<double>(color.a) / 255.0);
}

uint32_t topologyToEnum(AnnotationTopology topology) {
  switch (topology) {
    case AnnotationTopology::kPoints:
      return 1;
    case AnnotationTopology::kLineLoop:
      return 2;
    case AnnotationTopology::kLineStrip:
      return 3;
    case AnnotationTopology::kLineList:
      return 4;
  }
  return 1;
}

void writePointsAnnotation(Writer& writer, const PointsAnnotation& points) {
  writer.varint(2, topologyToEnum(points.topology));

  for (const auto& point : points.points) {
    writer.message(3, [&](Writer& nested) { writePoint2(nested, point); });
  }

  writer.message(4, [&](Writer& nested) { writeColor(nested, points.color); });

  for (const auto& color : points.colors) {
    writer.message(5, [&](Writer& nested) { writeColor(nested, color); });
  }

  writer.message(6, [&](Writer& nested) { writeColor(nested, points.fill_color); });
  writer.doubleField(7, points.thickness);
}

void writeCircleAnnotation(Writer& writer, const CircleAnnotation& circle) {
  writer.message(2, [&](Writer& nested) { writePoint2(nested, circle.center); });
  writer.doubleField(3, circle.radius * 2.0);
  writer.doubleField(4, circle.thickness);
  writer.message(5, [&](Writer& nested) { writeColor(nested, circle.fill_color); });
  writer.message(6, [&](Writer& nested) { writeColor(nested, circle.color); });
}

void writeTextAnnotation(Writer& writer, const TextAnnotation& text) {
  writer.message(2, [&](Writer& nested) { writePoint2(nested, text.position); });
  writer.string(3, text.text);
  writer.doubleField(4, text.font_size);
  writer.message(5, [&](Writer& nested) { writeColor(nested, text.color); });
}

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
      return AnnotationTopology::kPoints;
  }
}

uint8_t normalizedToByte(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return static_cast<uint8_t>(value * 255.0 + 0.5);
}

bool decodePoint2(Reader& reader, Point2& out) {
  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    if (tag.field == 1 && tag.type == WireType::kFixed64) {
      if (!reader.readDouble(out.x)) {
        return false;
      }
    } else if (tag.field == 2 && tag.type == WireType::kFixed64) {
      if (!reader.readDouble(out.y)) {
        return false;
      }
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool decodeColor(Reader& reader, ColorRGBA& out) {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  double a = 1.0;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    if (tag.type == WireType::kFixed64 && tag.field >= 1 && tag.field <= 4) {
      double value = 0.0;
      if (!reader.readDouble(value)) {
        return false;
      }
      switch (tag.field) {
        case 1:
          r = value;
          break;
        case 2:
          g = value;
          break;
        case 3:
          b = value;
          break;
        case 4:
          a = value;
          break;
        default:
          break;
      }
    } else if (!reader.skip(tag.type)) {
      return false;
    }
  }

  out = {normalizedToByte(r), normalizedToByte(g), normalizedToByte(b), normalizedToByte(a)};
  return true;
}

bool readPoint2Message(Reader& reader, Point2& out) {
  Reader nested;
  return reader.readMessage(nested) && decodePoint2(nested, out);
}

bool readColorMessage(Reader& reader, ColorRGBA& out) {
  Reader nested;
  return reader.readMessage(nested) && decodeColor(nested, out);
}

bool decodePointsAnnotation(Reader& reader, PointsAnnotation& out) {
  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    switch (tag.field) {
      case 2: {
        if (tag.type != WireType::kVarint) {
          break;
        }
        uint64_t value = 0;
        if (!reader.readVarint(value)) {
          return false;
        }
        out.topology = mapTopology(value);
        continue;
      }
      case 3: {
        if (tag.type != WireType::kLengthDelimited) {
          break;
        }
        Point2 point;
        if (!readPoint2Message(reader, point)) {
          return false;
        }
        out.points.push_back(point);
        continue;
      }
      case 4:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.color)) {
            return false;
          }
          continue;
        }
        break;
      case 5: {
        if (tag.type != WireType::kLengthDelimited) {
          break;
        }
        ColorRGBA color;
        if (!readColorMessage(reader, color)) {
          return false;
        }
        out.colors.push_back(color);
        continue;
      }
      case 6:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.fill_color)) {
            return false;
          }
          continue;
        }
        break;
      case 7:
        if (tag.type == WireType::kFixed64) {
          if (!reader.readDouble(out.thickness)) {
            return false;
          }
          continue;
        }
        break;
      default:
        break;
    }

    if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool decodeCircleAnnotation(Reader& reader, CircleAnnotation& out) {
  out.color = {0, 255, 0, 255};
  out.fill_color = {0, 0, 0, 0};
  out.thickness = 2.0;
  out.radius = 1.0;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    switch (tag.field) {
      case 2:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readPoint2Message(reader, out.center)) {
            return false;
          }
          continue;
        }
        break;
      case 3: {
        if (tag.type != WireType::kFixed64) {
          break;
        }
        double diameter = 0.0;
        if (!reader.readDouble(diameter)) {
          return false;
        }
        out.radius = diameter * 0.5;
        continue;
      }
      case 4:
        if (tag.type == WireType::kFixed64) {
          if (!reader.readDouble(out.thickness)) {
            return false;
          }
          continue;
        }
        break;
      case 5:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.fill_color)) {
            return false;
          }
          continue;
        }
        break;
      case 6:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.color)) {
            return false;
          }
          continue;
        }
        break;
      default:
        break;
    }

    if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

bool decodeTextAnnotation(Reader& reader, TextAnnotation& out) {
  out.color = {255, 255, 255, 255};
  out.font_size = 14.0;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return false;
    }

    switch (tag.field) {
      case 2:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readPoint2Message(reader, out.position)) {
            return false;
          }
          continue;
        }
        break;
      case 3:
        if (tag.type == WireType::kLengthDelimited) {
          if (!reader.readString(out.text)) {
            return false;
          }
          continue;
        }
        break;
      case 4:
        if (tag.type == WireType::kFixed64) {
          if (!reader.readDouble(out.font_size)) {
            return false;
          }
          continue;
        }
        break;
      case 5:
        if (tag.type == WireType::kLengthDelimited) {
          if (!readColorMessage(reader, out.color)) {
            return false;
          }
          continue;
        }
        break;
      default:
        break;
    }

    if (!reader.skip(tag.type)) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::vector<uint8_t> serializeImageAnnotations(const ImageAnnotations& annotations) {
  std::vector<uint8_t> out;
  Writer writer(out);

  for (const auto& circle : annotations.circles) {
    writer.message(1, [&](Writer& nested) { writeCircleAnnotation(nested, circle); });
  }
  for (const auto& points : annotations.points) {
    writer.message(2, [&](Writer& nested) { writePointsAnnotation(nested, points); });
  }
  for (const auto& text : annotations.texts) {
    writer.message(3, [&](Writer& nested) { writeTextAnnotation(nested, text); });
  }

  return out;
}

Expected<sdk::ImageAnnotations> deserializeImageAnnotations(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return unexpected(std::string("ImageAnnotations wire: empty buffer"));
  }

  Reader reader(data, size);
  sdk::ImageAnnotations annotations;

  while (!reader.eof()) {
    Tag tag;
    if (!reader.readTag(tag)) {
      return unexpected(std::string("ImageAnnotations wire: bad tag"));
    }

    if (tag.type != WireType::kLengthDelimited) {
      if (!reader.skip(tag.type)) {
        return unexpected(std::string("ImageAnnotations wire: skip failed"));
      }
      continue;
    }

    Reader nested;
    if (!reader.readMessage(nested)) {
      return unexpected(std::string("ImageAnnotations wire: bad nested message length"));
    }

    switch (tag.field) {
      case 1: {
        CircleAnnotation circle;
        if (!decodeCircleAnnotation(nested, circle)) {
          return unexpected(std::string("ImageAnnotations wire: CircleAnnotation decode failed"));
        }
        annotations.circles.push_back(std::move(circle));
        break;
      }
      case 2: {
        PointsAnnotation points;
        points.color = {0, 255, 0, 255};
        points.thickness = 2.0;
        if (!decodePointsAnnotation(nested, points)) {
          return unexpected(std::string("ImageAnnotations wire: PointsAnnotation decode failed"));
        }
        annotations.points.push_back(std::move(points));
        break;
      }
      case 3: {
        TextAnnotation text;
        if (!decodeTextAnnotation(nested, text)) {
          return unexpected(std::string("ImageAnnotations wire: TextAnnotation decode failed"));
        }
        annotations.texts.push_back(std::move(text));
        break;
      }
      default:
        break;
    }
  }

  return annotations;
}

}  // namespace PJ
