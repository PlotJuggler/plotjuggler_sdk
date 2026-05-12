/**
 * @file canonical_object.hpp
 * @brief Canonical object types produced by MessageParser plugins and consumed
 *        by widgets and toolboxes.
 *
 * This header defines the vocabulary that bridges parser plugins (which
 * understand wire formats: ROS, Foxglove, Protobuf, etc.) and consumer code
 * (widgets, toolboxes) that renders or processes the result. The ObjectStore
 * itself remains agnostic to these types — it stores opaque bytes; the
 * decoding into a CanonicalObject happens in the consumer at pull time, by
 * invoking the parser's parseObject() against the bytes.
 *
 * Reference report: docs/claude_reports/2026.05.07-arquitectura-objectstore-pipeline-misalignment.md
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

// -----------------------------------------------------------------------------
// Schema classification
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Buffer anchor — type-erased ownership token shared between a payload buffer
// and any non-owning views derived from it. Carries no data, only keeps the
// underlying allocation alive while at least one anchor copy exists. Concrete
// typical type erased here is std::shared_ptr<std::vector<uint8_t>>; consumers
// never need to know.
// -----------------------------------------------------------------------------

using BufferAnchor = std::shared_ptr<const void>;

/// Non-owning view + ownership anchor of a payload buffer. Used by the host
/// to hand a parser a message payload without committing to a copy: the parser
/// reads `bytes` and, in the canonical object it returns, may keep a Span into
/// the same memory plus a copy of `anchor` so the bytes outlive the parse call.
///
/// `anchor` may be empty when the caller does not share ownership — in that
/// case the parser must materialize any bytes it wants to retain (the C ABI
/// trampoline path is the typical case; in-process direct calls are expected
/// to provide a non-empty anchor).
struct PayloadView {
  Span<const uint8_t> bytes;
  BufferAnchor anchor;
};

/// What kind of canonical object a parser produces for a given schema.
/// Returned a priori (without parsing payload) by classifySchema(). kNone means
/// the parser only produces scalars for the Datastore — no ObjectTopic to
/// register.
enum class CanonicalObjectKind : uint16_t {
  kNone = 0,
  kImage = 1,            ///< sdk::Image — pixels already in canonical PixelFormat.
  kCompressedImage = 2,  ///< sdk::CompressedImage — JPEG/PNG/QOI bytes, undecoded.
  kPointCloud = 3,       ///< sdk::PointCloud — packed points + per-channel field layout.
  // Reserved for future kinds; keep numeric values stable across releases.
  // kMarkers = 4,
  // kOccupancyGrid = 5,
};

/// A priori classification of a schema, returned by MessageParser::classifySchema().
/// Currently a single field; struct (vs raw enum) leaves room to attach
/// declarative metadata later (preferred cache size, expected rate, etc.) without
/// breaking the API. What deliberately does NOT belong here: parse cost hints
/// (the DataSource knows the payload size), retention policy, eager/lazy choice.
struct SchemaClassification {
  CanonicalObjectKind object_kind = CanonicalObjectKind::kNone;
};

// -----------------------------------------------------------------------------
// Pixel formats — canonical for sdk::Image
// -----------------------------------------------------------------------------

/// Canonical pixel format for sdk::Image. The buffer may include row padding
/// (sdk::Image::row_step >= width * bytesPerPixel(format)); consumers must
/// honor row_step rather than assuming tightly-packed.
///
/// Both R-G-B and B-G-R orderings are first-class citizens. ROS bgr8/bgra8
/// (and many machine-vision sources) deliver bytes in B-G-R order natively;
/// keeping the byte order in the format tag (instead of swizzling at parse
/// time) lets the consumer hand bytes straight to a renderer that supports
/// GL_BGR / GL_BGRA texture uploads — zero-copy all the way.
///
/// Note: pj_scene2D (and other consumers) currently define their own pixel
/// format. Harmonizing on this canonical enum is part of consumer-side
/// migration; this header defines the SDK-level vocabulary.
enum class PixelFormat : uint16_t {
  kUnknown = 0,
  kRGB888 = 1,    ///< 3 bytes/pixel, R-G-B order.
  kRGBA8888 = 2,  ///< 4 bytes/pixel, R-G-B-A order.
  kMono8 = 3,     ///< 1 byte/pixel, grayscale.
  kMono16 = 4,    ///< 2 bytes/pixel, grayscale (depth, etc.); see is_bigendian.
  kBGR888 = 5,    ///< 3 bytes/pixel, B-G-R order (ROS bgr8, OpenCV native).
  kBGRA8888 = 6,  ///< 4 bytes/pixel, B-G-R-A order (ROS bgra8).
};

/// Bytes per pixel for a given format. Returns 0 for kUnknown.
[[nodiscard]] constexpr uint32_t bytesPerPixel(PixelFormat format) noexcept {
  switch (format) {
    case PixelFormat::kRGB888:
    case PixelFormat::kBGR888:
      return 3;
    case PixelFormat::kRGBA8888:
    case PixelFormat::kBGRA8888:
      return 4;
    case PixelFormat::kMono8:
      return 1;
    case PixelFormat::kMono16:
      return 2;
    case PixelFormat::kUnknown:
      return 0;
  }
  return 0;
}

// -----------------------------------------------------------------------------
// sdk::Image — already-decoded image
// -----------------------------------------------------------------------------

/// Image already decoded into a canonical pixel format. If the producer
/// (parser) returns this, the consumer can upload the pixels directly to a
/// renderer (QRhi or otherwise) without going through any codec.
///
/// Layout: `pixels` is a non-owning view of size at least `row_step * height`.
/// `row_step` may exceed `width * bytesPerPixel(pixel_format)` when the wire
/// format included per-row padding; consumers must honor it. `anchor` keeps
/// the underlying buffer alive — the parser may have made `pixels` a view
/// into the source payload (zero-copy) or into a freshly-allocated vector
/// (when the wire format required conversion); consumers don't need to know
/// which.
///
/// For mono16 buffers `is_bigendian` indicates the byte order of each sample;
/// otherwise it is unused. RGB/BGR ordering is encoded in `pixel_format`.
struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat pixel_format = PixelFormat::kUnknown;
  uint32_t row_step = 0;
  bool is_bigendian = false;
  Span<const uint8_t> pixels;
  BufferAnchor anchor;
  Timestamp timestamp_ns = 0;
};

// -----------------------------------------------------------------------------
// sdk::CompressedImage — undecoded compressed image bytes
// -----------------------------------------------------------------------------

/// Image still in compressed wire format (JPEG/PNG/QOI). The consumer is
/// expected to run it through the appropriate codec (pj_scene2D::JpegCodec,
/// PngCodec, etc.) to obtain an sdk::Image.
///
/// The parser does NOT decompress: it only extracts the compressed payload
/// from whatever wrapper the wire format used (CDR for ROS2, etc.) and tags it
/// with the format.
struct CompressedImage {
  enum class Format : uint8_t {
    kUnknown = 0,
    kJPEG = 1,
    kPNG = 2,
    kQOI = 3,
  };

  /// Auxiliary metadata that some wrappers attach to the compressed bytes
  /// and that the consumer needs to decode correctly. The parser fills the
  /// fields it can; consumers ignore those they don't care about.
  struct Extras {
    /// For ROS compressedDepth: the depth-quantization range to use after
    /// PNG decoding. Both nullopt for non-depth compressed images.
    std::optional<float> compressed_depth_min;
    std::optional<float> compressed_depth_max;
  };

  Format format = Format::kUnknown;
  Span<const uint8_t> bytes;
  BufferAnchor anchor;
  Timestamp timestamp_ns = 0;
  Extras extras;
};

// -----------------------------------------------------------------------------
// sdk::PointCloud — packed point cloud
// -----------------------------------------------------------------------------

/// Description of one channel inside a packed point cloud (x, y, z, intensity,
/// rgb, ring, time, …). Mirrors the shape of sensor_msgs/PointField but the
/// type is canonical PJ vocabulary, not a ROS-specific enum.
struct PointField {
  enum class Datatype : uint8_t {
    kUnknown = 0,
    kInt8 = 1,
    kUint8 = 2,
    kInt16 = 3,
    kUint16 = 4,
    kInt32 = 5,
    kUint32 = 6,
    kFloat32 = 7,
    kFloat64 = 8,
  };

  std::string name;
  uint32_t offset = 0;  ///< Byte offset of this field within a single point.
  Datatype datatype = Datatype::kUnknown;
  uint32_t count = 1;  ///< Number of elements of `datatype` (typically 1).
};

/// Bytes per element for a given PointField datatype. Returns 0 for kUnknown.
[[nodiscard]] constexpr uint32_t bytesPerElement(PointField::Datatype dt) noexcept {
  switch (dt) {
    case PointField::Datatype::kInt8:
    case PointField::Datatype::kUint8:
      return 1;
    case PointField::Datatype::kInt16:
    case PointField::Datatype::kUint16:
      return 2;
    case PointField::Datatype::kInt32:
    case PointField::Datatype::kUint32:
    case PointField::Datatype::kFloat32:
      return 4;
    case PointField::Datatype::kFloat64:
      return 8;
    case PointField::Datatype::kUnknown:
      return 0;
  }
  return 0;
}

/// Packed point cloud. The `data` buffer holds `width * height` points, each
/// occupying `point_step` bytes laid out per `fields`. `is_dense=false` means
/// some points may be invalid (typically NaN-filled).
struct PointCloud {
  uint32_t width = 0;
  uint32_t height = 1;
  uint32_t point_step = 0;  ///< Bytes per point.
  uint32_t row_step = 0;    ///< Bytes per row (= point_step * width when no padding).
  bool is_bigendian = false;
  bool is_dense = true;
  std::vector<PointField> fields;
  Span<const uint8_t> data;
  BufferAnchor anchor;
  Timestamp timestamp_ns = 0;
};

// -----------------------------------------------------------------------------
// CanonicalObject — variant carried by parser->parseObject()
// -----------------------------------------------------------------------------

/// Sum type of all canonical objects a parser may produce. Closed for now;
/// extending it (kMarkers, kOccupancyGrid, …) requires bumping
/// PJ_MESSAGE_PARSER_PROTOCOL_VERSION (compatible append at the end).
using CanonicalObject = std::variant<Image, CompressedImage, PointCloud>;

/// Helper: get the kind tag for a CanonicalObject without unpacking it.
[[nodiscard]] inline CanonicalObjectKind kindOf(const CanonicalObject& obj) noexcept {
  return std::visit(
      [](const auto& concrete) -> CanonicalObjectKind {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, Image>) {
          return CanonicalObjectKind::kImage;
        } else if constexpr (std::is_same_v<T, CompressedImage>) {
          return CanonicalObjectKind::kCompressedImage;
        } else if constexpr (std::is_same_v<T, PointCloud>) {
          return CanonicalObjectKind::kPointCloud;
        } else {
          return CanonicalObjectKind::kNone;
        }
      },
      obj);
}

}  // namespace sdk
}  // namespace PJ
