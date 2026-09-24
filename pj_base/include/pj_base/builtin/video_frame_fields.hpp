/**
 * @file video_frame_fields.hpp
 * @brief `FieldTable` specialization for `video_frame.hpp`'s `VideoFrame`.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

#include "pj_base/builtin/field_table.hpp"
#include "pj_base/builtin/video_frame.hpp"

namespace PJ::sdk {

/// `kBuffer` descriptor for `VideoFrame::data`. Every `VideoFrame::format`
/// ("h264", "h265", "vp9", "av1") is a compressed bitstream with
/// inter-frame dependencies — there is no static per-record size at all, so
/// `record_step`/`record_count`/`row_step` are always 0 and `bytes` alone
/// (its full span) is the payload; `channels` carries exactly one entry
/// naming the codec (`format`), so a consumer that gets a zeroed
/// `record_step` still learns which codec produced the bytes — same
/// convention as `imageDataField()` (`image_fields.hpp`) for a compressed
/// `Image`. `buffer_assign()` follows the same take-ownership-and-re-anchor
/// idiom as `pointCloudDataField()`.
[[nodiscard]] constexpr FieldDescriptor videoFrameDataField(std::string_view name) {
  FieldDescriptor d{};
  d.name = name;
  d.kind = FieldKind::kBuffer;
  d.buffer = [](const void* p) -> BufferLayout {
    const auto& frame = *static_cast<const VideoFrame*>(p);
    BufferLayout layout;
    layout.bytes = frame.data;
    layout.channels.push_back(BufferLayout::Channel{.name = frame.format, .offset = 0, .datatype = 0, .count = 0});
    return layout;
  };
  d.buffer_assign = [](void* p, std::vector<uint8_t> bytes) {
    auto& frame = *static_cast<VideoFrame*>(p);
    const PayloadView view = makePayloadView(std::move(bytes));
    frame.data = view.bytes;
    frame.anchor = view.anchor;
  };
  return d;
}

/// Fields of `VideoFrame`: every member is an ordinary field except `data`
/// (`videoFrameDataField`); `anchor` is written through that descriptor's
/// `buffer_assign()` and is not listed.
template <>
struct FieldTable<VideoFrame> {
  static constexpr std::array<FieldDescriptor, 4> fields{
      field<&VideoFrame::timestamp_ns>("timestamp_ns"),
      field<&VideoFrame::frame_id>("frame_id"),
      field<&VideoFrame::format>("format"),
      videoFrameDataField("data"),
  };
  static constexpr FieldTableView view{"VideoFrame", Span<const FieldDescriptor>(fields)};
};

}  // namespace PJ::sdk
