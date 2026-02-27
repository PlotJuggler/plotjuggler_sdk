cmake_minimum_required(VERSION 3.20)
project(pj_dialog_protocol LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Header-only interface libraries ---

# Pure C ABI header (no deps)
add_library(pj_dialog_protocol INTERFACE)
target_include_directories(pj_dialog_protocol INTERFACE include)

# C++ SDK (adds nlohmann/json for widget_data/widget_event)
include(FetchContent)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(pj_dialog_sdk INTERFACE)
target_link_libraries(pj_dialog_sdk INTERFACE pj_dialog_protocol nlohmann_json::nlohmann_json)
target_include_directories(pj_dialog_sdk INTERFACE include)

# Host-side C++ API (depends on protocol header + nlohmann/json)
add_library(pj_dialog_host INTERFACE)
target_link_libraries(pj_dialog_host INTERFACE pj_dialog_protocol nlohmann_json::nlohmann_json)
target_include_directories(pj_dialog_host INTERFACE include)

# --- Mock plugin (shared library for integration testing) ---

add_library(mock_streamer_plugin SHARED examples/mock_streamer.cpp)
target_link_libraries(mock_streamer_plugin PRIVATE pj_dialog_sdk)

# --- Tests ---

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(widget_data_test tests/widget_data_test.cpp)
target_link_libraries(widget_data_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_data_test COMMAND widget_data_test)

add_executable(widget_event_test tests/widget_event_test.cpp)
target_link_libraries(widget_event_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_event_test COMMAND widget_event_test)

add_executable(dialog_plugin_typed_test tests/dialog_plugin_typed_test.cpp)
target_link_libraries(dialog_plugin_typed_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME dialog_plugin_typed_test COMMAND dialog_plugin_typed_test)

# Links mock_streamer.cpp statically (no dlopen) for lifecycle testing
add_executable(plugin_lifecycle_test tests/plugin_lifecycle_test.cpp examples/mock_streamer.cpp)
target_link_libraries(plugin_lifecycle_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME plugin_lifecycle_test COMMAND plugin_lifecycle_test)

# --- Host-side API tests ---

# Links mock_streamer.cpp statically for RAII handle testing
add_executable(dialog_handle_test tests/dialog_handle_test.cpp examples/mock_streamer.cpp)
target_link_libraries(dialog_handle_test PRIVATE pj_dialog_host pj_dialog_sdk GTest::gtest_main)
add_test(NAME dialog_handle_test COMMAND dialog_handle_test)

add_executable(widget_data_view_test tests/widget_data_view_test.cpp)
target_link_libraries(widget_data_view_test PRIVATE pj_dialog_host GTest::gtest_main)
add_test(NAME widget_data_view_test COMMAND widget_data_view_test)

# Round-trip test: build events with host API, parse with plugin-side WidgetEvent
add_executable(widget_event_builder_test tests/widget_event_builder_test.cpp)
target_link_libraries(widget_event_builder_test PRIVATE pj_dialog_host pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_event_builder_test COMMAND widget_event_builder_test)
