cmake_minimum_required(VERSION 3.22)
project(sdk_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk descriptor_import_support)

# A plugin is: add_library(SHARED) + link plugin_sdk + pj_configure_plugin().
# The helper (shipped with plugin_sdk) applies symbol isolation, the export
# allowlist + post-build gate, validates manifest.json, emits the sidecar and
# — with MANIFEST_HEADER — embeds manifest.json so it is the single source.
add_library(minimal_data_source SHARED minimal_data_source.cpp)
target_link_libraries(minimal_data_source PRIVATE plotjuggler_sdk::plugin_sdk)
pj_configure_plugin(minimal_data_source
  FAMILIES        data_source
  MANIFEST_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
  MANIFEST_HEADER generated/minimal_data_source_manifest.hpp
  MANIFEST_VAR    kMinimalManifest
)

add_library(dialog_controls_example SHARED dialog_controls.cpp)
target_link_libraries(dialog_controls_example PRIVATE plotjuggler_sdk::plugin_sdk)
pj_configure_plugin(dialog_controls_example
  FAMILIES        dialog
  MANIFEST_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/dialog_manifest.json
  MANIFEST_HEADER generated/dialog_controls_manifest.hpp
  MANIFEST_VAR    kManifestJson
)
# Keep the .ui editable in Qt Designer; embed it at configure time.
pj_embed_file(dialog_controls_example
  FILE     ${CMAKE_CURRENT_SOURCE_DIR}/dialog_controls.ui
  HEADER   generated/dialog_controls_ui.hpp
  VAR_NAME kUiContent
)

# Probe for the descriptor_import_support component: links the exported
# compiled target and touches one symbol per public header.
add_executable(descriptor_import_probe descriptor_import_probe.cpp)
target_link_libraries(descriptor_import_probe PRIVATE plotjuggler_sdk::descriptor_import_support)
