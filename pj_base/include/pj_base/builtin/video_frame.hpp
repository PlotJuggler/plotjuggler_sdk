/**
 * @file video_frame.hpp
 * @brief Single frame of a compressed video stream (h264 / h265 / vp9 / av1).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/span.hpp"
#include "pj_base/types.hpp"

namespace PJ {
namespace sdk {

/// Single frame of a compressed video stream. Unlike Image, a video frame
/// may have inter-frame dependencies (P-frames, etc.); consumers must
/// maintain decoder state across frames within a stream.
///
/// `data` must contain enough bitstream to decode exactly one frame given
/// prior stream state. Format-specific packaging requirements live in the
/// proto schema documentation (pj_base/proto/pj/VideoFrame.proto).
///
/// `anchor` keeps the underlying buffer alive — the producer may have made
/// `data` a view into the source payload (zero-copy) or into a freshly
/// allocated vector; consumers don't need to know which.
struct VideoFrame {
  Timestamp timestamp_ns = 0;
  std::string frame_id;  ///< Camera frame. Optical axis: +x right, +y down, +z into scene.
  std::string format;    ///< Codec identifier, lowercase. "h264", "h265", "vp9", "av1".
  Span<const uint8_t> data;
  BufferAnchor anchor;
};

}  // namespace sdk
}  // namespace PJ
