cmake_minimum_required(VERSION 3.22)
project(sdk_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(plotjuggler_sdk REQUIRED COMPONENTS plugin_sdk)

add_library(minimal_data_source SHARED minimal_data_source.cpp)
target_link_libraries(minimal_data_source PRIVATE plotjuggler_sdk::plugin_sdk)

# Exercise the cmake helper shipped alongside plugin_sdk.
pj_emit_plugin_manifest(minimal_data_source
  FAMILY        data_source
  MANIFEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
)
