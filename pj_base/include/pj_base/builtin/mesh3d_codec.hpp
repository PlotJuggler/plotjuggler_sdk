#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/mesh3d.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaMesh3D = "PJ.Mesh3D";

/// Serializes sdk::Mesh3D to canonical PJ.Mesh3D wire bytes (see
/// pj_base/proto/pj/Mesh3D.proto).
[[nodiscard]] std::vector<uint8_t> serializeMesh3D(const sdk::Mesh3D& mesh);

/// Decodes canonical PJ.Mesh3D wire bytes. The returned mesh owns its
/// embedded bytes via `anchor` (or `data` is empty when only `url` was set).
[[nodiscard]] Expected<sdk::Mesh3D> deserializeMesh3D(const uint8_t* data, size_t size);

}  // namespace PJ
