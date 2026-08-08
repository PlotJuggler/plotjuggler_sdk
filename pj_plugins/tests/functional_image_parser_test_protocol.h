// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_FUNCTIONAL_IMAGE_PARSER_TEST_PROTOCOL_H
#define PJ_FUNCTIONAL_IMAGE_PARSER_TEST_PROTOCOL_H

#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_FUNCTIONAL_IMAGE_PARSER_UNLOAD_OBSERVER_V1 "pj.test.unload_observer.v1"

typedef void (*PJ_test_plugin_unloaded_fn_t)(void* ctx) PJ_NOEXCEPT;

typedef void (*PJ_test_observe_plugin_unload_fn_t)(void* ctx, PJ_test_plugin_unloaded_fn_t callback) PJ_NOEXCEPT;

typedef struct PJ_test_unload_observer_v1_t {
  uint32_t struct_size;
  PJ_test_observe_plugin_unload_fn_t observe;
} PJ_test_unload_observer_v1_t;

#ifdef __cplusplus
}
#endif

#endif  // PJ_FUNCTIONAL_IMAGE_PARSER_TEST_PROTOCOL_H
