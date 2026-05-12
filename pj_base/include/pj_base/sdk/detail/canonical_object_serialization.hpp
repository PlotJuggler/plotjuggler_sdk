/**
 * @file detail/canonical_object_serialization.hpp
 * @brief (De)serialization of PJ::sdk::CanonicalObject to/from the byte
 *        layout defined in pj_base/canonical_object_abi.h.
 *
 * The blob crosses the C ABI as raw bytes; this header turns it into the
 * C++ variant on the host side and back into bytes on the plugin side.
 *
 * Endianness: writes/reads multi-byte integers using std::memcpy under the
 * assumption that the host architecture is little-endian (the ABI mandates
 * little-endian). Big-endian targets would need an explicit byte-swap layer
 * here; documented as a known limitation in this iteration.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "pj_base/canonical_object_abi.h"
#include "pj_base/expected.hpp"
#include "pj_base/sdk/canonical_object.hpp"

namespace PJ {
namespace sdk {
namespace detail {

// -----------------------------------------------------------------------------
// Low-level write helpers (host-endian = little-endian assumed)
// -----------------------------------------------------------------------------

inline void appendBytes(std::vector<uint8_t>& out, const void* src, size_t n) {
  if (n == 0) {
    return;
  }
  const auto* p = static_cast<const uint8_t*>(src);
  out.insert(out.end(), p, p + n);
}

template <typename T>
inline void appendPod(std::vector<uint8_t>& out, T value) {
  static_assert(std::is_trivially_copyable_v<T>, "appendPod requires trivially copyable");
  appendBytes(out, &value, sizeof(T));
}

// -----------------------------------------------------------------------------
// Low-level read helpers
// -----------------------------------------------------------------------------

class BlobReader {
 public:
  BlobReader(const uint8_t* data, size_t size) : ptr_(data), end_(data + size) {}

  [[nodiscard]] bool remaining(size_t n) const noexcept {
    return static_cast<size_t>(end_ - ptr_) >= n;
  }

  template <typename T>
  Expected<T> readPod() {
    static_assert(std::is_trivially_copyable_v<T>, "readPod requires trivially copyable");
    if (!remaining(sizeof(T))) {
      return unexpected(std::string("blob truncated"));
    }
    T value;
    std::memcpy(&value, ptr_, sizeof(T));
    ptr_ += sizeof(T);
    return value;
  }

  Expected<std::vector<uint8_t>> readBytes(size_t n) {
    if (!remaining(n)) {
      return unexpected(std::string("blob truncated reading bytes"));
    }
    std::vector<uint8_t> out(ptr_, ptr_ + n);
    ptr_ += n;
    return out;
  }

  Expected<std::string> readString(size_t n) {
    if (!remaining(n)) {
      return unexpected(std::string("blob truncated reading string"));
    }
    std::string out(reinterpret_cast<const char*>(ptr_), n);
    ptr_ += n;
    return out;
  }

 private:
  const uint8_t* ptr_;
  const uint8_t* end_;
};

// -----------------------------------------------------------------------------
// Serialization (C++ → bytes)
// -----------------------------------------------------------------------------

inline void writeImageBody(std::vector<uint8_t>& out, const Image& img) {
  appendPod<uint32_t>(out, img.width);
  appendPod<uint32_t>(out, img.height);
  appendPod<uint16_t>(out, static_cast<uint16_t>(img.pixel_format));
  appendPod<uint8_t>(out, img.is_bigendian ? 1 : 0);
  appendPod<uint8_t>(out, 0);  // reserved
  appendPod<uint32_t>(out, img.row_step);
  const uint32_t pixels_size = static_cast<uint32_t>(img.pixels.size());
  appendPod<uint32_t>(out, pixels_size);
  if (pixels_size > 0) {
    appendBytes(out, img.pixels.data(), pixels_size);
  }
}

inline void writeCompressedImageBody(std::vector<uint8_t>& out, const CompressedImage& ci) {
  appendPod<uint8_t>(out, static_cast<uint8_t>(ci.format));
  appendPod<uint8_t>(out, ci.extras.compressed_depth_min.has_value() ? 1 : 0);
  appendPod<uint8_t>(out, ci.extras.compressed_depth_max.has_value() ? 1 : 0);
  appendPod<uint8_t>(out, 0);  // reserved
  appendPod<float>(out, ci.extras.compressed_depth_min.value_or(0.0f));
  appendPod<float>(out, ci.extras.compressed_depth_max.value_or(0.0f));
  const uint32_t bytes_size = static_cast<uint32_t>(ci.bytes.size());
  appendPod<uint32_t>(out, bytes_size);
  if (bytes_size > 0) {
    appendBytes(out, ci.bytes.data(), bytes_size);
  }
}

inline void writePointCloudBody(std::vector<uint8_t>& out, const PointCloud& pc) {
  appendPod<uint32_t>(out, pc.width);
  appendPod<uint32_t>(out, pc.height);
  appendPod<uint32_t>(out, pc.point_step);
  appendPod<uint32_t>(out, pc.row_step);
  appendPod<uint8_t>(out, pc.is_bigendian ? 1 : 0);
  appendPod<uint8_t>(out, pc.is_dense ? 1 : 0);
  appendPod<uint16_t>(out, static_cast<uint16_t>(pc.fields.size()));
  for (const auto& f : pc.fields) {
    const uint32_t name_size = static_cast<uint32_t>(f.name.size());
    appendPod<uint32_t>(out, name_size);
    appendBytes(out, f.name.data(), name_size);
    appendPod<uint32_t>(out, f.offset);
    appendPod<uint8_t>(out, static_cast<uint8_t>(f.datatype));
    appendPod<uint8_t>(out, 0);  // reserved
    appendPod<uint8_t>(out, 0);  // reserved
    appendPod<uint8_t>(out, 0);  // reserved
    appendPod<uint32_t>(out, f.count);
  }
  const uint32_t data_size = static_cast<uint32_t>(pc.data.size());
  appendPod<uint32_t>(out, data_size);
  if (data_size > 0) {
    appendBytes(out, pc.data.data(), data_size);
  }
}

/// Serialize a CanonicalObject into a flat byte buffer matching the layout
/// in canonical_object_abi.h. Caller owns the returned vector.
inline std::vector<uint8_t> serializeCanonicalObject(const CanonicalObject& obj) {
  std::vector<uint8_t> out;
  out.reserve(64);  // header + small body; body grows for image/pointcloud

  // Header: kind (u16), reserved (u16), timestamp (i64).
  const auto kind = kindOf(obj);
  appendPod<uint16_t>(out, static_cast<uint16_t>(kind));
  appendPod<uint16_t>(out, 0);  // reserved
  std::visit(
      [&](const auto& concrete) {
        appendPod<int64_t>(out, concrete.timestamp_ns);
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, Image>) {
          writeImageBody(out, concrete);
        } else if constexpr (std::is_same_v<T, CompressedImage>) {
          writeCompressedImageBody(out, concrete);
        } else if constexpr (std::is_same_v<T, PointCloud>) {
          writePointCloudBody(out, concrete);
        }
      },
      obj);

  return out;
}

// -----------------------------------------------------------------------------
// Deserialization (bytes → C++)
// -----------------------------------------------------------------------------

// On the deserialize side we don't have a foreign anchor — the bytes come
// from the blob buffer. Wrap them in a shared_ptr<vector> and use that as
// the anchor; the Span points into the wrapped vector. Net cost: one alloc
// per object, same as before the iter-3 SDK change.
inline Expected<Image> readImageBody(BlobReader& r, Timestamp ts) {
  auto width = r.readPod<uint32_t>();
  if (!width) {
    return unexpected(width.error());
  }
  auto height = r.readPod<uint32_t>();
  if (!height) {
    return unexpected(height.error());
  }
  auto pixel_format_raw = r.readPod<uint16_t>();
  if (!pixel_format_raw) {
    return unexpected(pixel_format_raw.error());
  }
  auto is_be = r.readPod<uint8_t>();
  if (!is_be) {
    return unexpected(is_be.error());
  }
  /*reserved*/ if (auto rsv = r.readPod<uint8_t>(); !rsv) { return unexpected(rsv.error()); }
  auto row_step = r.readPod<uint32_t>();
  if (!row_step) {
    return unexpected(row_step.error());
  }
  auto pixels_size = r.readPod<uint32_t>();
  if (!pixels_size) {
    return unexpected(pixels_size.error());
  }
  auto pixels = r.readBytes(*pixels_size);
  if (!pixels) {
    return unexpected(pixels.error());
  }

  auto owned = std::make_shared<std::vector<uint8_t>>(std::move(*pixels));
  Span<const uint8_t> view(owned->data(), owned->size());
  return Image{
      .width = *width,
      .height = *height,
      .pixel_format = static_cast<PixelFormat>(*pixel_format_raw),
      .row_step = *row_step,
      .is_bigendian = (*is_be != 0),
      .pixels = view,
      .anchor = owned,
      .timestamp_ns = ts,
  };
}

