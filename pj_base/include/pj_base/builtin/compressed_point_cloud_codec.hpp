#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/compressed_point_cloud.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaCompressedPointCloud = "PJ.CompressedPointCloud";

/// Serializes sdk::CompressedPointCloud to canonical PJ.CompressedPointCloud
/// wire bytes (see pj_base/proto/pj/CompressedPointCloud.proto).
[[nodiscard]] std::vector<uint8_t> serializeCompressedPointCloud(const sdk::CompressedPointCloud& cloud);

/// Decodes canonical PJ.CompressedPointCloud wire bytes. The returned object
/// owns its bytes via `anchor`.
[[nodiscard]] Expected<sdk::CompressedPointCloud> deserializeCompressedPointCloud(const uint8_t* data, size_t size);

}  // namespace PJ
