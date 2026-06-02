cmake_minimum_required(VERSION 3.20)
project(pj_dialog_protocol LANGUAGES CXX)

# --- Header-only interface libraries ---

# Pure C ABI header (depends on pj_base for PJ_error_t, PJ_string_view_t)
add_library(pj_dialog_protocol INTERFACE)
target_include_directories(pj_dialog_protocol INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(pj_dialog_protocol INTERFACE pj_base)

# C++ SDK (adds nlohmann/json for widget_data/widget_event)
find_package(nlohmann_json REQUIRED)

add_library(pj_dialog_sdk INTERFACE)
target_compile_features(pj_dialog_sdk INTERFACE cxx_std_17)
# PJ_DIALOG_PLUGIN's variadic overload needs conformant __VA_ARGS__ expansion on MSVC.
target_compile_options(pj_dialog_sdk INTERFACE
  $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:preprocessor>
)
target_link_libraries(pj_dialog_sdk INTERFACE pj_dialog_protocol nlohmann_json::nlohmann_json)
target_include_directories(pj_dialog_sdk INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
set_target_properties(pj_dialog_sdk PROPERTIES EXPORT_NAME dialog_sdk)

# Host-side C++ API (depends on protocol header + nlohmann/json)
add_library(pj_dialog_host INTERFACE)
target_compile_features(pj_dialog_host INTERFACE cxx_std_17)
target_link_libraries(pj_dialog_host INTERFACE pj_dialog_protocol nlohmann_json::nlohmann_json)
target_include_directories(pj_dialog_host INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
set_target_properties(pj_dialog_host PROPERTIES EXPORT_NAME dialog_host)

# ---------------------------------------------------------------------------
# pj_dialog_library — host-side dialog .so loader
# ---------------------------------------------------------------------------

add_library(pj_dialog_library STATIC
  src/dialog_library.cpp
)
target_include_directories(pj_dialog_library PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_dialog_library PUBLIC cxx_std_20)
target_compile_options(pj_dialog_library PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_dialog_library PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME dialog_library
)
target_link_libraries(pj_dialog_library
  PUBLIC
    pj_dialog_host
    pj_base
  PRIVATE
    pj_plugin_loader_detail
    ${CMAKE_DL_LIBS}
)

if(PJ_BUILD_TESTS)

# --- Mock plugins (shared libraries for integration testing) ---

add_library(mock_dialog_plugin SHARED examples/mock_dialog.cpp)
target_compile_options(mock_dialog_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_dialog_plugin PRIVATE pj_dialog_sdk)

add_library(missing_dialog_abi_plugin SHARED tests/missing_dialog_abi_plugin.cpp)
target_compile_options(missing_dialog_abi_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(missing_dialog_abi_plugin PRIVATE pj_dialog_protocol)

add_library(missing_dialog_required_slots_plugin SHARED tests/missing_dialog_required_slots_plugin.cpp)
target_compile_options(missing_dialog_required_slots_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(missing_dialog_required_slots_plugin PRIVATE pj_dialog_protocol)

# --- Tests ---

enable_testing()

set(PJ_PLUGIN_TESTS
  widget_data_test
  widget_event_test
  dialog_plugin_typed_test
  plugin_lifecycle_test
  dialog_handle_test
  widget_data_view_test
  widget_event_builder_test
  dialog_library_test
)

add_executable(widget_data_test tests/widget_data_test.cpp)
target_link_libraries(widget_data_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_data_test COMMAND widget_data_test)

add_executable(widget_event_test tests/widget_event_test.cpp)
target_link_libraries(widget_event_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_event_test COMMAND widget_event_test)

add_executable(dialog_plugin_typed_test tests/dialog_plugin_typed_test.cpp)
target_link_libraries(dialog_plugin_typed_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME dialog_plugin_typed_test COMMAND dialog_plugin_typed_test)

# Links mock_dialog.cpp statically (no dlopen) for lifecycle testing
add_executable(plugin_lifecycle_test tests/plugin_lifecycle_test.cpp examples/mock_dialog.cpp)
target_link_libraries(plugin_lifecycle_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME plugin_lifecycle_test COMMAND plugin_lifecycle_test)

# --- Host-side API tests ---

# Links mock_dialog.cpp statically for RAII handle testing
add_executable(dialog_handle_test tests/dialog_handle_test.cpp examples/mock_dialog.cpp)
target_link_libraries(dialog_handle_test PRIVATE pj_dialog_host pj_dialog_sdk GTest::gtest_main)
add_test(NAME dialog_handle_test COMMAND dialog_handle_test)

add_executable(widget_data_view_test tests/widget_data_view_test.cpp)
target_link_libraries(widget_data_view_test PRIVATE pj_dialog_host GTest::gtest_main)
add_test(NAME widget_data_view_test COMMAND widget_data_view_test)

# Round-trip test: build events with host API, parse with plugin-side WidgetEvent
add_executable(widget_event_builder_test tests/widget_event_builder_test.cpp)
target_link_libraries(widget_event_builder_test PRIVATE pj_dialog_host pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_event_builder_test COMMAND widget_event_builder_test)

# DialogLibrary test (loads mock_dialog_plugin via dlopen)
add_executable(dialog_library_test tests/dialog_library_test.cpp)
target_compile_definitions(dialog_library_test PRIVATE
  PJ_MOCK_DIALOG_PLUGIN_PATH="$<TARGET_FILE:mock_dialog_plugin>"
  PJ_MISSING_DIALOG_ABI_PLUGIN_PATH="$<TARGET_FILE:missing_dialog_abi_plugin>"
  PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_dialog_required_slots_plugin>"
)
target_link_libraries(dialog_library_test PRIVATE
  pj_dialog_library pj_dialog_host pj_dialog_sdk GTest::gtest_main
)
add_dependencies(dialog_library_test
  mock_dialog_plugin
  missing_dialog_abi_plugin
  missing_dialog_required_slots_plugin
)
add_test(NAME dialog_library_test COMMAND dialog_library_test)

foreach(_test ${PJ_PLUGIN_TESTS})
  target_compile_options(${_test} PRIVATE ${PJ_WARNING_FLAGS})
endforeach()

endif() # PJ_BUILD_TESTS

# --- Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt) ---

if(PJ_INSTALL_SDK)
  install(TARGETS
      pj_dialog_protocol
      pj_dialog_sdk
      pj_dialog_host
      pj_dialog_library
    EXPORT plotjuggler_sdkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  # All dialog_protocol headers live under include/pj_plugins/ (dialog_protocol.h,
  # sdk/, host/). Install the whole tree — it merges with pj_plugins/include/
  # at the destination since each module writes into distinct files.
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
