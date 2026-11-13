# PjParserModule.cmake
#
# Native functional parser-module target helper. Wasm manifest embedding and
# toolchain support arrive with the wasm loader milestone.

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
    message(FATAL_ERROR "pj_add_parser_module(${TARGET}): TARGETS native is required")
  endif()
  foreach(_requested_target IN LISTS ARG_TARGETS)
    if(NOT _requested_target STREQUAL "native")
      message(FATAL_ERROR
        "pj_add_parser_module(${TARGET}): TARGETS ${_requested_target} is unavailable; "
        "wasm support arrives with the SDK wasm loader milestone")
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
endfunction()
