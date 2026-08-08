/**
 * @file parser_module_abi.h
 * @brief Frozen parser-module export ABI and host-side byte codecs.
 *
 * Native and wasm parser modules expose the same operational functions. Every
 * address-like value is a uint64_t module-space token: a process address for
 * native artifacts or a linear-memory offset for wasm artifacts. The C++
 * helpers below encode and decode the little-endian blocks exchanged through
 * those functions without exposing C++ objects across the module boundary.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#ifndef PJ_PARSER_MODULE_ABI_H
#define PJ_PARSER_MODULE_ABI_H

#include <stdint.h>

#define PJ_PARSER_MODULE_ABI_VERSION UINT32_C(1)

/* Result codes are append-only. DECLINE is valid only from bind; parse uses
 * OK or a negative error. Every negative result records a UTF-8 diagnostic. */
#define PJ_MODULE_OK INT32_C(0)
#define PJ_MODULE_DECLINE INT32_C(1)
#define PJ_MODULE_ERR_GENERIC (-INT32_C(1))
#define PJ_MODULE_ERR_BAD_TOKEN (-INT32_C(2))
#define PJ_MODULE_ERR_MALFORMED_INPUT (-INT32_C(3))
#define PJ_MODULE_ERR_BAD_CLAIM_INDEX (-INT32_C(4))
#define PJ_MODULE_ERR_ALLOCATION_FAILURE (-INT32_C(5))

#define PJ_MODULE_ABI_EXPORT_NAME "pj_module_abi"
#define PJ_MODULE_CREATE_EXPORT_NAME "pj_module_create"
#define PJ_MODULE_DESTROY_EXPORT_NAME "pj_module_destroy"
#define PJ_MODULE_BIND_EXPORT_NAME "pj_module_bind"
#define PJ_MODULE_PARSE_EXPORT_NAME "pj_module_parse"
#define PJ_MODULE_LAST_ERROR_EXPORT_NAME "pj_module_last_error"
#define PJ_MODULE_ALLOC_EXPORT_NAME "pj_module_alloc"
#define PJ_MODULE_FREE_EXPORT_NAME "pj_module_free"
#define PJ_MODULE_MANIFEST_ADDR_EXPORT_NAME "pj_module_manifest_addr"
#define PJ_MODULE_MANIFEST_LEN_EXPORT_NAME "pj_module_manifest_len"

#define PJ_PARSER_MODULE_MANIFEST_SECTION_NAME "pj_parser_module_manifest"
#define PJ_MODULE_ERROR_BUFFER_SIZE UINT32_C(512)
/* Instance token zero is never valid. Creation failures are retrieved by
 * passing this token to pj_module_last_error. */
#define PJ_MODULE_CREATION_ERROR_TOKEN UINT64_C(0)

#define PJ_MODULE_BINDING_INFO_VERSION_V1 UINT16_C(1)
#define PJ_MODULE_OUTPUT_DESCRIPTOR_VERSION_V1 UINT16_C(1)
#define PJ_MODULE_ROUTE_SCALAR UINT8_C(1)
#define PJ_MODULE_ROUTE_OBJECT UINT8_C(2)
#define PJ_MODULE_PARSE_INPUT_FLAG_HAS_TIMESTAMP UINT8_C(1)
#define PJ_MODULE_SCALAR_VALUE_F64 UINT8_C(0)
#define PJ_MODULE_SCALAR_VALUE_I64 UINT8_C(1)
#define PJ_MODULE_SCALAR_VALUE_U64 UINT8_C(2)
#define PJ_MODULE_SCALAR_VALUE_BOOL UINT8_C(3)
#define PJ_MODULE_SCALAR_VALUE_STRING UINT8_C(4)

