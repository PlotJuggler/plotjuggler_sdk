#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

// Bounds checks for counts and string views read from an ABI struct that a peer
// filled. Shared by the plugin-side client wrappers and the host-side decoders.

#include <cstddef>
#include <cstdint>
#include <limits>

#include "pj_base/plugin_data_api.h"

namespace PJ::sdk::detail {

/// Whether `count` elements of `element_size` bytes can be one object: the byte count
/// must fit std::ptrdiff_t, so count * element_size cannot overflow std::size_t even
/// where it is 32 bits. A caller's count is untrusted until this holds.
[[nodiscard]] constexpr bool fitsObject(uint64_t count, std::size_t element_size) noexcept {
  return count <= static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) / element_size;
}

/// A view may be NULL only when empty, and its size must fit an object; a valid one
/// needs no terminator.
[[nodiscard]] constexpr bool validView(PJ_string_view_t view) noexcept {
  return (view.data != nullptr || view.size == 0) && fitsObject(view.size, 1);
}

}  // namespace PJ::sdk::detail
