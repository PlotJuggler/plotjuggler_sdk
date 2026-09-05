// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0
//
// Private FIPS 180-4 SHA-256 for descriptor identities. Vendored rather than
// taken from a crypto library so the SDK package adds no dependency to every
// plugin build; pinned by the NIST vectors in the tests.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace PJ {
namespace sdk {
namespace descriptor_import {
namespace detail {

[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::string_view data);

}  // namespace detail
}  // namespace descriptor_import
}  // namespace sdk
}  // namespace PJ
