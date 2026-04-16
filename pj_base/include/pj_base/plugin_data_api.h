#ifndef PJ_PLUGIN_DATA_API_H
#define PJ_PLUGIN_DATA_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_PLUGIN_DATA_API_VERSION 1

typedef enum {
  PJ_PRIMITIVE_TYPE_FLOAT32 = 0,
  PJ_PRIMITIVE_TYPE_FLOAT64 = 1,
  PJ_PRIMITIVE_TYPE_INT8 = 2,
  PJ_PRIMITIVE_TYPE_INT16 = 3,
  PJ_PRIMITIVE_TYPE_INT32 = 4,
  PJ_PRIMITIVE_TYPE_INT64 = 5,
  PJ_PRIMITIVE_TYPE_UINT8 = 6,
  PJ_PRIMITIVE_TYPE_UINT16 = 7,
  PJ_PRIMITIVE_TYPE_UINT32 = 8,
  PJ_PRIMITIVE_TYPE_UINT64 = 9,
  PJ_PRIMITIVE_TYPE_BOOL = 10,
  PJ_PRIMITIVE_TYPE_STRING = 11,
  /** Sentinel: null value with no type information. Used when is_null=true
   *  and the plugin provides no type hint (untyped kNull). */
  PJ_PRIMITIVE_TYPE_UNSPECIFIED = 0xFF,
} PJ_primitive_type_t;

typedef struct {
  const char* data;
  size_t size;
} PJ_string_view_t;

typedef struct {
  const uint8_t* data;
  size_t size;
} PJ_bytes_view_t;

typedef struct {
  uint32_t id;
} PJ_data_source_handle_t;

typedef struct {
  uint32_t id;
} PJ_topic_handle_t;

typedef struct {
  PJ_topic_handle_t topic;
  uint32_t id;
} PJ_field_handle_t;

typedef union {
  float as_float32;
  double as_float64;
  int8_t as_int8;
  int16_t as_int16;
  int32_t as_int32;
  int64_t as_int64;
  uint8_t as_uint8;
  uint16_t as_uint16;
  uint32_t as_uint32;
  uint64_t as_uint64;
  uint8_t as_bool;
  PJ_string_view_t as_string;
} PJ_scalar_value_data_t;

typedef struct {
  PJ_primitive_type_t type;
  PJ_scalar_value_data_t data;
} PJ_scalar_value_t;

typedef struct {
  PJ_string_view_t name;
  bool is_null;
  PJ_scalar_value_t value;
} PJ_named_field_value_t;

typedef struct {
  PJ_field_handle_t field;
  bool is_null;
  PJ_scalar_value_t value;
} PJ_bound_field_value_t;

typedef struct {
  PJ_data_source_handle_t handle;
  PJ_string_view_t name;
  uint32_t first_topic;
  uint32_t topic_count;
} PJ_data_source_info_t;

typedef struct {
  PJ_topic_handle_t handle;
  PJ_data_source_handle_t source;
  PJ_string_view_t name;
  uint32_t first_field;
  uint32_t field_count;
} PJ_topic_info_t;

typedef struct {
  PJ_field_handle_t handle;
  PJ_string_view_t name;
  PJ_primitive_type_t type;
} PJ_field_info_t;

typedef struct {
  const uint32_t* offsets;
  size_t offset_count;
  const char* bytes;
  size_t byte_count;
} PJ_string_series_values_t;

typedef union {
  const float* as_float32;
  const double* as_float64;
  const int8_t* as_int8;
  const int16_t* as_int16;
  const int32_t* as_int32;
  const int64_t* as_int64;
  const uint8_t* as_uint8;
  const uint16_t* as_uint16;
  const uint32_t* as_uint32;
  const uint64_t* as_uint64;
  const uint8_t* as_bool;
  PJ_string_series_values_t as_string;
} PJ_series_values_t;

typedef struct {
  const PJ_data_source_info_t* data_sources;
  size_t data_source_count;
  const PJ_topic_info_t* topics;
  size_t topic_count;
  const PJ_field_info_t* fields;
  size_t field_count;
  void* release_ctx;
  void (*release)(void* release_ctx);
} PJ_catalog_snapshot_t;