#ifdef __cplusplus
extern "C" {
#endif

/** Operational export signatures, resolved by the matching *_EXPORT_NAME.
 *
 * Lifecycle is create(claim_index), bind(BindingInfo), any number of parse
 * calls, then destroy. Module-owned output descriptors returned by parse stay
 * valid until the next call on that instance or destroy. Native addresses are
 * process pointers encoded as uint64_t; wasm addresses are linear-memory
 * offsets and are never host pointers. The host serializes lifecycle calls.
 */
typedef uint32_t (*PJ_module_abi_fn_t)(void);
typedef uint64_t (*PJ_module_create_fn_t)(uint32_t claim_index);
typedef void (*PJ_module_destroy_fn_t)(uint64_t inst);
typedef int32_t (*PJ_module_bind_fn_t)(uint64_t inst, uint64_t info_addr, uint64_t info_len);
typedef int32_t (*PJ_module_parse_fn_t)(
    uint64_t inst, uint64_t in_addr, uint64_t in_len, uint64_t out_addr_ptr, uint64_t out_len_ptr);
typedef uint64_t (*PJ_module_last_error_fn_t)(uint64_t inst, uint64_t buf_addr, uint64_t buf_cap);
typedef uint64_t (*PJ_module_alloc_fn_t)(uint64_t size);
typedef void (*PJ_module_free_fn_t)(uint64_t addr, uint64_t size);

/** Native-only metadata exports. Wasm modules deliver the manifest only in
 * PJ_PARSER_MODULE_MANIFEST_SECTION_NAME and must not export these functions.
 */
typedef uint64_t (*PJ_module_manifest_addr_fn_t)(void);
typedef uint64_t (*PJ_module_manifest_len_fn_t)(void);

#ifdef __cplusplus
}

#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "pj_base/expected.hpp"
#include "pj_base/span.hpp"

namespace PJ::parser_module {

inline constexpr uint16_t kBindingInfoVersionV1 = PJ_MODULE_BINDING_INFO_VERSION_V1;
inline constexpr uint16_t kOutputDescriptorVersionV1 = PJ_MODULE_OUTPUT_DESCRIPTOR_VERSION_V1;

enum class Route : uint8_t {
  kScalar = PJ_MODULE_ROUTE_SCALAR,
  kObject = PJ_MODULE_ROUTE_OBJECT,
};

/** BindingInfo v1 fields. All views are borrowed from the caller on write and
 * from the encoded block on read.
 */
struct BindingInfoV1 {
  Route route = Route::kScalar;
  uint32_t claim_index = 0;
  uint16_t expected_object_type = 0;
  Span<const uint8_t> encoding;
  Span<const uint8_t> type_name;
  Span<const uint8_t> schema;
  Span<const uint8_t> claim_id;
  Span<const uint8_t> config_json;
  Span<const uint8_t> schema_digest;
};

/** Parse-input framing. `payload` is borrowed from the caller or input block. */
struct ParseInputV1 {
  bool has_timestamp = false;
  int64_t timestamp_ns = 0;
  Span<const uint8_t> payload;
};

struct ObjectSpliceV1 {
  uint32_t field_number = 0;
  uint64_t input_offset = 0;
  uint64_t input_length = 0;
};

/** Object output descriptor. `wire` is full canonical wire without a splice,
 * or partial canonical wire with the optional eligible bulk field elided.
 */
struct ObjectOutputV1 {
  uint16_t object_type = 0;
  std::optional<ObjectSpliceV1> splice;
  Span<const uint8_t> wire;
};

using ScalarValueV1 = std::variant<double, int64_t, uint64_t, bool, std::string_view>;

struct ScalarFieldV1 {
  /** Name offsets in encoded descriptors are relative to byte zero of the
   * complete output block. Writers place names after all field values.
   */
  std::string_view name;
  ScalarValueV1 value;
};

struct ScalarOutputV1 {
  bool has_timestamp = false;
  int64_t timestamp_ns = 0;
  std::vector<ScalarFieldV1> fields;
};

using OutputDescriptorV1 = std::variant<ObjectOutputV1, ScalarOutputV1>;

/** Encode/decode the frozen little-endian module blocks. Readers return
 * borrowed views into `bytes` and reject malformed, out-of-range, truncated,
 * or unsupported-version data.
 */
[[nodiscard]] Expected<std::vector<uint8_t>> writeBindingInfoV1(const BindingInfoV1& info);
[[nodiscard]] Expected<BindingInfoV1> readBindingInfoV1(Span<const uint8_t> bytes);

[[nodiscard]] Expected<std::vector<uint8_t>> writeParseInputV1(const ParseInputV1& input);
[[nodiscard]] Expected<ParseInputV1> readParseInputV1(Span<const uint8_t> bytes);

[[nodiscard]] Expected<std::vector<uint8_t>> writeOutputDescriptorV1(const OutputDescriptorV1& output);
[[nodiscard]] Expected<OutputDescriptorV1> readOutputDescriptorV1(Span<const uint8_t> bytes);

}  // namespace PJ::parser_module

#endif

#endif  // PJ_PARSER_MODULE_ABI_H
