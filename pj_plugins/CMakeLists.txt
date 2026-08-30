find_package(nlohmann_json REQUIRED)

set(PJ_WASMER_ROOT "$ENV{PJ_WASMER_ROOT}" CACHE PATH
  "Wasmer 7.0.1 C-API root used for wasm parser-module execution")
set(_pj_wasmer_include "${PJ_WASMER_ROOT}/include")
find_file(_pj_wasmer_library
  NAMES libwasmer.a libwasmer.lib wasmer.lib
  PATHS "${PJ_WASMER_ROOT}/lib"
  NO_DEFAULT_PATH
  NO_CACHE)
set(PJ_WASMER_AVAILABLE OFF)
if(NOT PJ_WASMER_ROOT)
  message(STATUS
    "Wasm parser-module loader skipped: PJ_WASMER_ROOT cache/env variable is not set")
elseif(NOT EXISTS "${_pj_wasmer_include}/wasm.h" OR
       NOT EXISTS "${_pj_wasmer_include}/wasmer.h" OR
       NOT _pj_wasmer_library)
  message(STATUS
    "Wasm parser-module loader skipped: Wasmer C-API is missing under PJ_WASMER_ROOT=${PJ_WASMER_ROOT}")
else()
  file(STRINGS "${_pj_wasmer_include}/wasmer.h" _pj_wasmer_version_line
    REGEX "^#define WASMER_VERSION ")
  if(NOT _pj_wasmer_version_line MATCHES "\"7\\.0\\.1\"")
    message(FATAL_ERROR
      "Wasm parser-module loader requires Wasmer 7.0.1; found '${_pj_wasmer_version_line}'")
  endif()
  find_package(Threads REQUIRED)
  add_library(pj_wasmer_static UNKNOWN IMPORTED)
  set_target_properties(pj_wasmer_static PROPERTIES
    IMPORTED_LOCATION "${_pj_wasmer_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${_pj_wasmer_include}"
  )
  set(PJ_WASMER_AVAILABLE ON)
  message(STATUS "Wasm parser-module loader enabled with Wasmer 7.0.1: ${PJ_WASMER_ROOT}")
endif()
# The wasm executor is an in-tree component: the application builds this SDK
# from source with PJ_WASMER_ROOT set. Installed packages stay wasmer-free (they
# still ship the wasm authoring preset and pj-wasm-embed-manifest), so nothing
# below exports a target that would reference pj_wasmer_static.

add_library(pj_plugin_loader_detail STATIC
  src/detail/vtable_validation.cpp
)
target_include_directories(pj_plugin_loader_detail PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dialog_protocol/include>
)
target_compile_features(pj_plugin_loader_detail PUBLIC cxx_std_20)
target_compile_options(pj_plugin_loader_detail PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_plugin_loader_detail PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME plugin_loader_detail
)
target_link_libraries(pj_plugin_loader_detail PUBLIC pj_base)

add_subdirectory(dialog_protocol)

# ---------------------------------------------------------------------------
# pj_plugin_sdk — umbrella INTERFACE library that exposes the full plugin-author
# SDK surface: C++ SDK headers (MessageParserPluginBase, ObjectIngestPolicy,
# parser trampolines, ...) PLUS the dialog SDK (widget_data, widget_event) so
# plugin authors can link one target and get everything they need.
# ---------------------------------------------------------------------------

