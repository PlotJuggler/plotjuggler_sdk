cmake_minimum_required(VERSION 3.20)
project(pj_widget_protocol LANGUAGES CXX)

# The Widget family is the one place a plugin links a GUI toolkit. Nothing in
# this module does: the widget object crosses the ABI as an opaque void*.

# --- Header-only interface libraries ---

# Pure C ABI header (depends on pj_base for PJ_error_t, PJ_string_view_t)
add_library(pj_widget_protocol INTERFACE)
target_include_directories(pj_widget_protocol INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(pj_widget_protocol INTERFACE pj_base)
set_target_properties(pj_widget_protocol PROPERTIES EXPORT_NAME widget_protocol)

# C++ SDK for plugin authors (WidgetPluginBase, PJ_WIDGET_PLUGIN)
add_library(pj_widget_sdk INTERFACE)
target_compile_features(pj_widget_sdk INTERFACE cxx_std_20)
target_link_libraries(pj_widget_sdk INTERFACE pj_widget_protocol)
set_target_properties(pj_widget_sdk PROPERTIES EXPORT_NAME widget_sdk)

# ---------------------------------------------------------------------------
# pj_widget_host — host-side widget .so loader and handle
# ---------------------------------------------------------------------------

add_library(pj_widget_host STATIC
  src/widget_library.cpp
)
target_include_directories(pj_widget_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_widget_host PUBLIC cxx_std_20)
target_compile_options(pj_widget_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_widget_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME widget_host
)
target_link_libraries(pj_widget_host
  PUBLIC
    pj_widget_protocol
    pj_base
  PRIVATE
    pj_plugin_loader_detail
    ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# widget_qt_modules.json — the canonical module list for template CI
# ---------------------------------------------------------------------------
# Generated at configure time from widget_qt_modules.h, the single source, so
# the header and the installed JSON cannot drift. One X(Name, "since") per line.

set(_pj_qt_modules_header "${CMAKE_CURRENT_SOURCE_DIR}/include/pj_plugins/widget_qt_modules.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_pj_qt_modules_header}")
file(READ "${_pj_qt_modules_header}" _pj_qt_modules_text)
string(REGEX MATCHALL "[ \t]X\\([A-Za-z0-9_]+, \"[0-9]+\\.[0-9]+\\.[0-9]+\"\\)" _pj_qt_module_entries
  "${_pj_qt_modules_text}")
if(NOT _pj_qt_module_entries)
  message(FATAL_ERROR "widget_qt_modules.h: no X(Name, \"since\") entries found")
endif()
set(_pj_qt_module_json "")
foreach(_entry IN LISTS _pj_qt_module_entries)
  string(REGEX REPLACE "^[ \t]X\\(([A-Za-z0-9_]+), \"([0-9.]+)\"\\)$" "\\1;\\2" _pair "${_entry}")
  list(GET _pair 0 _name)
  list(GET _pair 1 _since)
  if(_pj_qt_module_json)
    string(APPEND _pj_qt_module_json ",\n")
  endif()
  string(APPEND _pj_qt_module_json "    {\"name\": \"${_name}\", \"since\": \"${_since}\"}")
endforeach()
set(PJ_WIDGET_QT_MODULES_JSON_FILE "${CMAKE_CURRENT_BINARY_DIR}/widget_qt_modules.json")
file(CONFIGURE OUTPUT "${PJ_WIDGET_QT_MODULES_JSON_FILE}" CONTENT
"{
  \"schema_version\": 1,
  \"modules\": [
${_pj_qt_module_json}
  ]
}
" @ONLY)

if(PJ_BUILD_TESTS)

# --- Mock plugin (shared library, Qt-free, for dlopen tests) ---
# pj_configure_plugin also runs the export-allowlist gate, which fails the build
# unless PJ_get_widget_vtable and pj_plugin_abi_version are exported.

set(_pj_mock_widget_manifest "${CMAKE_CURRENT_SOURCE_DIR}/examples/mock_widget_manifest.json")

add_library(mock_widget_plugin SHARED examples/mock_widget.cpp)
target_compile_options(mock_widget_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_include_directories(mock_widget_plugin PRIVATE examples)
target_link_libraries(mock_widget_plugin PRIVATE pj_widget_sdk)
pj_configure_plugin(mock_widget_plugin
  FAMILIES        widget
  MANIFEST_FILE   "${_pj_mock_widget_manifest}"
  MANIFEST_HEADER mock_widget_generated/mock_widget_manifest.hpp
  MANIFEST_VAR    kMockWidgetManifest
)

add_library(missing_widget_required_slots_plugin SHARED tests/missing_widget_required_slots_plugin.cpp)
target_compile_options(missing_widget_required_slots_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(missing_widget_required_slots_plugin PRIVATE pj_widget_protocol)

# --- Tests ---

enable_testing()

add_executable(widget_library_test tests/widget_library_test.cpp)
target_compile_definitions(widget_library_test PRIVATE
  PJ_MOCK_WIDGET_PLUGIN_PATH="$<TARGET_FILE:mock_widget_plugin>"
  PJ_MISSING_WIDGET_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_widget_required_slots_plugin>"
)
target_include_directories(widget_library_test PRIVATE examples)
# pj_plugin_sdk supplies host/service_registry_builder.hpp, which lives in the parent module.
# pj_plugin_loader_detail supplies detail/library_loader.hpp (the pinning tests open the library themselves).
target_link_libraries(widget_library_test PRIVATE pj_widget_host pj_plugin_sdk pj_plugin_loader_detail GTest::gtest_main)
add_dependencies(widget_library_test mock_widget_plugin missing_widget_required_slots_plugin)
add_test(NAME widget_library_test COMMAND widget_library_test)

# Links mock_widget.cpp statically with PJ_STATIC_PLUGINS (no dlopen) to test loadStatic; the test
# defines a second static widget plugin, so two of them share the binary.
add_executable(widget_static_library_test
  tests/widget_static_library_test.cpp
  examples/mock_widget.cpp
)
target_compile_definitions(widget_static_library_test PRIVATE PJ_STATIC_PLUGINS)
target_include_directories(widget_static_library_test PRIVATE examples)
target_link_libraries(widget_static_library_test PRIVATE pj_widget_host pj_widget_sdk pj_plugin_sdk GTest::gtest_main)
pj_embed_file(widget_static_library_test
  FILE     "${_pj_mock_widget_manifest}"
  HEADER   mock_widget_static_generated/mock_widget_manifest.hpp
  VAR_NAME kMockWidgetManifest
)
add_test(NAME widget_static_library_test COMMAND widget_static_library_test)

# Compile-time layout pins; the test is the successful build.
add_executable(widget_abi_layout_sentinels_test tests/widget_abi_layout_sentinels_test.cpp)
target_link_libraries(widget_abi_layout_sentinels_test PRIVATE pj_widget_protocol)
add_test(NAME widget_abi_layout_sentinels_test COMMAND widget_abi_layout_sentinels_test)

find_package(nlohmann_json REQUIRED)
add_executable(widget_qt_modules_test tests/widget_qt_modules_test.cpp)
target_compile_definitions(widget_qt_modules_test PRIVATE
  PJ_WIDGET_QT_MODULES_JSON_PATH="${PJ_WIDGET_QT_MODULES_JSON_FILE}"
)
target_link_libraries(widget_qt_modules_test PRIVATE pj_widget_protocol nlohmann_json::nlohmann_json GTest::gtest_main)
add_test(NAME widget_qt_modules_test COMMAND widget_qt_modules_test)

foreach(_test widget_library_test widget_static_library_test widget_qt_modules_test
        widget_abi_layout_sentinels_test)
  target_compile_options(${_test} PRIVATE ${PJ_WARNING_FLAGS})
endforeach()

endif() # PJ_BUILD_TESTS

# --- Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt) ---

if(PJ_INSTALL_SDK)
  install(TARGETS
      pj_widget_protocol
      pj_widget_sdk
      pj_widget_host
    EXPORT plotjuggler_sdkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  # Headers live under include/pj_plugins/ (widget_protocol.h, widget_qt_modules.h,
  # widget_types.hpp, sdk/, host/); the tree merges with the parent module's at the destination.
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES "${PJ_WIDGET_QT_MODULES_JSON_FILE}" DESTINATION share/plotjuggler_sdk)
endif()
