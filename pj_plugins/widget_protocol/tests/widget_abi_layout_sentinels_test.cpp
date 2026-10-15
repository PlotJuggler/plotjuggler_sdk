/**
 * @file widget_abi_layout_sentinels_test.cpp
 * @brief Compile-time sentinels that pin the Widget plugin C ABI layout.
 *
 * Same maintenance rule as pj_base/tests/abi_layout_sentinels_test.cpp: sizes
 * may grow at the tail when a slot or field is appended (update them
 * deliberately); offsets of existing members never change;
 * PJ_WIDGET_MIN_VTABLE_SIZE never increases. This lives next to the protocol
 * rather than in pj_base because the Widget family is a pj_plugins module.
 *
 * Pinning target: x86-64 System V (64-bit pointers).
 */
// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>

#include "pj_plugins/widget_protocol.h"

static_assert(sizeof(void*) == 8, "ABI pinned to 64-bit targets");

// --- Enums -------------------------------------------------------------------
static_assert(sizeof(PJ_widget_input_kind_t) == 4, "enum layout pinned");
static_assert(sizeof(PJ_widget_value_kind_t) == 4, "enum layout pinned");
static_assert(PJ_WIDGET_INPUT_NUMBER == 0 && PJ_WIDGET_INPUT_TEXT == 1 && PJ_WIDGET_INPUT_OBJECT == 2, "values pinned");
static_assert(
    PJ_WIDGET_VALUE_NONE == 0 && PJ_WIDGET_VALUE_NUMBER == 1 && PJ_WIDGET_VALUE_TEXT == 2 &&
        PJ_WIDGET_VALUE_OBJECT == 3,
    "values pinned");

// --- PJ_widget_input_t (ABI-APPENDABLE) --------------------------------------
static_assert(offsetof(PJ_widget_input_t, struct_size) == 0, "prefix pinned");
static_assert(offsetof(PJ_widget_input_t, name) == 8, "layout pinned");
static_assert(offsetof(PJ_widget_input_t, label) == 24, "layout pinned");
static_assert(offsetof(PJ_widget_input_t, unit) == 40, "layout pinned");
static_assert(offsetof(PJ_widget_input_t, kind) == 56, "layout pinned");
static_assert(offsetof(PJ_widget_input_t, multiple) == 60, "layout pinned");
static_assert(sizeof(PJ_widget_input_t) == 64, "size (update deliberately on append)");

// --- PJ_widget_value_t (ABI-APPENDABLE) --------------------------------------
static_assert(offsetof(PJ_widget_value_t, struct_size) == 0, "prefix pinned");
static_assert(offsetof(PJ_widget_value_t, input) == 4, "layout pinned");
static_assert(offsetof(PJ_widget_value_t, slot) == 8, "layout pinned");
static_assert(offsetof(PJ_widget_value_t, kind) == 12, "layout pinned");
static_assert(offsetof(PJ_widget_value_t, time) == 16, "layout pinned");
static_assert(offsetof(PJ_widget_value_t, number) == 24, "layout pinned");
static_assert(offsetof(PJ_widget_value_t, text) == 32, "layout pinned");
static_assert(offsetof(PJ_widget_value_t, bytes) == 48, "layout pinned");
static_assert(offsetof(PJ_widget_value_t, type_name) == 64, "layout pinned");
static_assert(offsetof(PJ_widget_value_t, label) == 80, "layout pinned");
static_assert(sizeof(PJ_widget_value_t) == 96, "size (update deliberately on append)");

// --- PJ_widget_build_info_t (ABI-APPENDABLE) ---------------------------------
static_assert(offsetof(PJ_widget_build_info_t, struct_size) == 0, "prefix pinned");
static_assert(offsetof(PJ_widget_build_info_t, qt_major) == 4, "layout pinned");
static_assert(offsetof(PJ_widget_build_info_t, qt_minor) == 8, "layout pinned");
static_assert(offsetof(PJ_widget_build_info_t, qt_patch) == 12, "layout pinned");
static_assert(offsetof(PJ_widget_build_info_t, flags) == 16, "layout pinned");
static_assert(offsetof(PJ_widget_build_info_t, compiler) == 24, "layout pinned");
static_assert(offsetof(PJ_widget_build_info_t, qt_version_str) == 40, "layout pinned");
static_assert(sizeof(PJ_widget_build_info_t) == 56, "size (update deliberately on append)");
static_assert(PJ_WIDGET_BUILD_DEBUG == 1, "flag bit pinned");

// --- PJ_widget_host_vtable_t (ABI-APPENDABLE, service "pj.widget_host.v1") ---
static_assert(offsetof(PJ_widget_host_vtable_t, protocol_version) == 0, "prefix pinned");
static_assert(offsetof(PJ_widget_host_vtable_t, struct_size) == 4, "prefix pinned");
static_assert(offsetof(PJ_widget_host_vtable_t, query_history) == 8, "slot pinned");
static_assert(offsetof(PJ_widget_host_vtable_t, notify_inputs_changed) == 16, "slot pinned");
static_assert(offsetof(PJ_widget_host_vtable_t, report_widget_message) == 24, "slot pinned");
static_assert(sizeof(PJ_widget_host_vtable_t) == 32, "size (update deliberately on append)");
static_assert(sizeof(PJ_widget_host_t) == 16, "fat pointer pinned");

// --- PJ_widget_vtable_t (ABI-APPENDABLE) -------------------------------------
static_assert(offsetof(PJ_widget_vtable_t, protocol_version) == 0, "prefix pinned");
static_assert(offsetof(PJ_widget_vtable_t, struct_size) == 4, "prefix pinned");
static_assert(offsetof(PJ_widget_vtable_t, create) == 8, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, destroy) == 16, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, manifest_json) == 24, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, build_info) == 32, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, bind) == 40, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, inputs) == 48, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, create_qwidget) == 56, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, on_update) == 64, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, save_config) == 72, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, load_config) == 80, "slot pinned");
static_assert(offsetof(PJ_widget_vtable_t, get_plugin_extension) == 88, "last v1.0 slot pinned");
static_assert(sizeof(PJ_widget_vtable_t) == 96, "size (update deliberately on append)");
static_assert(PJ_WIDGET_MIN_VTABLE_SIZE == 96, "MIN vtable size is pinned at v1.0 - NEVER INCREASE");
static_assert(PJ_WIDGET_MIN_VTABLE_SIZE <= sizeof(PJ_widget_vtable_t), "MIN must never exceed current");

// Compile-time only; linking confirms the translation unit built.
int main() {
  return 0;
}
