#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/video_frame.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaVideoFrame = "PJ.VideoFrame";

/// Serializes sdk::VideoFrame to canonical PJ.VideoFrame wire bytes.
///
/// The payload follows pj_base/proto/pj/VideoFrame.proto, but the
/// implementation uses PlotJuggler's private protobuf-wire primitives rather
/// than generated Protobuf code.
[[nodiscard]] std::vector<uint8_t> serializeVideoFrame(const sdk::VideoFrame& frame);

/// Decodes canonical PJ.VideoFrame wire bytes into sdk::VideoFrame. The
/// returned frame owns its bytes via `anchor` (a fresh copy of the `data`
/// field). Use this when the wire buffer does not outlive the call.
[[nodiscard]] Expected<sdk::VideoFrame> deserializeVideoFrame(const uint8_t* data, size_t size);

/// Decodes canonical PJ.VideoFrame / foxglove.CompressedVideo wire bytes into
/// sdk::VideoFrame without copying the compressed bitstream. The returned
/// frame's `data` ALIASES the input buffer and its `anchor` is set to the
/// supplied `anchor`, which the caller must keep alive for as long as the frame
/// (and its `data` span) is used. The two schemas are wire-identical, so this
/// one decoder serves both.
[[nodiscard]] Expected<sdk::VideoFrame> deserializeVideoFrameView(
    const uint8_t* data, size_t size, sdk::BufferAnchor anchor);

}  // namespace PJ
