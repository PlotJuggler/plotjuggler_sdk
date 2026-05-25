#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/depth_image.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaDepthImage = "PJ.DepthImage";

/// Serializes sdk::DepthImage to canonical PJ.DepthImage wire bytes (see
/// pj_base/proto/pj/DepthImage.proto).
[[nodiscard]] std::vector<uint8_t> serializeDepthImage(const sdk::DepthImage& depth);

/// Decodes canonical PJ.DepthImage wire bytes. The returned object owns
/// its depth pixel bytes via `anchor`.
[[nodiscard]] Expected<sdk::DepthImage> deserializeDepthImage(const uint8_t* data, size_t size);

}  // namespace PJ
