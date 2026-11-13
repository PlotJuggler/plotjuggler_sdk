#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/mesh_map.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaMeshMap = "PJ.MeshMap";

/// Serializes sdk::MeshMap to canonical PJ.MeshMap wire bytes
/// (see pj_base/proto/pj/MeshMap.proto). The struct is written as-is; the
/// layout is checked on decode.
[[nodiscard]] std::vector<uint8_t> serializeMeshMap(const sdk::MeshMap& mesh);

/// Decodes canonical PJ.MeshMap wire bytes. The returned object owns its bytes
/// via `anchor`. Rejects any mesh validateMeshMap() rejects.
[[nodiscard]] Expected<sdk::MeshMap> deserializeMeshMap(const uint8_t* data, size_t size);

/// Full check of a mesh whose bytes are in place: every field has a known
/// datatype, a non-zero count and ends within its stride; the required
/// channels exist with the required types; both record regions fit in `data`
/// without overlapping; every face's vertex indices are below `vertex_count`;
/// every link names existing faces. Consumers that index records call it
/// before trusting the layout. Linear in the face count.
[[nodiscard]] Expected<void> validateMeshMap(const sdk::MeshMap& mesh);

}  // namespace PJ
