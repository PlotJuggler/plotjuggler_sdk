#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/buffer_anchor.hpp"
#include "pj_base/builtin/point_cloud.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaPointCloud = "PJ.PointCloud";

/// Serializes sdk::PointCloud to canonical PJ.PointCloud wire bytes (see
/// pj_base/proto/pj/PointCloud.proto).
[[nodiscard]] std::vector<uint8_t> serializePointCloud(const sdk::PointCloud& cloud);

/// Decodes canonical PJ.PointCloud wire bytes. The returned object owns
/// its packed point bytes via `anchor`.
[[nodiscard]] Expected<sdk::PointCloud> deserializePointCloud(const uint8_t* data, size_t size);

/// Decodes canonical PJ.PointCloud wire bytes without copying the packed point
/// bytes: the returned cloud's `data` ALIASES the input buffer and its `anchor`
/// is the supplied `anchor`, which must keep that buffer alive and unchanged for
/// as long as the cloud is used. A null `anchor` copies, like
/// deserializePointCloud().
[[nodiscard]] Expected<sdk::PointCloud> deserializePointCloudView(
    const uint8_t* data, size_t size, sdk::BufferAnchor anchor);

}  // namespace PJ
