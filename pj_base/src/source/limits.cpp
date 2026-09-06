// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/sdk/source/limits.hpp"

#include "pj_base/number_parse.hpp"
#include "pj_base/sdk/platform.hpp"

namespace PJ::sdk::source {

std::uint64_t envLimit(const char* name, std::uint64_t fallback_value) {
  const auto raw = getEnv(name);
  // 20-char cap keeps even zero-padded overlong values invalid; parseNumber
  // rejects empty input, signs, trailing junk and overflow past UINT64_MAX.
  if (!raw || raw->size() > 20) {
    return fallback_value;
  }
  return PJ::parseNumber<std::uint64_t>(*raw).value_or(fallback_value);
}

}  // namespace PJ::sdk::source
