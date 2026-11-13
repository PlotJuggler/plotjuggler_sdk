# ---------------------------------------------------------------------------
# EXPERIMENTAL: functional parser modules (authoring kit, export ABI, native
# host loader/runtime). Not installed, not packaged, and outside the SDK
# compatibility promise. See README.md.
# ---------------------------------------------------------------------------

include(cmake/PjParserModule.cmake)
find_package(nlohmann_json REQUIRED)

# Standalone, header-only authoring kit. Carries include paths and a C++ floor
# only: modules link no SDK library and stay suitable for native and WASI
# reactor builds. pj_add_parser_module() links it by this alias.
add_library(pj_parser_module INTERFACE)
target_include_directories(pj_parser_module INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/pj_base/include
)
target_compile_features(pj_parser_module INTERFACE cxx_std_17)
add_library(plotjuggler_sdk::parser_module ALIAS pj_parser_module)

# Export-ABI block codecs and the wasm manifest-section codec, shared by the
# host runtime and by test fixtures that must not link the loader.
add_library(pj_parser_module_abi STATIC
  src/parser_module_abi.cpp
  src/parser_module_manifest.cpp
)
target_include_directories(pj_parser_module_abi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(pj_parser_module_abi PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
set_target_properties(pj_parser_module_abi PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(pj_parser_module_abi PUBLIC pj_base)

# Native module loader, runtime and manifest-to-claims decoding.
add_library(pj_parser_module_host STATIC
  src/native_parser_module.cpp
  src/parser_module_runtime.cpp
  src/parser_module_claims.cpp
)
target_include_directories(pj_parser_module_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(pj_parser_module_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_parser_module_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(pj_parser_module_host
  PUBLIC
    pj_parser_module_abi
    pj_parser_claim_catalog
  PRIVATE
    pj_plugin_loader_detail  # detail/library_loader.hpp: the plugin loaders' dlopen policy
    nlohmann_json::nlohmann_json
    ${CMAKE_DL_LIBS}
)

if(NOT PJ_BUILD_TESTS)
  return()
endif()

foreach(test_name parser_module_abi_test parser_module_manifest_test)
  add_executable(${test_name} tests/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE pj_parser_module_abi GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

add_executable(parser_module_claims_test tests/parser_module_claims_test.cpp)
target_compile_options(parser_module_claims_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_claims_test PRIVATE pj_parser_module_host GTest::gtest_main)
add_test(NAME parser_module_claims_test COMMAND parser_module_claims_test)

# C++17 standalone authoring-kit gate. This target intentionally does not
# link pj_base; GTest is test infrastructure only.
add_executable(parser_module_kit_test tests/parser_module_kit_test.cpp)
set_target_properties(parser_module_kit_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
target_compile_options(parser_module_kit_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_kit_test PRIVATE pj_parser_module GTest::gtest_main)
add_test(NAME parser_module_kit_test COMMAND parser_module_kit_test)

add_executable(parser_module_object_writer_test tests/parser_module_object_writer_test.cpp)
target_compile_definitions(parser_module_object_writer_test PRIVATE PJ_PARSER_MODULE_CLAIM_COUNT=1)
target_compile_options(parser_module_object_writer_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_object_writer_test PRIVATE pj_parser_module pj_parser_module_abi GTest::gtest_main)
add_test(NAME parser_module_object_writer_test COMMAND parser_module_object_writer_test)

# Static wasm ABI conformance. The authoring source is compiled directly by
# wasi-sdk, then a host-side auditor embeds and validates the final module.
set(PJ_WASI_SDK_ROOT "$ENV{PJ_WASI_SDK_ROOT}" CACHE PATH
  "wasi-sdk 27 root used for parser-module conformance tests")
set(_pj_wasi_clang "${PJ_WASI_SDK_ROOT}/bin/clang++")
set(_pj_wasi_sysroot "${PJ_WASI_SDK_ROOT}/share/wasi-sysroot")
if(NOT PJ_WASI_SDK_ROOT)
  message(STATUS
    "Parser-module WASI conformance skipped: PJ_WASI_SDK_ROOT cache/env variable is not set")
elseif(NOT EXISTS "${_pj_wasi_clang}" OR NOT IS_DIRECTORY "${_pj_wasi_sysroot}")
  message(STATUS
    "Parser-module WASI conformance skipped: wasi-sdk is missing under PJ_WASI_SDK_ROOT=${PJ_WASI_SDK_ROOT}")
else()
  file(STRINGS "${PJ_WASI_SDK_ROOT}/VERSION" _pj_wasi_version LIMIT_COUNT 1)
  if(NOT _pj_wasi_version MATCHES "^27\\.")
    message(FATAL_ERROR
      "Parser-module WASI conformance requires wasi-sdk 27; found '${_pj_wasi_version}'")
  endif()
  message(STATUS
    "Parser-module WASI conformance enabled with wasi-sdk ${_pj_wasi_version}: ${PJ_WASI_SDK_ROOT}")

  add_executable(parser_module_wasm_audit tests/parser_module_wasm_audit.cpp)
  target_compile_options(parser_module_wasm_audit PRIVATE ${PJ_WARNING_FLAGS})
  target_link_libraries(parser_module_wasm_audit PRIVATE pj_parser_module_abi)

  file(GLOB _pj_parser_module_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pj_base/parser_module/*.hpp")
  set(_pj_wasm_dir "${CMAKE_CURRENT_BINARY_DIR}/parser_module_wasm_conformance")
  set(_pj_wasm_raw "${_pj_wasm_dir}/toy_cdr_pointcloud.raw.wasm")
  set(_pj_wasm_embedded "${_pj_wasm_dir}/toy_cdr_pointcloud.wasm")
  set(_pj_wasm_source "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud_module.cpp")
  set(_pj_wasm_manifest "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud.module.json")

  add_custom_command(
    OUTPUT "${_pj_wasm_raw}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_pj_wasm_dir}"
    COMMAND "${_pj_wasi_clang}"
      --target=wasm32-wasip1
      --sysroot=${_pj_wasi_sysroot}
      -mexec-model=reactor
      -std=c++17
      -fno-exceptions
      -fno-rtti
      -fvisibility=hidden
      -O1
      -Wall -Wextra -Werror
      -DPJ_PARSER_MODULE_CLAIM_COUNT=2
      -I${CMAKE_CURRENT_SOURCE_DIR}/include
      -I${PROJECT_SOURCE_DIR}/pj_base/include
      "${_pj_wasm_source}"
      -Wl,--export=pj_module_abi
      -Wl,--export=pj_module_create
      -Wl,--export=pj_module_destroy
      -Wl,--export=pj_module_bind
      -Wl,--export=pj_module_parse
      -Wl,--export=pj_module_last_error
      -Wl,--export=pj_module_alloc
      -Wl,--export=pj_module_free
      -o "${_pj_wasm_raw}"
    DEPENDS "${_pj_wasm_source}" ${_pj_parser_module_headers}
      "${PROJECT_SOURCE_DIR}/pj_base/include/pj_base/time_math.hpp"
    COMMENT "Compiling C++17 parser-module WASI reactor fixture"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${_pj_wasm_embedded}"
    COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0
      $<TARGET_FILE:parser_module_wasm_audit>
      --embed "${_pj_wasm_raw}" "${_pj_wasm_manifest}" "${_pj_wasm_embedded}"
    DEPENDS "${_pj_wasm_raw}" "${_pj_wasm_manifest}" parser_module_wasm_audit
    COMMENT "Embedding parser-module wasm manifest with the shared codec"
    VERBATIM
  )
  add_custom_target(parser_module_wasm_conformance_fixture ALL
    DEPENDS "${_pj_wasm_embedded}")
  add_test(NAME parser_module_wasm_conformance_test
    COMMAND parser_module_wasm_audit --audit "${_pj_wasm_embedded}" "${_pj_wasm_manifest}")
  set_tests_properties(parser_module_wasm_conformance_test PROPERTIES
    LABELS "parser_module;wasi"
    REQUIRED_FILES "${_pj_wasm_embedded}")
endif()

pj_add_parser_module(toy_cdr_pointcloud_module
  SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud_module.cpp"
  MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud.module.json"
  TARGETS native
)

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
  target_link_libraries(${target} PRIVATE pj_parser_module_abi)
  if(ARGN)
    target_compile_definitions(${target} PRIVATE ${ARGN})
  endif()
  if(UNIX AND NOT APPLE)
    target_link_options(${target} PRIVATE "LINKER:-z,defs" "LINKER:--exclude-libs,ALL")
  endif()
endfunction()

pj_add_native_parser_module_fixture(native_parser_module_fixture)
pj_add_native_parser_module_fixture(native_parser_module_missing_export PJ_FIXTURE_OMIT_FREE)
pj_add_native_parser_module_fixture(native_parser_module_wrong_abi PJ_FIXTURE_WRONG_ABI)
pj_add_native_parser_module_fixture(native_parser_module_unreadable_manifest PJ_FIXTURE_UNREADABLE_MANIFEST)

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
  pj_parser_module_host GTest::gtest_main
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

# Keystone integration: a C++17 module authored and built only with the
# header-only kit, then exercised through the production native loader/runtime.
add_executable(parser_module_authoring_e2e_test tests/parser_module_authoring_e2e_test.cpp)
add_dependencies(parser_module_authoring_e2e_test toy_cdr_pointcloud_module)
target_compile_definitions(parser_module_authoring_e2e_test PRIVATE
  PJ_TOY_CDR_POINTCLOUD_MODULE_PATH="$<TARGET_FILE:toy_cdr_pointcloud_module>"
)
target_compile_options(parser_module_authoring_e2e_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_authoring_e2e_test PRIVATE
  pj_parser_module_host GTest::gtest_main
)
add_test(NAME parser_module_authoring_e2e_test COMMAND parser_module_authoring_e2e_test)
