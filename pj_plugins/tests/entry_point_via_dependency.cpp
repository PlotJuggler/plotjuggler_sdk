// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include "pj_base/plugin_data_api.h"

#if defined(_WIN32)
#define PJ_FIXTURE_IMPORT __declspec(dllimport)
#else
#define PJ_FIXTURE_IMPORT
#endif

extern "C" PJ_FIXTURE_IMPORT int pj_entry_point_donor_marker() noexcept;

namespace {

// Force the donor to remain in the candidate's dependency closure even when
// the toolchain links shared libraries with --as-needed.
[[maybe_unused]] const int kKeepDonorDependency = pj_entry_point_donor_marker();

}  // namespace
