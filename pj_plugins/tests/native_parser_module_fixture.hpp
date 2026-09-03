#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

/// Claim indices exposed by native_parser_module_fixture.cpp. The order must
/// match the claims array of the fixture manifest: the host addresses claims
/// positionally, so a reordering here silently rebinds every test.
namespace pj_fixture {

enum ClaimIndex : uint32_t {
  kObject = 0,
  kScalar = 1,
  kDecline = 2,
  kCreateFailure = 3,
  kDataError = 4,
  kMalformed = 5,
  kSplice = 6,
  kSpliceOutOfBounds = 7,
  kSpliceIneligible = 8,
  kBadToken = 9,
  kRouteMismatch = 10,
  kTypeMismatch = 11,
  kSpliceGridMap = 12,
  kClaimCount = 13,
};

}  // namespace pj_fixture
