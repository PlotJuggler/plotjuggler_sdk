#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/asset_video.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaAssetVideo = "PJ.AssetVideo";

/// Serializes sdk::AssetVideo to canonical PJ.AssetVideo wire bytes (see
/// pj_base/proto/pj/AssetVideo.proto).
[[nodiscard]] std::vector<uint8_t> serializeAssetVideo(const sdk::AssetVideo& asset);

/// Decodes canonical PJ.AssetVideo wire bytes into sdk::AssetVideo.
[[nodiscard]] Expected<sdk::AssetVideo> deserializeAssetVideo(const uint8_t* data, size_t size);

}  // namespace PJ
