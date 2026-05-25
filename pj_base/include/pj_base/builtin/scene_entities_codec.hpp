#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pj_base/builtin/scene_entities.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

inline constexpr std::string_view kSchemaSceneEntities = "PJ.SceneEntities";

/// Serializes sdk::SceneEntities to canonical PJ.SceneEntities wire bytes
/// (see pj_base/proto/pj/SceneEntities.proto).
[[nodiscard]] std::vector<uint8_t> serializeSceneEntities(const sdk::SceneEntities& entities);

/// Decodes canonical PJ.SceneEntities wire bytes into sdk::SceneEntities.
[[nodiscard]] Expected<sdk::SceneEntities> deserializeSceneEntities(const uint8_t* data, size_t size);

}  // namespace PJ