inline Expected<CompressedImage> readCompressedImageBody(BlobReader& r, Timestamp ts) {
  auto format_raw = r.readPod<uint8_t>();
  if (!format_raw) {
    return unexpected(format_raw.error());
  }
  auto has_min = r.readPod<uint8_t>();
  if (!has_min) {
    return unexpected(has_min.error());
  }
  auto has_max = r.readPod<uint8_t>();
  if (!has_max) {
    return unexpected(has_max.error());
  }
  /*reserved*/ if (auto rsv = r.readPod<uint8_t>(); !rsv) { return unexpected(rsv.error()); }
  auto depth_min = r.readPod<float>();
  if (!depth_min) {
    return unexpected(depth_min.error());
  }
  auto depth_max = r.readPod<float>();
  if (!depth_max) {
    return unexpected(depth_max.error());
  }
  auto bytes_size = r.readPod<uint32_t>();
  if (!bytes_size) {
    return unexpected(bytes_size.error());
  }
  auto bytes = r.readBytes(*bytes_size);
  if (!bytes) {
    return unexpected(bytes.error());
  }

  auto owned = std::make_shared<std::vector<uint8_t>>(std::move(*bytes));
  CompressedImage ci{};
  ci.format = static_cast<CompressedImage::Format>(*format_raw);
  ci.bytes = Span<const uint8_t>(owned->data(), owned->size());
  ci.anchor = owned;
  ci.timestamp_ns = ts;
  if (*has_min != 0) {
    ci.extras.compressed_depth_min = *depth_min;
  }
  if (*has_max != 0) {
    ci.extras.compressed_depth_max = *depth_max;
  }
  return ci;
}

