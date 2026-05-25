/**
 * @file builtin_object_abi.h
 * @brief C ABI vocabulary for schema classification.
 *
 * The host invokes classify_schema (a slot in PJ_message_parser_vtable_t)
 * after bind_schema to learn what type of canonical object the parser will
 * produce for that schema. The parser returns a PJ_schema_classification_t
 * carrying a PJ_builtin_object_type_t.
 *
 * Canonical-object production (any concrete sdk::* type listed in
 * BuiltinObjectType — see pj_base/builtin/builtin_object.hpp) and the
 * pure-functional scalar production (Expected<ObjectRecord> /
 * Expected<ScalarRecord>) are C++ SDK contracts: plugins inheriting from
 * MessageParserPluginBase register handlers in SchemaHandler, and the
 * in-process host consumes them via MessageParserPluginBase::parseObject()
 * and parseScalars() called directly on the C++ pointer. Pure-C plugins
 * emit scalars via the parse() slot (writing to writeHost).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_BUILTIN_OBJECT_ABI_H
#define PJ_BUILTIN_OBJECT_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Canonical object types. Numeric values are stable across releases — never
 * renumber. Returned by the classify_schema slot to advertise what type of
 * canonical object the parser will produce for this schema (or kNone if
 * the parser only produces scalars).
 */
typedef enum PJ_builtin_object_type_t {
  PJ_BUILTIN_OBJECT_TYPE_NONE = 0,
  PJ_BUILTIN_OBJECT_TYPE_IMAGE = 1,
  /* 2 reserved — never used historically. */
  PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD = 3,
  PJ_BUILTIN_OBJECT_TYPE_DEPTH_IMAGE = 4,
  PJ_BUILTIN_OBJECT_TYPE_IMAGE_ANNOTATIONS = 5,
  PJ_BUILTIN_OBJECT_TYPE_FRAME_TRANSFORMS = 6,
  PJ_BUILTIN_OBJECT_TYPE_OCCUPANCY_GRID = 7,
  PJ_BUILTIN_OBJECT_TYPE_COMPRESSED_POINTCLOUD = 8,
  PJ_BUILTIN_OBJECT_TYPE_MESH3D = 9,
  PJ_BUILTIN_OBJECT_TYPE_VIDEO_FRAME = 10,
  PJ_BUILTIN_OBJECT_TYPE_SCENE_ENTITIES = 11,
  PJ_BUILTIN_OBJECT_TYPE_ASSET_VIDEO = 12,
  PJ_BUILTIN_OBJECT_TYPE_ROBOT_DESCRIPTION = 13,
  /* Reserve future types; appended at the tail. Numeric values are stable
   * across releases — never renumber. Each new value here must match the
   * matching kFoo entry in BuiltinObjectType (builtin_object.hpp). */
} PJ_builtin_object_type_t;

/**
 * Schema classification — what type a parser declares for a given schema.
 * Returned a priori (without parsing payload) by the classify_schema slot.
 *
 * Single field plus reserved padding to keep the struct size stable across
 * future minor extensions. The reserved byte must be zero today; readers
 * accept any value (forward compat).
 */
typedef struct PJ_schema_classification_t {
  uint16_t object_type; /**< PJ_builtin_object_type_t. */
  uint16_t reserved;
} PJ_schema_classification_t;

#ifdef __cplusplus
}
#endif

#endif /* PJ_BUILTIN_OBJECT_ABI_H */
