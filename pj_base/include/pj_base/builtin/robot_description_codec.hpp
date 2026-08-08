#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pj_base/builtin/robot_description.hpp"
#include "pj_base/expected.hpp"

namespace PJ {

/// Serializes sdk::RobotDescription to canonical PJ.RobotDescription wire bytes.
[[nodiscard]] std::vector<uint8_t> serializeRobotDescription(const sdk::RobotDescription& description);

/// Decodes canonical PJ.RobotDescription wire bytes.
[[nodiscard]] Expected<sdk::RobotDescription> deserializeRobotDescription(const uint8_t* data, size_t size);

}  // namespace PJ