inline Expected<PointCloud> readPointCloudBody(BlobReader& r, Timestamp ts) {
  auto width = r.readPod<uint32_t>();
  if (!width) {
    return unexpected(width.error());
  }
  auto height = r.readPod<uint32_t>();
  if (!height) {
    return unexpected(height.error());
  }
  auto point_step = r.readPod<uint32_t>();
  if (!point_step) {
    return unexpected(point_step.error());
  }
  auto row_step = r.readPod<uint32_t>();
  if (!row_step) {
    return unexpected(row_step.error());
  }
  auto is_be = r.readPod<uint8_t>();
  if (!is_be) {
    return unexpected(is_be.error());
  }
  auto is_dense = r.readPod<uint8_t>();
  if (!is_dense) {
    return unexpected(is_dense.error());
  }
  auto fields_count = r.readPod<uint16_t>();
  if (!fields_count) {
    return unexpected(fields_count.error());
  }

  std::vector<PointField> fields;
  fields.reserve(*fields_count);
  for (uint16_t i = 0; i < *fields_count; ++i) {
    auto name_size = r.readPod<uint32_t>();
    if (!name_size) {
      return unexpected(name_size.error());
    }
    auto name = r.readString(*name_size);
    if (!name) {
      return unexpected(name.error());
    }
    auto offset = r.readPod<uint32_t>();
    if (!offset) {
      return unexpected(offset.error());
    }
    auto datatype_raw = r.readPod<uint8_t>();
    if (!datatype_raw) {
      return unexpected(datatype_raw.error());
    }
    /*reserved×3*/ for (int j = 0; j < 3; ++j) {
      if (auto rsv = r.readPod<uint8_t>(); !rsv) {
        return unexpected(rsv.error());
      }
    }
    auto count = r.readPod<uint32_t>();
    if (!count) {
      return unexpected(count.error());
    }
    fields.push_back(
        PointField{
            .name = std::move(*name),
            .offset = *offset,
            .datatype = static_cast<PointField::Datatype>(*datatype_raw),
            .count = *count,
        });
  }

  auto data_size = r.readPod<uint32_t>();
  if (!data_size) {
    return unexpected(data_size.error());
  }
  auto data = r.readBytes(*data_size);
  if (!data) {
    return unexpected(data.error());
  }

  auto owned = std::make_shared<std::vector<uint8_t>>(std::move(*data));
  Span<const uint8_t> view(owned->data(), owned->size());
  return PointCloud{
      .width = *width,
      .height = *height,
      .point_step = *point_step,
      .row_step = *row_step,
      .is_bigendian = (*is_be != 0),
      .is_dense = (*is_dense != 0),
      .fields = std::move(fields),
      .data = view,
      .anchor = owned,
      .timestamp_ns = ts,
  };
}

/// Deserialize a flat byte buffer into a CanonicalObject. Returns unexpected
/// on truncation, unknown kind, or any inconsistency.
inline Expected<CanonicalObject> deserializeCanonicalObject(const uint8_t* data, size_t size) {
  if (data == nullptr) {
    return unexpected(std::string("null blob"));
  }
  BlobReader r(data, size);

  auto kind_raw = r.readPod<uint16_t>();
  if (!kind_raw) {
    return unexpected(kind_raw.error());
  }
  /*reserved*/ if (auto rsv = r.readPod<uint16_t>(); !rsv) { return unexpected(rsv.error()); }
  auto ts = r.readPod<int64_t>();
  if (!ts) {
    return unexpected(ts.error());
  }

  switch (static_cast<CanonicalObjectKind>(*kind_raw)) {
    case CanonicalObjectKind::kImage: {
      auto img = readImageBody(r, *ts);
      if (!img) {
        return unexpected(img.error());
      }
      return CanonicalObject{std::move(*img)};
    }
    case CanonicalObjectKind::kCompressedImage: {
      auto ci = readCompressedImageBody(r, *ts);
      if (!ci) {
        return unexpected(ci.error());
      }
      return CanonicalObject{std::move(*ci)};
    }
    case CanonicalObjectKind::kPointCloud: {
      auto pc = readPointCloudBody(r, *ts);
      if (!pc) {
        return unexpected(pc.error());
      }
      return CanonicalObject{std::move(*pc)};
    }
    case CanonicalObjectKind::kNone:
    default:
      return unexpected(std::string("unknown or unsupported canonical object kind"));
  }
}

}  // namespace detail
}  // namespace sdk
}  // namespace PJ
