/**
 * @file abi_layout_sentinels_test.cpp
 * @brief Compile-time sentinels that pin the plugin C ABI layout.
 *
 * Every assertion here is a static_assert. A failure at compile time means
 * a struct defined in the ABI-visible headers has shifted in a way that
 * would silently break binary compatibility with existing plugins.
 *
 * Maintenance rule:
 *   - Sizes and alignments are allowed to GROW at the tail (new slots
 *     appended). In that case, update the `sizeof` and MIN-size assertions
 *     deliberately — the intent is "I appended a slot, the ABI is still
 *     backward-compatible because struct_size gates the read."
 *   - Offsets of existing fields MUST NOT CHANGE. A failing `offsetof`
 *     assertion means someone reordered fields, which is always an ABI
 *     break.
 *   - MIN-size constants (PJ_*_MIN_VTABLE_SIZE) MUST NEVER INCREASE
 *     within a major version. They are pinned at v4.0 release and are
 *     the floor that forward compatibility relies on within the current
 *     major series.
 *
 * Pinning target: x86-64 System V (Linux/macOS on Intel/AMD). For other
 * ABIs (ARM64, MSVC), either confirm identical layout during initial
 * port or add target-specific guards here.
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: MIT

#include <cstddef>
#include <cstdint>

#include "pj_base/data_source_protocol.h"
#include "pj_base/message_parser_protocol.h"
#include "pj_base/plugin_data_api.h"
#include "pj_base/toolbox_protocol.h"

// --- Word-size guard ---------------------------------------------------------
// The entire ABI is pinned to 64-bit. A 32-bit regression would shift every
// pointer-aligned field and silently invalidate every other assertion below.
static_assert(sizeof(void*) == 8, "v4 ABI pinned to 64-bit targets");

// --- Enum size guards --------------------------------------------------------
// Defends against `-fshort-enums` and similar flags that silently shrink
// enums below the 32-bit wire assumption.
static_assert(sizeof(PJ_primitive_type_t) == 4, "enum layout pinned");
static_assert(sizeof(PJ_data_source_state_t) == 4, "enum layout pinned");
static_assert(sizeof(PJ_data_source_message_level_t) == 4, "enum layout pinned");
static_assert(sizeof(PJ_message_box_type_t) == 4, "enum layout pinned");
static_assert(sizeof(PJ_toolbox_message_level_t) == 4, "enum layout pinned");

// --- PJ_error_t (ABI-FROZEN) -------------------------------------------------
static_assert(sizeof(PJ_error_t) == 304, "PJ_error_t size pinned at v4.0 release");
static_assert(alignof(PJ_error_t) == 8, "PJ_error_t alignment pinned");
static_assert(offsetof(PJ_error_t, code) == 0, "PJ_error_t layout pinned");
static_assert(offsetof(PJ_error_t, domain) == 4, "PJ_error_t layout pinned");
static_assert(offsetof(PJ_error_t, message) == 36, "PJ_error_t layout pinned");
static_assert(offsetof(PJ_error_t, extended) == 264, "PJ_error_t layout pinned");
static_assert(offsetof(PJ_error_t, extended_kind) == 272, "PJ_error_t layout pinned");

// --- Service registry (fat pointer types) ------------------------------------
static_assert(sizeof(PJ_service_t) == 16, "PJ_service_t fat pointer pinned");
static_assert(sizeof(PJ_service_registry_t) == 16, "PJ_service_registry_t fat pointer pinned");
static_assert(sizeof(PJ_borrowed_dialog_t) == 16, "PJ_borrowed_dialog_t fat pointer pinned");

// --- DataSource vtable (ABI-APPENDABLE within v4) ----------------------------
// Offsets of v4.0 slots: PINNED. sizeof and MIN_VTABLE_SIZE are allowed to
// grow at the tail via future appends within the current major series.
static_assert(offsetof(PJ_data_source_vtable_t, protocol_version) == 0, "v4 prefix pinned");
static_assert(offsetof(PJ_data_source_vtable_t, struct_size) == 4, "v4 prefix pinned");
static_assert(offsetof(PJ_data_source_vtable_t, bind) == 40, "v4 bind slot pinned");
static_assert(offsetof(PJ_data_source_vtable_t, start) == 64, "v4 lifecycle slot pinned");
static_assert(offsetof(PJ_data_source_vtable_t, get_dialog) == 112, "v4 get_dialog slot pinned");
static_assert(offsetof(PJ_data_source_vtable_t, get_plugin_extension) == 120, "v4 last baseline slot pinned");
static_assert(sizeof(PJ_data_source_vtable_t) == 128, "DataSource vtable size (update deliberately on append)");
static_assert(PJ_DATA_SOURCE_MIN_VTABLE_SIZE == 128, "MIN vtable size is pinned at v4.0 — NEVER INCREASE");
static_assert(
    PJ_DATA_SOURCE_MIN_VTABLE_SIZE <= sizeof(PJ_data_source_vtable_t),
    "MIN must never exceed current — host would reject its own vtable");

// --- MessageParser vtable (ABI-APPENDABLE within v4) -------------------------
static_assert(offsetof(PJ_message_parser_vtable_t, protocol_version) == 0, "v4 prefix pinned");
static_assert(offsetof(PJ_message_parser_vtable_t, struct_size) == 4, "v4 prefix pinned");
static_assert(offsetof(PJ_message_parser_vtable_t, bind) == 32, "v4 bind slot pinned");
static_assert(offsetof(PJ_message_parser_vtable_t, parse) == 64, "v4 parse slot pinned");
static_assert(offsetof(PJ_message_parser_vtable_t, get_plugin_extension) == 72, "v4 last baseline slot pinned");
// 80 baseline (v4.0) + 1 tail slot × 8 bytes = 88.
static_assert(sizeof(PJ_message_parser_vtable_t) == 88, "MessageParser vtable size (update deliberately on append)");
static_assert(PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE == 80, "MIN vtable size is pinned at v4.0 — NEVER INCREASE");
static_assert(PJ_MESSAGE_PARSER_MIN_VTABLE_SIZE <= sizeof(PJ_message_parser_vtable_t), "MIN must never exceed current");

// --- Toolbox vtable (ABI-APPENDABLE within v4) -------------------------------
static_assert(offsetof(PJ_toolbox_vtable_t, protocol_version) == 0, "v4 prefix pinned");
static_assert(offsetof(PJ_toolbox_vtable_t, struct_size) == 4, "v4 prefix pinned");
static_assert(offsetof(PJ_toolbox_vtable_t, bind) == 40, "v4 bind slot pinned");
static_assert(offsetof(PJ_toolbox_vtable_t, on_data_changed) == 72, "v4 on_data_changed slot pinned");
static_assert(offsetof(PJ_toolbox_vtable_t, get_plugin_extension) == 80, "v4 last baseline slot pinned");
static_assert(sizeof(PJ_toolbox_vtable_t) == 88, "Toolbox vtable size (update deliberately on append)");
static_assert(PJ_TOOLBOX_MIN_VTABLE_SIZE == 88, "MIN vtable size is pinned at v4.0 — NEVER INCREASE");
static_assert(PJ_TOOLBOX_MIN_VTABLE_SIZE <= sizeof(PJ_toolbox_vtable_t), "MIN must never exceed current");

// --- Canonical-object pipeline structs ---------------------------------------
// Public ABI types crossing the boundary for the v4 builtin-object pipeline.
// Sizes and offsets are pinned; any change is a deliberate ABI revision.
static_assert(sizeof(PJ_builtin_object_type_t) == 4, "enum layout pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_NONE == 0, "None type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_IMAGE == 1, "Image type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_POINTCLOUD == 3, "PointCloud type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_DEPTH_IMAGE == 4, "DepthImage type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_IMAGE_ANNOTATIONS == 5, "ImageAnnotations type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_FRAME_TRANSFORMS == 6, "FrameTransforms type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_OCCUPANCY_GRID == 7, "OccupancyGrid type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_COMPRESSED_POINTCLOUD == 8, "CompressedPointCloud type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_MESH3D == 9, "Mesh3D type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_VIDEO_FRAME == 10, "VideoFrame type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_SCENE_ENTITIES == 11, "SceneEntities type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_ASSET_VIDEO == 12, "AssetVideo type id pinned");
static_assert(PJ_BUILTIN_OBJECT_TYPE_ROBOT_DESCRIPTION == 13, "RobotDescription type id pinned");
static_assert(sizeof(PJ_schema_classification_t) == 4, "PJ_schema_classification_t layout pinned");
static_assert(offsetof(PJ_schema_classification_t, object_type) == 0, "object_type at offset 0");
static_assert(offsetof(PJ_schema_classification_t, reserved) == 2, "reserved at offset 2");

static_assert(sizeof(PJ_payload_anchor_t) == 16, "PJ_payload_anchor_t pinned (ctx + release fn ptr)");
static_assert(offsetof(PJ_payload_anchor_t, ctx) == 0, "ctx at offset 0");
static_assert(offsetof(PJ_payload_anchor_t, release) == 8, "release at offset 8");

static_assert(sizeof(PJ_payload_t) == 32, "PJ_payload_t pinned (data + size + anchor)");
static_assert(offsetof(PJ_payload_t, data) == 0, "data at offset 0");
static_assert(offsetof(PJ_payload_t, size) == 8, "size at offset 8");
static_assert(offsetof(PJ_payload_t, anchor) == 16, "anchor at offset 16");

static_assert(
    sizeof(PJ_message_data_fetcher_t) == 24, "PJ_message_data_fetcher_t pinned (ctx + fetchMessageData + release)");
static_assert(offsetof(PJ_message_data_fetcher_t, ctx) == 0, "ctx at offset 0");
static_assert(offsetof(PJ_message_data_fetcher_t, fetchMessageData) == 8, "fetchMessageData at offset 8");
static_assert(offsetof(PJ_message_data_fetcher_t, release) == 16, "release at offset 16");

// --- DataSource runtime host vtable (ABI-APPENDABLE within v4) ---------------
// The vtable the host exposes to plugins under "pj.runtime.v1". Offsets of
// existing slots are pinned; size grows deliberately as tail slots append.
static_assert(offsetof(PJ_data_source_runtime_host_vtable_t, protocol_version) == 0, "v1 prefix pinned");
static_assert(offsetof(PJ_data_source_runtime_host_vtable_t, struct_size) == 4, "v1 prefix pinned");
static_assert(offsetof(PJ_data_source_runtime_host_vtable_t, report_message) == 8, "v1 first slot pinned");
static_assert(
    offsetof(PJ_data_source_runtime_host_vtable_t, push_raw_message) == 72, "v1 push_raw_message slot pinned");
static_assert(
    offsetof(PJ_data_source_runtime_host_vtable_t, list_available_encodings) == 88,
    "v1 list_available_encodings slot pinned");
static_assert(
    offsetof(PJ_data_source_runtime_host_vtable_t, push_message_v2) == 96, "v1 push_message_v2 tail slot pinned");
static_assert(
    sizeof(PJ_data_source_runtime_host_vtable_t) == 104, "Runtime host vtable size (update deliberately on append)");

// --- Write-host vtables (ABI-APPENDABLE within v4) --------------------------
static_assert(offsetof(PJ_source_write_host_vtable_t, abi_version) == 0, "source write host prefix pinned");
static_assert(offsetof(PJ_source_write_host_vtable_t, struct_size) == 4, "source write host prefix pinned");
static_assert(offsetof(PJ_source_write_host_vtable_t, append_bound_record) == 32, "source write host baseline pinned");
static_assert(offsetof(PJ_source_write_host_vtable_t, append_arrow_stream) == 40, "source write host bulk slot pinned");
static_assert(sizeof(PJ_source_write_host_vtable_t) == 48, "Source write host size");

static_assert(offsetof(PJ_parser_write_host_vtable_t, abi_version) == 0, "parser write host prefix pinned");
static_assert(offsetof(PJ_parser_write_host_vtable_t, struct_size) == 4, "parser write host prefix pinned");
static_assert(offsetof(PJ_parser_write_host_vtable_t, append_bound_record) == 24, "parser write host baseline pinned");
static_assert(
    offsetof(PJ_parser_write_host_vtable_t, append_arrow_stream) == 32, "parser write host bulk tail slot pinned");
static_assert(sizeof(PJ_parser_write_host_vtable_t) == 40, "Parser write host size updated deliberately on append");
static_assert(PJ_PARSER_WRITE_HOST_MIN_VTABLE_SIZE == 32, "Parser write host min size remains v4.0 baseline");
static_assert(
    PJ_PARSER_WRITE_HOST_MIN_VTABLE_SIZE <= sizeof(PJ_parser_write_host_vtable_t), "MIN must never exceed current");

static_assert(offsetof(PJ_toolbox_host_vtable_t, abi_version) == 0, "toolbox host prefix pinned");
static_assert(offsetof(PJ_toolbox_host_vtable_t, struct_size) == 4, "toolbox host prefix pinned");
static_assert(offsetof(PJ_toolbox_host_vtable_t, append_bound_record) == 40, "toolbox host baseline pinned");
static_assert(offsetof(PJ_toolbox_host_vtable_t, append_arrow_stream) == 48, "toolbox host bulk slot pinned");
static_assert(offsetof(PJ_toolbox_host_vtable_t, read_series_arrow) == 64, "toolbox host read slot pinned");
static_assert(sizeof(PJ_toolbox_host_vtable_t) == 72, "Toolbox host size");

// --- ABI version symbol ------------------------------------------------------
static_assert(PJ_ABI_VERSION == 5, "v5 ABI version");

// This translation unit has no runtime behavior; the above are all
// compile-time assertions. Linking only confirms the TU compiled.
extern "C" void pj_abi_layout_sentinels_touch() {}
