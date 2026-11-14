# ---------------------------------------------------------------------------
# EXPERIMENTAL: functional parser modules (authoring kit, export ABI, native
# and wasm host loader/runtime). Not installed, not packaged, and outside the SDK
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
  src/parser_module_wasm.cpp
)
target_include_directories(pj_parser_module_abi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(pj_parser_module_abi PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
set_target_properties(pj_parser_module_abi PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(pj_parser_module_abi PUBLIC pj_base)

# Build-time frontend for embedding exact manifest bytes and running the same
# static wasm audit used by the conformance gate. pj_add_parser_module(TARGETS
# wasm) invokes it by this alias.
add_executable(pj_wasm_embed_manifest tools/pj_wasm_embed_manifest.cpp)
target_compile_features(pj_wasm_embed_manifest PRIVATE cxx_std_20)
target_compile_options(pj_wasm_embed_manifest PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(pj_wasm_embed_manifest PRIVATE pj_parser_module_abi)
set_target_properties(pj_wasm_embed_manifest PROPERTIES OUTPUT_NAME pj-wasm-embed-manifest)
add_executable(plotjuggler_sdk::wasm_embed_manifest ALIAS pj_wasm_embed_manifest)

# Native module loader, runtime, session budget and manifest-to-claims decoding.
add_library(pj_parser_module_host STATIC
  src/native_parser_module.cpp
  src/parser_module_runtime.cpp
  src/parser_module_session_budget.cpp
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

# Wasm module loader/runtime, executed by a statically linked Wasmer 7.0.1
# C API. Built only when PJ_WASMER_ROOT names that installation.
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

  add_library(pj_wasm_parser_module_host STATIC
    src/wasm_parser_module.cpp
    src/wasm_parser_module_runtime.cpp
  )
  target_include_directories(pj_wasm_parser_module_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_features(pj_wasm_parser_module_host PUBLIC cxx_std_20)
  target_compile_options(pj_wasm_parser_module_host PRIVATE ${PJ_WARNING_FLAGS})
  set_target_properties(pj_wasm_parser_module_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
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
endif()

if(NOT PJ_BUILD_TESTS)
  return()
endif()

foreach(test_name parser_module_abi_test parser_module_manifest_test parser_module_wasm_test)
  add_executable(${test_name} tests/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE pj_parser_module_abi GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

add_executable(parser_module_claims_test tests/parser_module_claims_test.cpp)
target_compile_options(parser_module_claims_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_claims_test PRIVATE pj_parser_module_host GTest::gtest_main)
add_test(NAME parser_module_claims_test COMMAND parser_module_claims_test)

add_executable(parser_module_session_budget_test tests/parser_module_session_budget_test.cpp)
target_compile_options(parser_module_session_budget_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(parser_module_session_budget_test PRIVATE pj_parser_module_host GTest::gtest_main)
add_test(NAME parser_module_session_budget_test COMMAND parser_module_session_budget_test)

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

# Static wasm ABI conformance. The production authoring helper compiles the
# fixture through wasi-sdk, embeds its manifest, and audits the result.
set(_pj_build_toy_wasm OFF)
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
  set(_pj_build_toy_wasm ON)
endif()

set(_pj_toy_targets native)
if(_pj_build_toy_wasm)
  list(APPEND _pj_toy_targets wasm)
endif()
pj_add_parser_module(toy_cdr_pointcloud_module
  SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud_module.cpp"
  MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud.module.json"
  TARGETS ${_pj_toy_targets}
)
if(_pj_build_toy_wasm)
  get_property(_pj_wasm_embedded TARGET toy_cdr_pointcloud_module_wasm PROPERTY PJ_PARSER_MODULE_WASM_PATH)
  set(_pj_wasm_manifest "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud.module.json")
  add_custom_target(parser_module_wasm_conformance_fixture ALL DEPENDS toy_cdr_pointcloud_module_wasm)
  add_test(NAME parser_module_wasm_conformance_test
    COMMAND pj_wasm_embed_manifest verify "${_pj_wasm_embedded}" "${_pj_wasm_manifest}")
  set_tests_properties(parser_module_wasm_conformance_test PROPERTIES
    LABELS "parser_module;wasi"
    REQUIRED_FILES "${_pj_wasm_embedded}")
endif()

# Wasm execution through Wasmer: an isolated Wasmer prototype, the toy module
# end to end, and adversarial guests against the hardening limits.
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

  if(_pj_build_toy_wasm)
    add_executable(wasm_parser_module_test tests/wasm_parser_module_test.cpp)
    add_dependencies(wasm_parser_module_test parser_module_wasm_conformance_fixture)
    target_compile_definitions(wasm_parser_module_test PRIVATE
      PJ_TOY_CDR_POINTCLOUD_WASM_PATH="${_pj_wasm_embedded}")
    target_compile_options(wasm_parser_module_test PRIVATE ${PJ_WARNING_FLAGS})
    target_link_libraries(wasm_parser_module_test PRIVATE pj_wasm_parser_module_host GTest::gtest_main)
    add_test(NAME wasm_parser_module_test COMMAND wasm_parser_module_test)
    set_tests_properties(wasm_parser_module_test PROPERTIES LABELS "parser_module;wasmer;wasi")

    pj_add_parser_module(adversarial_wasm_parser_module
      SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/adversarial_wasm_parser_module.cpp"
      MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/adversarial_wasm_parser_module.module.json"
      TARGETS wasm
    )
    get_property(_pj_adversarial_wasm_path TARGET adversarial_wasm_parser_module_wasm
      PROPERTY PJ_PARSER_MODULE_WASM_PATH)
    add_executable(wasm_parser_module_hardening_test tests/wasm_parser_module_hardening_test.cpp)
    add_dependencies(wasm_parser_module_hardening_test adversarial_wasm_parser_module_wasm)
    target_compile_definitions(wasm_parser_module_hardening_test PRIVATE
      PJ_ADVERSARIAL_WASM_PATH="${_pj_adversarial_wasm_path}")
    target_compile_options(wasm_parser_module_hardening_test PRIVATE ${PJ_WARNING_FLAGS})
    target_link_libraries(wasm_parser_module_hardening_test PRIVATE pj_wasm_parser_module_host GTest::gtest_main)
    add_test(NAME wasm_parser_module_hardening_test COMMAND wasm_parser_module_hardening_test)
    set_tests_properties(wasm_parser_module_hardening_test PROPERTIES LABELS "parser_module;wasmer;wasi")
  else()
    message(STATUS "Wasm parser-module E2E skipped: the wasi-sdk fixture target is unavailable")
  endif()
endif()

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