typedef struct {
  PJ_data_source_handle_t source;
  PJ_topic_handle_t topic;
  PJ_field_handle_t field;
  PJ_primitive_type_t type;
  const int64_t* timestamps; /**< Nanoseconds since Unix epoch (1970-01-01T00:00:00Z). */
  size_t row_count;
  const uint8_t* validity_bits;
  size_t validity_size;
  PJ_series_values_t values;
  void* release_ctx;
  void (*release)(void* release_ctx);
} PJ_materialized_series_t;

typedef struct PJ_source_write_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;
  const char* (*get_last_error)(void* ctx);
  bool (*ensure_topic)(void* ctx, PJ_string_view_t topic_name, PJ_topic_handle_t* out_topic);
  bool (*ensure_field)(
      void* ctx, PJ_topic_handle_t topic, PJ_string_view_t field_name, PJ_primitive_type_t type,
      PJ_field_handle_t* out_field);
  bool (*append_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count);
  bool (*append_bound_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count);
  bool (*append_arrow_ipc)(
      void* ctx, PJ_topic_handle_t topic, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column);
} PJ_source_write_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_source_write_host_vtable_t* vtable;
} PJ_source_write_host_t;

typedef struct PJ_parser_write_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;
  const char* (*get_last_error)(void* ctx);
  bool (*ensure_field)(void* ctx, PJ_string_view_t field_name, PJ_primitive_type_t type, PJ_field_handle_t* out_field);
  bool (*append_record)(void* ctx, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count);
  bool (*append_bound_record)(void* ctx, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count);
  bool (*append_arrow_ipc)(void* ctx, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column);
} PJ_parser_write_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_parser_write_host_vtable_t* vtable;
} PJ_parser_write_host_t;

typedef struct PJ_toolbox_host_vtable_t {
  uint32_t abi_version;
  uint32_t struct_size;
  const char* (*get_last_error)(void* ctx);
  bool (*create_data_source)(void* ctx, PJ_string_view_t name, PJ_data_source_handle_t* out_source);
  bool (*ensure_topic)(
      void* ctx, PJ_data_source_handle_t source, PJ_string_view_t topic_name, PJ_topic_handle_t* out_topic);
  bool (*ensure_field)(
      void* ctx, PJ_topic_handle_t topic, PJ_string_view_t field_name, PJ_primitive_type_t type,
      PJ_field_handle_t* out_field);
  bool (*append_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_named_field_value_t* fields, size_t field_count);
  bool (*append_bound_record)(
      void* ctx, PJ_topic_handle_t topic, int64_t timestamp, const PJ_bound_field_value_t* fields, size_t field_count);
  bool (*append_arrow_ipc)(
      void* ctx, PJ_topic_handle_t topic, PJ_bytes_view_t ipc_stream, PJ_string_view_t timestamp_column);
  bool (*acquire_catalog_snapshot)(void* ctx, PJ_catalog_snapshot_t* out_snapshot);
  bool (*read_series)(void* ctx, PJ_field_handle_t field, PJ_materialized_series_t* out_series);

  /** Register a named colormap backed by a plugin-side callback.
   *  eval_fn receives a scalar value and returns a color string (CSS name or "#rrggbb").
   *  The returned pointer is plugin-owned and must remain valid until the next call.
   *  The host stores the callback; the chart renderer calls it per data point.
   *  Returns false if a colormap with the same name already exists. */
  bool (*register_colormap)(void* ctx, PJ_string_view_t name,
                            const char* (*eval_fn)(double value, void* user_ctx),
                            void* user_ctx);

  /** Unregister a previously registered colormap by name. */
  bool (*unregister_colormap)(void* ctx, PJ_string_view_t name);
} PJ_toolbox_host_vtable_t;

typedef struct {
  void* ctx;
  const PJ_toolbox_host_vtable_t* vtable;
} PJ_toolbox_host_t;

#ifdef __cplusplus
}
#endif

#endif
