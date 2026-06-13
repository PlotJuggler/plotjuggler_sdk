#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/poses_in_frame.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaPosesInFrame = "PJ.PosesInFrame";

/// Serializes sdk::PosesInFrame to canonical PJ.PosesInFrame wire bytes
/// (see pj_base/proto/pj/PosesInFrame.proto).
[[nodiscard]] std::vector<uint8_t> serializePosesInFrame(const sdk::PosesInFrame& poses);

/// Decodes canonical PJ.PosesInFrame wire bytes into sdk::PosesInFrame.
[[nodiscard]] Expected<sdk::PosesInFrame> deserializePosesInFrame(const uint8_t* data, size_t size);

}  // namespace PJ
