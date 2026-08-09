/**
 * @file builtin_object_abi.h
 * @brief C ABI vocabulary for schema classification.
 *
 * The host invokes classify_schema (a slot in PJ_message_parser_vtable_t)
 * after bind_schema to learn what type of canonical object the parser will
 * produce for that schema. The parser returns a PJ_schema_classification_t
 * carrying a PJ_builtin_object_type_t.
 *
 * Canonical-object and pure-functional scalar production use the additive
 * `pj.parser_functional.v1` and v2 C extensions. MessageParserPluginBase keeps
 * the plugin-author-facing ObjectRecord/ScalarRecord API inside the plugin
 * DSO; its trampolines emit only POD scalar views, canonical wire bytes, or a
 * v2 splice using the frozen eligibility table below. The concrete host-owned
 * C++ object is reconstructed on the host side.
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
  /* 12 reserved — was PJ_BUILTIN_OBJECT_TYPE_ASSET_VIDEO (removed; video unified on VIDEO_FRAME). */
  PJ_BUILTIN_OBJECT_TYPE_ROBOT_DESCRIPTION = 13,
  PJ_BUILTIN_OBJECT_TYPE_CAMERA_INFO = 14,
  PJ_BUILTIN_OBJECT_TYPE_OCCUPANCY_GRID_UPDATE = 15,
  PJ_BUILTIN_OBJECT_TYPE_LOG = 16,
  PJ_BUILTIN_OBJECT_TYPE_POSES_IN_FRAME = 17,
  PJ_BUILTIN_OBJECT_TYPE_VOXEL_GRID = 18,
  PJ_BUILTIN_OBJECT_TYPE_PLOT_MARKERS = 19,
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

/** One frozen canonical `PJ.*` bulk-field splice mapping.
 *
 * Object types appear only when their top-level canonical wire message has a
 * single unambiguous bulk bytes field. New mappings append to the table;
 * existing object-type/field-number pairs never change.
 */
typedef struct PJ_builtin_object_splice_field_v1_t {
  uint16_t object_type;
  uint16_t reserved;
  uint32_t field_number;
} PJ_builtin_object_splice_field_v1_t;

#define PJ_BUILTIN_OBJECT_SPLICE_FIELDS_V1_COUNT UINT32_C(9)

/** Return the frozen splice-eligible table and optionally its entry count. */
static inline const PJ_builtin_object_splice_field_v1_t* pj_builtin_object_splice_fields_v1(uint32_t* out_count) {
  static const PJ_builtin_object_splice_field_v1_t fields[PJ_BUILTIN_OBJECT_SPLICE_FIELDS_V1_COUNT] = {
      {PJ_BUILTIN_OBJECT_TYPE_IMAGE, 0, 7},
      {PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD, 0, 9},
      {PJ_BUILTIN_OBJECT_TYPE_DEPTH_IMAGE, 0, 5},
      {PJ_BUILTIN_OBJECT_TYPE_OCCUPANCY_GRID, 0, 7},
      {PJ_BUILTIN_OBJECT_TYPE_COMPRESSED_POINTCLOUD, 0, 4},
      {PJ_BUILTIN_OBJECT_TYPE_MESH3D, 0, 7},
      {PJ_BUILTIN_OBJECT_TYPE_VIDEO_FRAME, 0, 3},
      {PJ_BUILTIN_OBJECT_TYPE_OCCUPANCY_GRID_UPDATE, 0, 7},
      {PJ_BUILTIN_OBJECT_TYPE_VOXEL_GRID, 0, 12},
  };
  if (out_count != NULL) {
    *out_count = PJ_BUILTIN_OBJECT_SPLICE_FIELDS_V1_COUNT;
  }
  return fields;
}

/** Look up an eligible field number. Returns false for ineligible types or a
 * null output pointer.
 */
static inline bool pj_builtin_object_splice_field_number_v1(uint16_t object_type, uint32_t* out_field_number) {
  uint32_t count = 0;
  const PJ_builtin_object_splice_field_v1_t* fields = pj_builtin_object_splice_fields_v1(&count);
  uint32_t index = 0;
  if (out_field_number == NULL) {
    return false;
  }
  for (index = 0; index < count; ++index) {
    if (fields[index].object_type == object_type) {
      *out_field_number = fields[index].field_number;
      return true;
    }
  }
  return false;
}

#ifdef __cplusplus
}
#endif

#endif /* PJ_BUILTIN_OBJECT_ABI_H */
