#pragma once
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifdef PJ_ASSERT_THROWS
#include <stdexcept>
#define PJ_ASSERT(cond, msg)         \
  do {                               \
    if (!(cond)) {                   \
      throw std::runtime_error(msg); \
    }                                \
  } while (false)
#else
#include <cassert>
#define PJ_ASSERT(cond, msg) assert((cond) && (msg))
#endif
