#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/frame_transforms.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaFrameTransforms = "PJ.FrameTransforms";

/// Serializes sdk::FrameTransforms to canonical PJ.FrameTransforms wire bytes.
///
/// The payload follows pj_base/proto/pj/FrameTransforms.proto, but the
/// implementation uses PlotJuggler's private protobuf-wire primitives rather
/// than generated Protobuf code.
[[nodiscard]] std::vector<uint8_t> serializeFrameTransforms(const sdk::FrameTransforms& transforms);

/// Decodes canonical PJ.FrameTransforms wire bytes into sdk::FrameTransforms.
[[nodiscard]] Expected<sdk::FrameTransforms> deserializeFrameTransforms(const uint8_t* data, size_t size);

}  // namespace PJ
