#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/camera_info.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaCameraInfo = "PJ.CameraInfo";

/// Serializes sdk::CameraInfo to canonical PJ.CameraInfo wire bytes
/// (see pj_base/proto/pj/CameraInfo.proto).
[[nodiscard]] std::vector<uint8_t> serializeCameraInfo(const sdk::CameraInfo& info);

/// Decodes canonical PJ.CameraInfo wire bytes into an owned sdk::CameraInfo.
[[nodiscard]] Expected<sdk::CameraInfo> deserializeCameraInfo(const uint8_t* data, size_t size);

}  // namespace PJ
