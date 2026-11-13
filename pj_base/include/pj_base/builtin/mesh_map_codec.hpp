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

/// Decodes canonical PJ.MeshMap wire bytes. The returned object owns a copy of
/// the wire and its spans point into it (`anchor`), unaligned. As in protobuf
/// parsers, an unknown field, or a known field number with the wrong wire type,
/// is skipped, so a later SDK may add optional ones; a known field with the
/// right wire type and malformed contents, or a mesh validateMeshMap() rejects,
/// is an error. A channel whose target or datatype number this SDK does not
/// define is dropped and the rest of the mesh is returned.
[[nodiscard]] Expected<sdk::MeshMap> deserializeMeshMap(const uint8_t* data, size_t size);

/// Full check of a mesh whose spans are in place: position_type is float32 or
/// float64 and `positions` holds exactly vertex_count xyz triplets; `indices`
/// holds exactly face_count uint32 triplets, each below vertex_count; every
/// channel has a name, a known datatype, a non-zero count and exactly one
/// element per vertex or face; no (target, name) repeats; every link names
/// existing faces. Linear in the face count. Consumers that index the arrays
/// call it before trusting them.
[[nodiscard]] Expected<void> validateMeshMap(const sdk::MeshMap& mesh);

}  // namespace PJ