add_library(pj_plugin_sdk INTERFACE)
target_include_directories(pj_plugin_sdk INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(pj_plugin_sdk INTERFACE pj_base pj_dialog_sdk)
set_target_properties(pj_plugin_sdk PROPERTIES EXPORT_NAME plugin_sdk)
add_library(plotjuggler_sdk::plugin_sdk ALIAS pj_plugin_sdk)

# ---------------------------------------------------------------------------
# pj_plugin_catalog — host-side embedded-manifest discovery
# ---------------------------------------------------------------------------

add_library(pj_plugin_catalog STATIC
  src/plugin_catalog.cpp
)
target_include_directories(pj_plugin_catalog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_plugin_catalog PUBLIC cxx_std_20)
target_compile_options(pj_plugin_catalog PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_plugin_catalog PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME plugin_catalog
)
target_link_libraries(pj_plugin_catalog
  PUBLIC
    pj_base
  PRIVATE
    pj_plugin_loader_detail
    pj_dialog_protocol
    $<BUILD_INTERFACE:pj_internal_fmt>
    ${CMAKE_DL_LIBS}
    nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------------------------
# pj_parser_claim_catalog — host-side parser claim admission and resolution
# ---------------------------------------------------------------------------

add_library(pj_parser_claim_catalog STATIC
  src/parser_claim_catalog.cpp
  src/parser_route_resolver.cpp
)
target_include_directories(pj_parser_claim_catalog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_parser_claim_catalog PUBLIC cxx_std_20)
target_compile_options(pj_parser_claim_catalog PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_parser_claim_catalog PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME parser_claim_catalog
)
target_link_libraries(pj_parser_claim_catalog
  PUBLIC
    pj_base
  PRIVATE
    nlohmann_json::nlohmann_json
)
add_library(plotjuggler_sdk::parser_claim_catalog ALIAS pj_parser_claim_catalog)

# ---------------------------------------------------------------------------
# pj_parser_module_host — native functional-module loader and runtime
# ---------------------------------------------------------------------------

add_library(pj_parser_module_host STATIC
  src/native_parser_module.cpp
  src/parser_module_runtime.cpp
  src/parser_module_session_budget.cpp
)
target_include_directories(pj_parser_module_host
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(pj_parser_module_host PUBLIC cxx_std_20)
target_compile_options(pj_parser_module_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_parser_module_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME parser_module_host
)
target_link_libraries(pj_parser_module_host
  PUBLIC
    pj_base
  PRIVATE
    pj_parser_claim_catalog
    ${CMAKE_DL_LIBS}
)
add_library(plotjuggler_sdk::parser_module_host ALIAS pj_parser_module_host)

if(PJ_WASMER_AVAILABLE)
  add_library(pj_wasm_parser_module_host STATIC
    src/wasm_parser_module.cpp
    src/wasm_parser_module_runtime.cpp
  )
  target_include_directories(pj_wasm_parser_module_host
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_features(pj_wasm_parser_module_host PUBLIC cxx_std_20)
  target_compile_options(pj_wasm_parser_module_host PRIVATE ${PJ_WARNING_FLAGS})
  set_target_properties(pj_wasm_parser_module_host PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME wasm_parser_module_host
  )
  target_link_libraries(pj_wasm_parser_module_host
    PUBLIC
      pj_parser_module_host
    PRIVATE
      pj_wasmer_static
      Threads::Threads
      ${CMAKE_DL_LIBS}
  )
  if(UNIX)
    target_link_libraries(pj_wasm_parser_module_host PRIVATE m)
  endif()
  add_library(plotjuggler_sdk::wasm_parser_module_host ALIAS pj_wasm_parser_module_host)
endif()

# ---------------------------------------------------------------------------
# pj_data_source_host — host-side DataSource library loader
# ---------------------------------------------------------------------------

add_library(pj_data_source_host STATIC
  src/data_source_library.cpp
)
target_include_directories(pj_data_source_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_data_source_host PUBLIC cxx_std_20)
target_compile_options(pj_data_source_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_data_source_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME data_source_host
)
target_link_libraries(pj_data_source_host
  PUBLIC
    pj_base
    pj_dialog_protocol
    pj_plugin_catalog
  PRIVATE
    pj_plugin_loader_detail
    ${CMAKE_DL_LIBS}
)

if(PJ_BUILD_TESTS)

# ---------------------------------------------------------------------------
# Mock DataSource plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_data_source_plugin SHARED examples/mock_data_source.cpp)
target_compile_features(mock_data_source_plugin PRIVATE cxx_std_20)
target_compile_options(mock_data_source_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_data_source_plugin PRIVATE pj_base)

# ---------------------------------------------------------------------------
# Combined DataSource+Dialog mock plugin (shared library)
# ---------------------------------------------------------------------------

add_library(mock_source_with_dialog_plugin SHARED examples/mock_source_with_dialog.cpp)
target_compile_features(mock_source_with_dialog_plugin PRIVATE cxx_std_20)
target_compile_options(mock_source_with_dialog_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_source_with_dialog_plugin PRIVATE pj_base pj_dialog_sdk)

# ---------------------------------------------------------------------------
# FileSourceBase mock plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_file_source_plugin SHARED examples/mock_file_source.cpp)
target_compile_features(mock_file_source_plugin PRIVATE cxx_std_20)
target_compile_options(mock_file_source_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_file_source_plugin PRIVATE pj_base)

add_library(missing_id_data_source_plugin SHARED tests/missing_id_data_source_plugin.cpp)
target_compile_features(missing_id_data_source_plugin PRIVATE cxx_std_20)
target_compile_options(missing_id_data_source_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(missing_id_data_source_plugin PRIVATE pj_base)

add_library(invalid_optional_manifest_data_source_plugin SHARED tests/invalid_optional_manifest_data_source_plugin.cpp)
target_compile_features(invalid_optional_manifest_data_source_plugin PRIVATE cxx_std_20)
target_compile_options(invalid_optional_manifest_data_source_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(invalid_optional_manifest_data_source_plugin PRIVATE pj_base)

add_library(missing_required_slots_plugin SHARED tests/missing_required_slots_plugin.cpp)
target_compile_features(missing_required_slots_plugin PRIVATE cxx_std_20)
target_compile_options(missing_required_slots_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(missing_required_slots_plugin PRIVATE pj_base)

endif() # PJ_BUILD_TESTS

# ---------------------------------------------------------------------------
# pj_message_parser_host — host-side MessageParser library loader
# ---------------------------------------------------------------------------

add_library(pj_message_parser_host STATIC
  src/message_parser_library.cpp
)
target_include_directories(pj_message_parser_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_message_parser_host PUBLIC cxx_std_20)
target_compile_options(pj_message_parser_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_message_parser_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME message_parser_host
)
target_link_libraries(pj_message_parser_host
  PUBLIC
    pj_base
    pj_dialog_protocol
  PRIVATE
    pj_plugin_loader_detail
    ${CMAKE_DL_LIBS}
)

if(PJ_BUILD_TESTS)

# ---------------------------------------------------------------------------
# Mock MessageParser plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_json_parser_plugin SHARED examples/mock_json_parser.cpp)
target_compile_features(mock_json_parser_plugin PRIVATE cxx_std_20)
target_compile_options(mock_json_parser_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_json_parser_plugin PRIVATE pj_plugin_sdk)

# ---------------------------------------------------------------------------
# Mock Schema Parser plugin (shared library, high-throughput bound-fields example)
# ---------------------------------------------------------------------------

add_library(mock_schema_parser_plugin SHARED examples/mock_schema_parser.cpp)
target_compile_features(mock_schema_parser_plugin PRIVATE cxx_std_20)
target_compile_options(mock_schema_parser_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_schema_parser_plugin PRIVATE pj_plugin_sdk)

add_library(functional_image_parser_plugin SHARED tests/functional_image_parser_plugin.cpp)
target_compile_features(functional_image_parser_plugin PRIVATE cxx_std_20)
target_compile_options(functional_image_parser_plugin PRIVATE ${PJ_WARNING_FLAGS})
# Make the lifecycle test prove a real dlclose. GNU-unique C++ symbols cause
# glibc to retain a DSO even after its last explicit loader lease is gone.
target_compile_options(functional_image_parser_plugin PRIVATE
  $<$<AND:$<CXX_COMPILER_ID:GNU>,$<PLATFORM_ID:Linux>>:-fno-gnu-unique>
)
target_link_libraries(functional_image_parser_plugin PRIVATE pj_plugin_sdk)

endif() # PJ_BUILD_TESTS

# ---------------------------------------------------------------------------
# pj_toolbox_host — host-side Toolbox library loader
# ---------------------------------------------------------------------------

add_library(pj_toolbox_host STATIC
  src/toolbox_library.cpp
)
target_include_directories(pj_toolbox_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_toolbox_host PUBLIC cxx_std_20)
target_compile_options(pj_toolbox_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_toolbox_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME toolbox_host
)
target_link_libraries(pj_toolbox_host
  PUBLIC
    pj_base
    pj_dialog_protocol
  PRIVATE
    pj_plugin_loader_detail
    ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# pj_plugin_host — umbrella INTERFACE that bundles every host-side loader.
# Hosts (the PlotJuggler app, test rigs) link this single target to get
# discovery, all four plugin-family loaders, and functional parser-module
# execution. The duplicate-resolution catalog is host policy and lives in the
# app (pj_runtime), built on these primitives.
# ---------------------------------------------------------------------------

add_library(pj_plugin_host INTERFACE)
target_link_libraries(pj_plugin_host INTERFACE
  pj_data_source_host
  pj_message_parser_host
  pj_toolbox_host
  pj_dialog_library
  pj_plugin_catalog
  pj_parser_claim_catalog
  pj_parser_module_host
)
if(PJ_WASMER_AVAILABLE)
  target_link_libraries(pj_plugin_host INTERFACE $<BUILD_INTERFACE:pj_wasm_parser_module_host>)
endif()
set_target_properties(pj_plugin_host PROPERTIES EXPORT_NAME plugin_host)
add_library(plotjuggler_sdk::plugin_host ALIAS pj_plugin_host)

if(PJ_BUILD_TESTS)

# ---------------------------------------------------------------------------
# Native functional parser-module fixtures (real shared libraries)
# ---------------------------------------------------------------------------

# Trailing arguments become PRIVATE compile definitions, each selecting one
# deliberately defective variant of the shared fixture source.
function(pj_add_native_parser_module_fixture target)
  add_library(${target} SHARED tests/native_parser_module_fixture.cpp)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_options(${target} PRIVATE ${PJ_WARNING_FLAGS})
  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
  )
  target_link_libraries(${target} PRIVATE pj_base)
  if(ARGN)
    target_compile_definitions(${target} PRIVATE ${ARGN})
  endif()
  if(UNIX AND NOT APPLE)
    target_link_options(${target} PRIVATE "LINKER:-z,defs" "LINKER:--exclude-libs,ALL")
  endif()
endfunction()

if(PJ_WASMER_AVAILABLE)
  add_executable(wasmer_shared_module_prototype_test tests/wasmer_shared_module_prototype_test.cpp)
  target_compile_options(wasmer_shared_module_prototype_test PRIVATE ${PJ_WARNING_FLAGS})
  target_link_libraries(wasmer_shared_module_prototype_test PRIVATE
    pj_wasmer_static Threads::Threads ${CMAKE_DL_LIBS} GTest::gtest_main)
  if(UNIX)
    target_link_libraries(wasmer_shared_module_prototype_test PRIVATE m)
  endif()
  add_test(NAME wasmer_shared_module_prototype_test COMMAND wasmer_shared_module_prototype_test)
  set_tests_properties(wasmer_shared_module_prototype_test PROPERTIES LABELS "parser_module;wasmer")

  if(TARGET parser_module_wasm_conformance_fixture)
    get_property(_pj_toy_wasm_path TARGET parser_module_wasm_conformance_fixture
      PROPERTY PJ_PARSER_MODULE_WASM_FIXTURE_PATH)
    add_executable(wasm_parser_module_test tests/wasm_parser_module_test.cpp)
    add_dependencies(wasm_parser_module_test parser_module_wasm_conformance_fixture)
    target_compile_definitions(wasm_parser_module_test PRIVATE
      PJ_TOY_CDR_POINTCLOUD_WASM_PATH="${_pj_toy_wasm_path}")
    target_compile_options(wasm_parser_module_test PRIVATE ${PJ_WARNING_FLAGS})
    target_link_libraries(wasm_parser_module_test PRIVATE
      pj_wasm_parser_module_host pj_parser_claim_catalog GTest::gtest_main)
    add_test(NAME wasm_parser_module_test COMMAND wasm_parser_module_test)
    set_tests_properties(wasm_parser_module_test PROPERTIES LABELS "parser_module;wasmer;wasi")

    pj_add_parser_module(adversarial_wasm_parser_module
      SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/adversarial_wasm_parser_module.cpp"
      MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/adversarial_wasm_parser_module.module.json"
      TARGETS wasm
    )
    get_property(_pj_adversarial_wasm_path TARGET adversarial_wasm_parser_module_wasm
      PROPERTY PJ_PARSER_MODULE_WASM_PATH)
    add_executable(wasm_parser_module_hardening_test
      tests/wasm_parser_module_hardening_test.cpp)
    add_dependencies(wasm_parser_module_hardening_test adversarial_wasm_parser_module_wasm)
    target_compile_definitions(wasm_parser_module_hardening_test PRIVATE
      PJ_ADVERSARIAL_WASM_PATH="${_pj_adversarial_wasm_path}")
    target_compile_options(wasm_parser_module_hardening_test PRIVATE ${PJ_WARNING_FLAGS})
    target_link_libraries(wasm_parser_module_hardening_test PRIVATE
      pj_wasm_parser_module_host GTest::gtest_main)
    add_test(NAME wasm_parser_module_hardening_test COMMAND wasm_parser_module_hardening_test)
    set_tests_properties(wasm_parser_module_hardening_test PROPERTIES
      LABELS "parser_module;wasmer;wasi")
  else()
    message(STATUS "Wasm parser-module E2E skipped: the wasi-sdk fixture target is unavailable")
  endif()
endif()

pj_add_native_parser_module_fixture(native_parser_module_fixture)
pj_add_native_parser_module_fixture(native_parser_module_missing_export PJ_FIXTURE_OMIT_FREE)
pj_add_native_parser_module_fixture(native_parser_module_wrong_abi PJ_FIXTURE_WRONG_ABI)
pj_add_native_parser_module_fixture(native_parser_module_unreadable_manifest PJ_FIXTURE_UNREADABLE_MANIFEST)

# ---------------------------------------------------------------------------
# Mock Toolbox plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_toolbox_plugin SHARED examples/mock_toolbox.cpp)
target_compile_features(mock_toolbox_plugin PRIVATE cxx_std_20)
target_compile_options(mock_toolbox_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_toolbox_plugin PRIVATE pj_base)

add_library(static_manifest_dialog_plugin SHARED tests/static_manifest_dialog_plugin.cpp)
target_compile_features(static_manifest_dialog_plugin PRIVATE cxx_std_20)
target_compile_options(static_manifest_dialog_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(static_manifest_dialog_plugin PRIVATE pj_dialog_protocol)

add_library(legacy_macro_dialog_plugin SHARED tests/legacy_macro_dialog_plugin.cpp)
target_compile_features(legacy_macro_dialog_plugin PRIVATE cxx_std_20)
target_compile_options(legacy_macro_dialog_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(legacy_macro_dialog_plugin PRIVATE pj_dialog_protocol)

# First two-DSO loader fixtures: one malformed candidate whose entry points
# come only from a dependency, and one well-formed candidate that defines its
# own entry points while retaining the same dependency.
add_library(entry_point_donor SHARED tests/entry_point_donor.cpp)
target_compile_features(entry_point_donor PRIVATE cxx_std_20)
target_compile_options(entry_point_donor PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(entry_point_donor PRIVATE pj_base)
# The PE forwarder pragma in entry_point_forwarder.cpp names entry_point_donor.dll
# at runtime. Pin the output basename so it cannot drift from that module token.
set_target_properties(entry_point_donor PROPERTIES OUTPUT_NAME entry_point_donor)

add_library(entry_point_via_dependency_plugin SHARED tests/entry_point_via_dependency.cpp)
target_compile_features(entry_point_via_dependency_plugin PRIVATE cxx_std_20)
target_compile_options(entry_point_via_dependency_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(entry_point_via_dependency_plugin PRIVATE entry_point_donor pj_base)

add_library(entry_point_with_own_exports_plugin SHARED tests/entry_point_with_own_exports.cpp)
target_compile_features(entry_point_with_own_exports_plugin PRIVATE cxx_std_20)
target_compile_options(entry_point_with_own_exports_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(entry_point_with_own_exports_plugin PRIVATE entry_point_donor pj_base)

if(WIN32)
  add_library(entry_point_forwarder_plugin SHARED
    tests/entry_point_forwarder.cpp
  )
  target_compile_features(entry_point_forwarder_plugin PRIVATE cxx_std_20)
  target_compile_options(entry_point_forwarder_plugin PRIVATE ${PJ_WARNING_FLAGS})
  # Deliberately do not link entry_point_donor: the /export pragma in the source
  # emits a PE forwarder, not an import the linker should resolve. Both targets
  # share one runtime output directory, so entry_point_donor.dll is available
  # when GetProcAddress follows the forwarder.
  target_link_libraries(entry_point_forwarder_plugin PRIVATE pj_base)
  add_dependencies(entry_point_forwarder_plugin entry_point_donor)
  set_target_properties(
    entry_point_donor
    entry_point_via_dependency_plugin
    entry_point_with_own_exports_plugin
    entry_point_forwarder_plugin
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/entry_point_fixtures"
  )
endif()

# Dependency-search fixtures. The real and decoy dependencies intentionally
# share one filename but live in separate build directories.
add_library(dependency_search_real SHARED tests/dependency_search_dependency.cpp)
target_compile_features(dependency_search_real PRIVATE cxx_std_20)
target_compile_options(dependency_search_real PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(dependency_search_real PROPERTIES
  OUTPUT_NAME pj_dependency_search_fixture
  # Keep this DLL out of the test executable's directory: on Windows the
  # loader's LOAD_LIBRARY_SEARCH_APPLICATION_DIR leg would otherwise resolve
  # the dependency from there and defeat the decoy-only case.
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dependency_search_real"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dependency_search_real"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dependency_search_real"
)

add_library(dependency_search_decoy SHARED tests/dependency_search_dependency.cpp)
target_compile_features(dependency_search_decoy PRIVATE cxx_std_20)
target_compile_options(dependency_search_decoy PRIVATE ${PJ_WARNING_FLAGS})
target_compile_definitions(dependency_search_decoy PRIVATE PJ_DEPENDENCY_SEARCH_DECOY)
set_target_properties(dependency_search_decoy PROPERTIES
  OUTPUT_NAME pj_dependency_search_fixture
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dependency_search_decoy"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dependency_search_decoy"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dependency_search_decoy"
)

add_library(dependency_search_candidate_plugin SHARED tests/dependency_search_candidate.cpp)
target_compile_features(dependency_search_candidate_plugin PRIVATE cxx_std_20)
target_compile_options(dependency_search_candidate_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(dependency_search_candidate_plugin PRIVATE dependency_search_real pj_base)
if(APPLE)
  # Keep this copied fixture package-relative without CMake appending the
  # absolute build directory, which would invalidate the decoy-only case.
  set_target_properties(dependency_search_candidate_plugin PROPERTIES SKIP_BUILD_RPATH TRUE)
  target_link_options(dependency_search_candidate_plugin PRIVATE "LINKER:-rpath,@loader_path")
elseif(UNIX)
  set_target_properties(dependency_search_candidate_plugin PROPERTIES
    BUILD_RPATH "$ORIGIN"
    BUILD_RPATH_USE_ORIGIN TRUE
  )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

add_executable(data_source_library_test tests/data_source_library_test.cpp)
add_dependencies(data_source_library_test mock_data_source_plugin missing_required_slots_plugin)
target_compile_definitions(data_source_library_test PRIVATE
  PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
  PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_required_slots_plugin>"
)
target_compile_options(data_source_library_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(data_source_library_test PRIVATE
  pj_data_source_host pj_base GTest::gtest_main
)
add_test(NAME data_source_library_test COMMAND data_source_library_test)

# Integration test: DataSource + Dialog
add_executable(source_dialog_integration_test tests/source_dialog_integration_test.cpp)
add_dependencies(source_dialog_integration_test mock_source_with_dialog_plugin mock_data_source_plugin)
target_compile_definitions(source_dialog_integration_test PRIVATE
  PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH="$<TARGET_FILE:mock_source_with_dialog_plugin>"
  PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
)
target_compile_options(source_dialog_integration_test PRIVATE ${PJ_WARNING_FLAGS})
target_include_directories(source_dialog_integration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(source_dialog_integration_test PRIVATE
  pj_data_source_host pj_dialog_host pj_base GTest::gtest_main
)
add_test(NAME source_dialog_integration_test COMMAND source_dialog_integration_test)

# Integration test: FileSourceBase lifecycle
add_executable(file_source_integration_test tests/file_source_integration_test.cpp)
target_compile_definitions(file_source_integration_test PRIVATE
  PJ_MOCK_FILE_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_file_source_plugin>"
)
target_compile_options(file_source_integration_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(file_source_integration_test PRIVATE
  pj_data_source_host pj_base GTest::gtest_main
)
add_test(NAME file_source_integration_test COMMAND file_source_integration_test)

# Integration test: MessageParser library loader
add_executable(message_parser_library_test tests/message_parser_library_test.cpp)
add_dependencies(message_parser_library_test functional_image_parser_plugin)
target_compile_definitions(message_parser_library_test PRIVATE
  PJ_MOCK_JSON_PARSER_PLUGIN_PATH="$<TARGET_FILE:mock_json_parser_plugin>"
  PJ_FUNCTIONAL_IMAGE_PARSER_PLUGIN_PATH="$<TARGET_FILE:functional_image_parser_plugin>"
  PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_required_slots_plugin>"
)
target_compile_options(message_parser_library_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(message_parser_library_test PRIVATE
  pj_message_parser_host pj_base GTest::gtest_main
)
add_test(NAME message_parser_library_test COMMAND message_parser_library_test)

# Unit test: optional parser runtime diagnostics service binding.
add_executable(message_parser_runtime_test tests/message_parser_runtime_test.cpp)
target_compile_options(message_parser_runtime_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(message_parser_runtime_test PRIVATE
  pj_plugin_sdk GTest::gtest_main
)
add_test(NAME message_parser_runtime_test COMMAND message_parser_runtime_test)

# Unit test: additions-only pure-functional parser C extension.
add_executable(message_parser_functional_extension_test tests/message_parser_functional_extension_test.cpp)
target_compile_options(message_parser_functional_extension_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(message_parser_functional_extension_test PRIVATE
  pj_message_parser_host pj_plugin_sdk GTest::gtest_main
)
add_test(NAME message_parser_functional_extension_test COMMAND message_parser_functional_extension_test)

# Unit test: exact route claims synthesized from registered schema handlers.
add_executable(message_parser_route_claims_extension_test tests/message_parser_route_claims_extension_test.cpp)
target_compile_options(message_parser_route_claims_extension_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(message_parser_route_claims_extension_test PRIVATE
  pj_message_parser_host pj_plugin_sdk GTest::gtest_main
)
add_test(NAME message_parser_route_claims_extension_test COMMAND message_parser_route_claims_extension_test)

# Compile-time pins for the cross-DSO MessageParserPluginBase member layout.
add_executable(message_parser_abi_layout_sentinels_test tests/message_parser_abi_layout_sentinels_test.cpp)
target_compile_options(message_parser_abi_layout_sentinels_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(message_parser_abi_layout_sentinels_test PRIVATE
  pj_plugin_sdk GTest::gtest_main
)
add_test(NAME message_parser_abi_layout_sentinels_test COMMAND message_parser_abi_layout_sentinels_test)

# Unit test: ObjectIngestPolicyResolver cascade rules.
add_executable(object_ingest_policy_test tests/object_ingest_policy_test.cpp)
target_compile_options(object_ingest_policy_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(object_ingest_policy_test PRIVATE
  pj_base GTest::gtest_main
)
target_include_directories(object_ingest_policy_test PRIVATE include)
add_test(NAME object_ingest_policy_test COMMAND object_ingest_policy_test)

# Unit tests: parser claim admission, manifest decoding, and synthesis.
add_executable(parser_claim_catalog_test tests/parser_claim_catalog_test.cpp)
target_compile_options(parser_claim_catalog_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_claim_catalog_test PRIVATE
  pj_parser_claim_catalog GTest::gtest_main
)
add_test(NAME parser_claim_catalog_test COMMAND parser_claim_catalog_test)

# Unit tests: deterministic per-route selection and probe-cache invalidation.
add_executable(parser_route_resolver_test tests/parser_route_resolver_test.cpp)
target_compile_options(parser_route_resolver_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_route_resolver_test PRIVATE
  pj_parser_claim_catalog GTest::gtest_main
)
add_test(NAME parser_route_resolver_test COMMAND parser_route_resolver_test)

# Integration tests: native functional parser-module loading and execution.
add_executable(native_parser_module_test tests/native_parser_module_test.cpp)
add_dependencies(native_parser_module_test
  native_parser_module_fixture
  native_parser_module_missing_export
  native_parser_module_wrong_abi
  native_parser_module_unreadable_manifest
)
target_compile_definitions(native_parser_module_test PRIVATE
  PJ_NATIVE_MODULE_FIXTURE_PATH="$<TARGET_FILE:native_parser_module_fixture>"
  PJ_NATIVE_MODULE_MISSING_EXPORT_PATH="$<TARGET_FILE:native_parser_module_missing_export>"
  PJ_NATIVE_MODULE_WRONG_ABI_PATH="$<TARGET_FILE:native_parser_module_wrong_abi>"
  PJ_NATIVE_MODULE_UNREADABLE_MANIFEST_PATH="$<TARGET_FILE:native_parser_module_unreadable_manifest>"
)
target_compile_options(native_parser_module_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(native_parser_module_test PRIVATE
  pj_parser_module_host pj_parser_claim_catalog GTest::gtest_main
)
add_test(NAME native_parser_module_test COMMAND native_parser_module_test)

add_executable(parser_module_runtime_test tests/parser_module_runtime_test.cpp)
add_dependencies(parser_module_runtime_test native_parser_module_fixture)
target_compile_definitions(parser_module_runtime_test PRIVATE
  PJ_NATIVE_MODULE_FIXTURE_PATH="$<TARGET_FILE:native_parser_module_fixture>"
)
target_compile_options(parser_module_runtime_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_runtime_test PRIVATE
  pj_parser_module_host GTest::gtest_main
)
add_test(NAME parser_module_runtime_test COMMAND parser_module_runtime_test)

add_executable(parser_module_session_budget_test tests/parser_module_session_budget_test.cpp)
target_compile_options(parser_module_session_budget_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_session_budget_test PRIVATE pj_parser_module_host GTest::gtest_main)
add_test(NAME parser_module_session_budget_test COMMAND parser_module_session_budget_test)

# Keystone integration: a C++17 module authored and built only with the
# header-only kit, then exercised through the production native loader/runtime.
add_executable(parser_module_authoring_e2e_test tests/parser_module_authoring_e2e_test.cpp)
add_dependencies(parser_module_authoring_e2e_test toy_cdr_pointcloud_module)
target_compile_definitions(parser_module_authoring_e2e_test PRIVATE
  PJ_TOY_CDR_POINTCLOUD_MODULE_PATH="$<TARGET_FILE:toy_cdr_pointcloud_module>"
)
target_compile_options(parser_module_authoring_e2e_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_authoring_e2e_test PRIVATE
  pj_parser_module_host pj_parser_claim_catalog GTest::gtest_main
)
add_test(NAME parser_module_authoring_e2e_test COMMAND parser_module_authoring_e2e_test)

# Unit test: ServiceRegistryBuilder registration rules. PJ_ASSERT_THROWS makes
# the convenience overload's invariant observable in every build type — a plain
# assert() is compiled away under NDEBUG, which is where it would go unchecked.
add_executable(service_registry_builder_test tests/service_registry_builder_test.cpp)
target_compile_definitions(service_registry_builder_test PRIVATE PJ_ASSERT_THROWS)
target_compile_options(service_registry_builder_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(service_registry_builder_test PRIVATE
  pj_base GTest::gtest_main
)
target_include_directories(service_registry_builder_test PRIVATE include)
add_test(NAME service_registry_builder_test COMMAND service_registry_builder_test)

# TODO(v3-port): delegated_ingest_integration_test.cpp uses old bindWriteHost /
# bindRuntimeHost methods and get_last_error slots removed in v3. Pending port
# to the service registry + PJ_error_t* pattern. Its coverage (parser binding +
# raw-message dispatch) remains verified by data_source_library_test.cpp and
# message_parser_library_test.cpp individually.
#
# add_executable(delegated_ingest_integration_test tests/delegated_ingest_integration_test.cpp)
# target_compile_definitions(delegated_ingest_integration_test PRIVATE
#   PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
#   PJ_MOCK_JSON_PARSER_PLUGIN_PATH="$<TARGET_FILE:mock_json_parser_plugin>"
# )
# target_compile_options(delegated_ingest_integration_test PRIVATE ${PJ_WARNING_FLAGS})
# target_link_libraries(delegated_ingest_integration_test PRIVATE
#   pj_data_source_host pj_message_parser_host pj_base GTest::gtest_main
# )
# add_test(NAME delegated_ingest_integration_test COMMAND delegated_ingest_integration_test)

# Integration test: Toolbox library loader
add_executable(toolbox_plugin_test tests/toolbox_plugin_test.cpp)
target_compile_definitions(toolbox_plugin_test PRIVATE
  PJ_MOCK_TOOLBOX_PLUGIN_PATH="$<TARGET_FILE:mock_toolbox_plugin>"
  PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_required_slots_plugin>"
)
target_compile_options(toolbox_plugin_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(toolbox_plugin_test PRIVATE
  pj_toolbox_host pj_base GTest::gtest_main
)
add_test(NAME toolbox_plugin_test COMMAND toolbox_plugin_test)

# Static export macros need an explicit symbol token when the C++ class is
# namespaced; compile and call every plugin family's named variant together.
add_executable(static_namespaced_plugin_macros_test tests/static_namespaced_plugin_macros_test.cpp)
target_compile_definitions(static_namespaced_plugin_macros_test PRIVATE PJ_STATIC_PLUGINS)
target_compile_options(static_namespaced_plugin_macros_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(static_namespaced_plugin_macros_test PRIVATE
  pj_plugin_sdk GTest::gtest_main
)
add_test(NAME static_namespaced_plugin_macros_test COMMAND static_namespaced_plugin_macros_test)

# ---------------------------------------------------------------------------
# Plugin catalog DSO scanner test.
# ---------------------------------------------------------------------------
add_executable(plugin_catalog_test tests/plugin_catalog_test.cpp)
target_compile_definitions(plugin_catalog_test PRIVATE
  PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
  PJ_MOCK_JSON_PARSER_PLUGIN_PATH="$<TARGET_FILE:mock_json_parser_plugin>"
  PJ_MOCK_TOOLBOX_PLUGIN_PATH="$<TARGET_FILE:mock_toolbox_plugin>"
  PJ_MOCK_DIALOG_PLUGIN_PATH="$<TARGET_FILE:mock_dialog_plugin>"
  PJ_STATIC_MANIFEST_DIALOG_PLUGIN_PATH="$<TARGET_FILE:static_manifest_dialog_plugin>"
  PJ_LEGACY_MACRO_DIALOG_PLUGIN_PATH="$<TARGET_FILE:legacy_macro_dialog_plugin>"
  PJ_OLD_DIALOG_VTABLE_PLUGIN_PATH="$<TARGET_FILE:old_dialog_vtable_plugin>"
  PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_dialog_required_slots_plugin>"
  PJ_MISSING_ID_PLUGIN_PATH="$<TARGET_FILE:missing_id_data_source_plugin>"
  PJ_INVALID_OPTIONAL_PLUGIN_PATH="$<TARGET_FILE:invalid_optional_manifest_data_source_plugin>"
  PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_required_slots_plugin>"
  PJ_ENTRY_POINT_VIA_DEPENDENCY_PLUGIN_PATH="$<TARGET_FILE:entry_point_via_dependency_plugin>"
  PJ_ENTRY_POINT_WITH_OWN_EXPORTS_PLUGIN_PATH="$<TARGET_FILE:entry_point_with_own_exports_plugin>"
  PJ_DEPENDENCY_SEARCH_CANDIDATE_PATH="$<TARGET_FILE:dependency_search_candidate_plugin>"
  PJ_DEPENDENCY_SEARCH_REAL_PATH="$<TARGET_FILE:dependency_search_real>"
  PJ_DEPENDENCY_SEARCH_DECOY_PATH="$<TARGET_FILE:dependency_search_decoy>"
)
if(WIN32)
  target_compile_definitions(plugin_catalog_test PRIVATE
    PJ_ENTRY_POINT_FORWARDER_PLUGIN_PATH="$<TARGET_FILE:entry_point_forwarder_plugin>"
  )
endif()
target_compile_options(plugin_catalog_test PRIVATE ${PJ_WARNING_FLAGS})
target_include_directories(plugin_catalog_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(plugin_catalog_test PRIVATE
  pj_plugin_catalog pj_data_source_host GTest::gtest_main
)
add_dependencies(plugin_catalog_test mock_data_source_plugin
                 mock_json_parser_plugin
                 mock_toolbox_plugin mock_dialog_plugin missing_id_data_source_plugin
                 invalid_optional_manifest_data_source_plugin missing_required_slots_plugin
                 static_manifest_dialog_plugin legacy_macro_dialog_plugin
                 old_dialog_vtable_plugin missing_dialog_required_slots_plugin
                 entry_point_via_dependency_plugin entry_point_with_own_exports_plugin
                 dependency_search_candidate_plugin dependency_search_decoy)
if(WIN32)
  add_dependencies(plugin_catalog_test entry_point_forwarder_plugin)
endif()
add_test(NAME plugin_catalog_test COMMAND plugin_catalog_test)

endif() # PJ_BUILD_TESTS

# ---------------------------------------------------------------------------
# Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt)
# ---------------------------------------------------------------------------

if(PJ_INSTALL_SDK)
  install(TARGETS
      pj_plugin_loader_detail
      pj_plugin_sdk
      pj_plugin_catalog
      pj_parser_claim_catalog
      pj_parser_module_host
      pj_data_source_host
      pj_message_parser_host
      pj_toolbox_host
      pj_plugin_host
    EXPORT plotjuggler_sdkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
