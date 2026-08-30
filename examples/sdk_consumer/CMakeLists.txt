cmake_minimum_required(VERSION 3.22)
project(sdk_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk descriptor_import_support)

add_library(minimal_data_source SHARED minimal_data_source.cpp)
target_link_libraries(minimal_data_source PRIVATE plotjuggler_sdk::plugin_sdk)

# Exercise the cmake helper shipped alongside plugin_sdk.
pj_emit_plugin_manifest(minimal_data_source
  FAMILY        data_source
  MANIFEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
)

add_library(dialog_controls_example SHARED dialog_controls.cpp)
target_link_libraries(dialog_controls_example PRIVATE plotjuggler_sdk::plugin_sdk)

pj_emit_plugin_manifest(dialog_controls_example
  FAMILY        dialog
  MANIFEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/dialog_manifest.json
)

# Probe for the descriptor_import_support component: links the exported
# compiled target and touches one symbol per public header.
add_executable(descriptor_import_probe descriptor_import_probe.cpp)
target_link_libraries(descriptor_import_probe PRIVATE plotjuggler_sdk::descriptor_import_support)
