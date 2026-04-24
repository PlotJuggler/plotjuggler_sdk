/**
 * @file abi_layout_sentinels_test.cpp
 * @brief Compile-time sentinels that pin the v4 plugin ABI layout.
 *
 * Every assertion here is a static_assert. A failure at compile time means
 * a struct defined in the ABI-visible headers has shifted in a way that
 * would silently break binary compatibility with existing v4 plugins.
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
 *     the floor that forward compatibility relies on within the v4 series.
 *
 * Pinning target: x86-64 System V (Linux/macOS on Intel/AMD). For other
 * ABIs (ARM64, MSVC), either confirm identical layout during initial
 * port or add target-specific guards here.
 */
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
// grow at the tail via future appends within the v4 series.
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
static_assert(sizeof(PJ_message_parser_vtable_t) == 80, "MessageParser vtable size (update deliberately on append)");
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

// --- ABI version symbol ------------------------------------------------------
static_assert(PJ_ABI_VERSION == 4, "v4 ABI version");

// This translation unit has no runtime behavior; the above are all
// compile-time assertions. Linking only confirms the TU compiled.
extern "C" void pj_abi_layout_sentinels_touch() {}
