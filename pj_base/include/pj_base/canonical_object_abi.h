/**
 * @file canonical_object_abi.h
 * @brief C ABI vocabulary for schema classification.
 *
 * The host invokes classify_schema (a slot in PJ_message_parser_vtable_t)
 * after bind_schema to learn what kind of canonical object the parser will
 * produce for that schema. The parser returns a PJ_schema_classification_t
 * carrying a PJ_canonical_object_kind_t.
 *
 * Canonical-object production (sdk::Image / sdk::CompressedImage /
 * sdk::PointCloud) and the pure-functional scalar production
 * (Expected<vector<NamedFieldValue>>) are C++ SDK contracts: plugins
 * inheriting from MessageParserPluginBase register handlers in
 * SchemaHandler, and the in-process host consumes them via
 * MessageParserPluginBase::parseObject() and parseScalars() called
 * directly on the C++ pointer. Pure-C plugins emit scalars via the
 * parse() slot (writing to writeHost).
 */
#ifndef PJ_CANONICAL_OBJECT_ABI_H
#define PJ_CANONICAL_OBJECT_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pj_base/plugin_data_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Canonical object kinds. Numeric values are stable across releases — never
 * renumber. Returned by the classify_schema slot to advertise what kind of
 * canonical object the parser will produce for this schema (or kNone if
 * the parser only produces scalars).
 */
typedef enum PJ_canonical_object_kind_t {
  PJ_CANONICAL_OBJECT_KIND_NONE = 0,
  PJ_CANONICAL_OBJECT_KIND_IMAGE = 1,
  PJ_CANONICAL_OBJECT_KIND_COMPRESSED_IMAGE = 2,
  PJ_CANONICAL_OBJECT_KIND_POINTCLOUD = 3,
  /* Reserve future kinds; appended at the tail. */
  /* PJ_CANONICAL_OBJECT_KIND_MARKERS         = 4, */
  /* PJ_CANONICAL_OBJECT_KIND_OCCUPANCY_GRID  = 5, */
} PJ_canonical_object_kind_t;

/**
 * Schema classification — what kind a parser declares for a given schema.
 * Returned a priori (without parsing payload) by the classify_schema slot.
 *
 * Single field plus reserved padding to keep the struct size stable across
 * future minor extensions. The reserved byte must be zero today; readers
 * accept any value (forward compat).
 */
typedef struct PJ_schema_classification_t {
  uint16_t object_kind; /**< PJ_canonical_object_kind_t. */
  uint16_t reserved;
} PJ_schema_classification_t;

#ifdef __cplusplus
}
#endif

#endif /* PJ_CANONICAL_OBJECT_ABI_H */
