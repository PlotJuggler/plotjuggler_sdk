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

/// Decodes canonical PJ.MeshMapUpdate wire bytes. The returned object owns a
/// copy of the wire and its spans point into it (`anchor`), unaligned. As in
/// protobuf parsers, an unknown field, or a known field number with the wrong
/// wire type, is skipped; a known field with the right wire type and malformed
/// contents, or an update validateMeshMapUpdate() rejects, is an error. A patch
/// whose channel has a target or datatype number this SDK does not define is
/// dropped and the other patches are returned.
[[nodiscard]] Expected<sdk::MeshMapUpdate> deserializeMeshMapUpdate(const uint8_t* data, size_t size);

/// Layout check: each patch's channel has a name, a known datatype and a
/// non-zero count; a sparse patch's indices are uint32 and its data holds one
/// element per index; a dense patch's data holds whole elements; no (target,
/// name) repeats. Whether the indices, or a dense patch's element count, fit
/// the base mesh is for the consumer that pairs the two.
[[nodiscard]] Expected<void> validateMeshMapUpdate(const sdk::MeshMapUpdate& update);

}  // namespace PJ
