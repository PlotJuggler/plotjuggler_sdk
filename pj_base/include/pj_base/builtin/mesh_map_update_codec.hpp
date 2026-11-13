#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/mesh_map_update.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaMeshMapUpdate = "PJ.MeshMapUpdate";

/// Serializes sdk::MeshMapUpdate to canonical PJ.MeshMapUpdate wire bytes
/// (see pj_base/proto/pj/MeshMapUpdate.proto).
[[nodiscard]] std::vector<uint8_t> serializeMeshMapUpdate(const sdk::MeshMapUpdate& update);

/// Decodes canonical PJ.MeshMapUpdate wire bytes. The returned object owns its
/// bytes via `anchor`. Rejects any update validateMeshMapUpdate() rejects.
[[nodiscard]] Expected<sdk::MeshMapUpdate> deserializeMeshMapUpdate(const uint8_t* data, size_t size);

/// Layout check: every field has a known datatype, a non-zero count and ends
/// within `stride`; with records declared, `index` exists as uint32 with count 1; `data` covers
/// `count * stride`. Whether the indices exist in the base mesh is for the
/// consumer that pairs the two.
[[nodiscard]] Expected<void> validateMeshMapUpdate(const sdk::MeshMapUpdate& update);

}  // namespace PJ
