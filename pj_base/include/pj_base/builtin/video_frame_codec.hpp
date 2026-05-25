#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

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
/// returned frame owns its bytes via `anchor`.
[[nodiscard]] Expected<sdk::VideoFrame> deserializeVideoFrame(const uint8_t* data, size_t size);

}  // namespace PJ
