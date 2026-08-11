// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#if defined(_WIN32)
#define PJ_FIXTURE_EXPORT __declspec(dllexport)
#else
#define PJ_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" PJ_FIXTURE_EXPORT const char* pj_dependency_search_manifest() noexcept {
#if defined(PJ_DEPENDENCY_SEARCH_DECOY)
  return R"({"id":"dependency-search-decoy","name":"Dependency Search Decoy","version":"1.0.0"})";
#else
  return R"({"id":"dependency-search-real","name":"Dependency Search Real","version":"1.0.0"})";
#endif
}
