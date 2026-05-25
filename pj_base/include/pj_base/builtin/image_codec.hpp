#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/image.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaImage = "PJ.Image";

/// Serializes sdk::Image to canonical PJ.Image wire bytes (see
/// pj_base/proto/pj/Image.proto).
[[nodiscard]] std::vector<uint8_t> serializeImage(const sdk::Image& image);

/// Decodes canonical PJ.Image wire bytes. The returned image owns its
/// pixel/compressed bytes via `anchor`.
[[nodiscard]] Expected<sdk::Image> deserializeImage(const uint8_t* data, size_t size);

}  // namespace PJ
