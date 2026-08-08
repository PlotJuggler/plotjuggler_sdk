# PjParserModule.cmake
#
# Functional parser-module target helper for native modules and wasi-sdk 27
# reactor modules. Both targets compile the same author source and consume the
# same manifest bytes.

function(_pj_parser_module_kit_include OUTPUT)
  if(NOT TARGET plotjuggler_sdk::parser_module)
    message(FATAL_ERROR "pj_add_parser_module: plotjuggler_sdk::parser_module is unavailable")
  endif()
  get_target_property(_includes plotjuggler_sdk::parser_module INTERFACE_INCLUDE_DIRECTORIES)
  foreach(_include IN LISTS _includes)
    if(_include MATCHES "^\\$<BUILD_INTERFACE:(.*)>$")
      set(${OUTPUT} "${CMAKE_MATCH_1}" PARENT_SCOPE)
      return()
    elseif(NOT _include MATCHES "^\\$<" AND IS_DIRECTORY "${_include}")
      set(${OUTPUT} "${_include}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "pj_add_parser_module: cannot resolve the parser-module kit include directory")
endfunction()

function(pj_add_parser_module TARGET)
  set(_options)
  set(_oneValueArgs SOURCE MANIFEST)
  set(_multiValueArgs TARGETS)
  cmake_parse_arguments(ARG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "pj_add_parser_module(${TARGET}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT ARG_SOURCE)
    message(FATAL_ERROR "pj_add_parser_module(${TARGET}): SOURCE is required")
  endif()
  if(NOT ARG_MANIFEST)
    message(FATAL_ERROR "pj_add_parser_module(${TARGET}): MANIFEST is required")
  endif()
  if(NOT ARG_TARGETS)
    message(FATAL_ERROR "pj_add_parser_module(${TARGET}): TARGETS native and/or wasm is required")
  endif()

  list(REMOVE_DUPLICATES ARG_TARGETS)
  set(_build_native OFF)
  set(_build_wasm OFF)
  foreach(_requested_target IN LISTS ARG_TARGETS)
    if(_requested_target STREQUAL "native")
      set(_build_native ON)
    elseif(_requested_target STREQUAL "wasm")
      set(_build_wasm ON)
    else()
      message(FATAL_ERROR
        "pj_add_parser_module(${TARGET}): unsupported TARGETS value '${_requested_target}'; "
        "expected native and/or wasm")
    endif()
  endforeach()

  get_filename_component(_module_source "${ARG_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(_module_manifest "${ARG_MANIFEST}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_module_source}")
    message(FATAL_ERROR "pj_add_parser_module(${TARGET}): SOURCE not found: ${_module_source}")
  endif()
  if(NOT EXISTS "${_module_manifest}")
    message(FATAL_ERROR "pj_add_parser_module(${TARGET}): MANIFEST not found: ${_module_manifest}")
  endif()

  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_module_manifest}")
  file(READ "${_module_manifest}" _manifest_json)
  string(JSON _claims_type ERROR_VARIABLE _claims_error TYPE "${_manifest_json}" claims)
  if(_claims_error OR NOT _claims_type STREQUAL "ARRAY")
    message(FATAL_ERROR
      "pj_add_parser_module(${TARGET}): MANIFEST must contain a claims array")
  endif()
  string(JSON _claim_count LENGTH "${_manifest_json}" claims)

  if(_build_native)
    if(_manifest_json MATCHES "\\)PJM\"")
      message(FATAL_ERROR
        "pj_add_parser_module(${TARGET}): MANIFEST contains the reserved raw-string delimiter")
    endif()

    set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_parser_module_generated")
    file(MAKE_DIRECTORY "${_generated_dir}")
    set(_manifest_header "${_generated_dir}/${TARGET}_manifest.hpp")
    file(WRITE "${_manifest_header}"
      "#pragma once\n"
      "#define PJ_PARSER_MODULE_HAS_MANIFEST 1\n"
      "namespace pj { namespace detail {\n"
      "inline constexpr char kBuiltManifest[] = R\"PJM(${_manifest_json})PJM\";\n"
      "} }\n"
      "#define PJ_PARSER_MODULE_CLAIM_COUNT ${_claim_count}\n")

    add_library(${TARGET} MODULE "${_module_source}" "${_manifest_header}")
    target_link_libraries(${TARGET} PRIVATE plotjuggler_sdk::parser_module)
    target_include_directories(${TARGET} PRIVATE "${_generated_dir}")
    target_compile_definitions(${TARGET} PRIVATE
      PJ_PARSER_MODULE_MANIFEST_HEADER=\"${TARGET}_manifest.hpp\")
    set_target_properties(${TARGET} PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED YES
      CXX_EXTENSIONS NO
      CXX_VISIBILITY_PRESET hidden
      C_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES
    )
    if(DEFINED PJ_WARNING_FLAGS)
      target_compile_options(${TARGET} PRIVATE ${PJ_WARNING_FLAGS})
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      set(_version_script "${_generated_dir}/${TARGET}.map")
      file(WRITE "${_version_script}"
        "{\n  global:\n    pj_module_abi;\n    pj_module_create;\n    pj_module_destroy;\n"
        "    pj_module_bind;\n    pj_module_parse;\n    pj_module_last_error;\n"
        "    pj_module_alloc;\n    pj_module_free;\n    pj_module_manifest_addr;\n"
        "    pj_module_manifest_len;\n  local: *;\n};\n")
      target_link_options(${TARGET} PRIVATE
        "LINKER:-z,defs"
        "LINKER:--exclude-libs,ALL"
        "LINKER:--version-script=${_version_script}")
    elseif(APPLE)
      set(_exported_symbols "${_generated_dir}/${TARGET}.exports")
      file(WRITE "${_exported_symbols}"
        "_pj_module_abi\n_pj_module_create\n_pj_module_destroy\n_pj_module_bind\n"
        "_pj_module_parse\n_pj_module_last_error\n_pj_module_alloc\n_pj_module_free\n"
        "_pj_module_manifest_addr\n_pj_module_manifest_len\n")
      target_link_options(${TARGET} PRIVATE "LINKER:-exported_symbols_list,${_exported_symbols}")
    endif()
  endif()

  if(_build_wasm)
    if(NOT DEFINED PJ_WASI_SDK_ROOT OR NOT PJ_WASI_SDK_ROOT)
      set(PJ_WASI_SDK_ROOT "$ENV{PJ_WASI_SDK_ROOT}" CACHE PATH
        "wasi-sdk 27 root used for parser-module wasm targets")
    endif()
    if(NOT PJ_WASI_SDK_ROOT)
      message(FATAL_ERROR
        "pj_add_parser_module(${TARGET}): TARGETS wasm requires PJ_WASI_SDK_ROOT "
        "to name a wasi-sdk 27 installation")
    endif()
    set(_wasi_clang "${PJ_WASI_SDK_ROOT}/bin/clang++")
    set(_wasi_sysroot "${PJ_WASI_SDK_ROOT}/share/wasi-sysroot")
    if(NOT EXISTS "${_wasi_clang}" OR NOT IS_DIRECTORY "${_wasi_sysroot}" OR
       NOT EXISTS "${PJ_WASI_SDK_ROOT}/VERSION")
      message(FATAL_ERROR
        "pj_add_parser_module(${TARGET}): wasi-sdk is incomplete under "
        "PJ_WASI_SDK_ROOT=${PJ_WASI_SDK_ROOT}")
    endif()
    file(STRINGS "${PJ_WASI_SDK_ROOT}/VERSION" _wasi_version LIMIT_COUNT 1)
    if(NOT _wasi_version MATCHES "^27\\.")
      message(FATAL_ERROR
        "pj_add_parser_module(${TARGET}): TARGETS wasm requires wasi-sdk 27; "
        "found '${_wasi_version}'")
    endif()
    if(NOT TARGET plotjuggler_sdk::wasm_embed_manifest)
      message(FATAL_ERROR
        "pj_add_parser_module(${TARGET}): the pj-wasm-embed-manifest SDK tool is unavailable")
    endif()

    set(PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES "268435456" CACHE STRING
      "Declared maximum linear memory for authored parser-module wasm reactors")
    set(PJ_PARSER_MODULE_WASM_STACK_SIZE_BYTES "1048576" CACHE STRING
      "Shadow-stack size for authored parser-module wasm reactors")
    if(NOT PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES MATCHES "^[0-9]+$")
      message(FATAL_ERROR "PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES must be an integer byte count")
    endif()
    math(EXPR _maximum_memory_remainder "${PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES} % 65536")
    if(PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES LESS 65536 OR NOT _maximum_memory_remainder EQUAL 0)
      message(FATAL_ERROR
        "PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES must be a positive multiple of 65536")
    endif()
    if(NOT PJ_PARSER_MODULE_WASM_STACK_SIZE_BYTES MATCHES "^[0-9]+$" OR
       PJ_PARSER_MODULE_WASM_STACK_SIZE_BYTES LESS 262144)
      message(FATAL_ERROR
        "PJ_PARSER_MODULE_WASM_STACK_SIZE_BYTES must be an integer of at least 262144 bytes")
    endif()

    _pj_parser_module_kit_include(_kit_include)
    set(_no_io_stubs "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/parser_module_wasi_no_io_stubs.cpp")
    if(NOT EXISTS "${_no_io_stubs}")
      message(FATAL_ERROR "pj_add_parser_module(${TARGET}): missing wasm support source ${_no_io_stubs}")
    endif()
    file(GLOB _parser_module_headers CONFIGURE_DEPENDS "${_kit_include}/pj_base/parser_module/*.hpp")
    set(_wasm_dir "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_parser_module_wasm")
    set(_wasm_raw "${_wasm_dir}/${TARGET}.raw.wasm")
    set(_wasm_output "${_wasm_dir}/${TARGET}.wasm")

    add_custom_command(
      OUTPUT "${_wasm_raw}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_wasm_dir}"
      COMMAND "${_wasi_clang}"
        --target=wasm32-wasip1
        --sysroot=${_wasi_sysroot}
        -mexec-model=reactor
        -std=c++17
        -fno-exceptions
        -fno-rtti
        -fvisibility=hidden
        -O1
        -Wall -Wextra -Werror
        -DPJ_PARSER_MODULE_CLAIM_COUNT=${_claim_count}
        -I${_kit_include}
        "${_module_source}"
        "${_no_io_stubs}"
        -Wl,--export=pj_module_abi
        -Wl,--export=pj_module_create
        -Wl,--export=pj_module_destroy
        -Wl,--export=pj_module_bind
        -Wl,--export=pj_module_parse
        -Wl,--export=pj_module_last_error
        -Wl,--export=pj_module_alloc
        -Wl,--export=pj_module_free
        -Wl,--export-memory
        -Wl,--max-memory=${PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES}
        -Wl,-z,stack-size=${PJ_PARSER_MODULE_WASM_STACK_SIZE_BYTES}
        -Wl,--stack-first
        -o "${_wasm_raw}"
      DEPENDS "${_module_source}" "${_no_io_stubs}" ${_parser_module_headers}
      COMMENT "Compiling ${TARGET} as a C++17 WASI reactor"
      VERBATIM
    )
    add_custom_command(
      OUTPUT "${_wasm_output}"
      COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0
        $<TARGET_FILE:plotjuggler_sdk::wasm_embed_manifest>
        embed "${_wasm_raw}" "${_module_manifest}" "${_wasm_output}"
      COMMAND ${CMAKE_COMMAND} -E env ASAN_OPTIONS=detect_leaks=0
        $<TARGET_FILE:plotjuggler_sdk::wasm_embed_manifest>
        verify "${_wasm_output}" "${_module_manifest}"
        "${PJ_PARSER_MODULE_WASM_MAX_MEMORY_BYTES}"
      DEPENDS "${_wasm_raw}" "${_module_manifest}" plotjuggler_sdk::wasm_embed_manifest
      COMMENT "Embedding and auditing ${TARGET} parser-module manifest"
      VERBATIM
    )
    add_custom_target(${TARGET}_wasm ALL DEPENDS "${_wasm_output}")
    set_property(TARGET ${TARGET}_wasm PROPERTY PJ_PARSER_MODULE_WASM_PATH "${_wasm_output}")
  endif()
endfunction()
