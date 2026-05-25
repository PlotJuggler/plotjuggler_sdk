# PjPluginManifest.cmake
#
# Helper that emits a human-readable plugin manifest sidecar JSON next to a
# plugin shared library.
#
# The sidecar is for inspection, packaging diagnostics, and developer tooling
# only. PlotJuggler runtime discovery and marketplace validation do not read it;
# the plugin DSO's embedded manifest is the source of truth.
#
# The emitted file mirrors the plugin's manifest.json and adds build/discovery
# hints such as abi_major and family, so developers can inspect a build output
# without dlopen'ing the DSO.
#
# Usage:
#   include(PjPluginManifest)   # auto-included by the root CMakeLists.txt
#   add_library(csv_source_plugin SHARED csv_source.cpp)
#   pj_emit_plugin_manifest(csv_source_plugin
#     FAMILY        data_source
#     MANIFEST_FILE ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
#   )
#
# Writes <build-dir>/<target>.pjmanifest.json next to the DSO and installs it
# alongside the DSO.

include(GNUInstallDirs)  # CMAKE_INSTALL_LIBDIR — must be defined before install(FILES ...)

function(pj_emit_plugin_manifest TARGET)
  set(_options)
  set(_oneValueArgs FAMILY MANIFEST_FILE ABI_MAJOR)
  set(_multiValueArgs)
  cmake_parse_arguments(ARG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

  if(NOT ARG_FAMILY)
    message(FATAL_ERROR "pj_emit_plugin_manifest(${TARGET}): FAMILY is required")
  endif()

  # Symbol isolation — functional replacement for RTLD_DEEPBIND.
  #
  # Two complementary mechanisms are needed:
  #
  # 1. -fvisibility=hidden (compile-time): hides symbols DEFINED in the plugin's
  #    own source files. Prevents them from being interposable by the host.
  #
  # 2. -Wl,-Bsymbolic-functions (link-time): makes function calls WITHIN the .so
  #    resolve to the definitions inside it, bypassing the PLT entirely.
  #    This is critical for statically bundled deps (e.g. libssl.a from Conan)
  #    that were compiled WITHOUT -fvisibility=hidden: their symbols enter the .so
  #    with DEFAULT visibility, and without -Bsymbolic-functions their calls would
  #    still go through PLT → resolved to the host's namespace first → crash.
  #
  # Together: all function calls inside the plugin use the embedded static copies.
  # The two boot-level exports (pj_plugin_abi_version + PJ_get_<family>_vtable)
  # keep visibility("default") via the PJ_*_PLUGIN macros and are unaffected.
  # malloc / pthread / system calls are NOT defined in the plugin, so they still
  # resolve to the host — ASAN malloc interposition works correctly.
  #
  # -Bsymbolic-functions is Linux/ELF-specific. On macOS the linker uses
  # two-level namespace by default (equivalent behavior), so the flag is omitted.
  set_target_properties(${TARGET} PROPERTIES
    CXX_VISIBILITY_PRESET   hidden
    C_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
  )
  target_link_options(${TARGET} PRIVATE
    $<$<PLATFORM_ID:Linux>:-Wl,-Bsymbolic-functions>
  )

  set(_valid_families data_source message_parser toolbox dialog)
  list(FIND _valid_families "${ARG_FAMILY}" _family_idx)
  if(_family_idx LESS 0)
    message(FATAL_ERROR
      "pj_emit_plugin_manifest(${TARGET}): FAMILY \"${ARG_FAMILY}\" is invalid. "
      "Must be one of: ${_valid_families}")
  endif()

  if(NOT ARG_MANIFEST_FILE)
    set(ARG_MANIFEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json")
  endif()
  if(NOT EXISTS "${ARG_MANIFEST_FILE}")
    message(FATAL_ERROR
      "pj_emit_plugin_manifest(${TARGET}): MANIFEST_FILE not found: ${ARG_MANIFEST_FILE}")
  endif()

  if(NOT ARG_ABI_MAJOR)
    # Matches PJ_ABI_VERSION in pj_base/plugin_data_api.h. Bump in lockstep.
    set(ARG_ABI_MAJOR 5)
  endif()

  # Track manifest edits so CMake reconfigures when the source changes.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ARG_MANIFEST_FILE}")

  file(READ "${ARG_MANIFEST_FILE}" _src_json)

  foreach(_key IN ITEMS id name version)
    string(JSON _key_type ERROR_VARIABLE _err TYPE "${_src_json}" "${_key}")
    if(_err OR NOT _key_type STREQUAL "STRING")
      message(FATAL_ERROR "${ARG_MANIFEST_FILE}: missing required string \"${_key}\" key")
    endif()

    string(JSON _key_value GET "${_src_json}" "${_key}")
    if(_key_value STREQUAL "")
      message(FATAL_ERROR "${ARG_MANIFEST_FILE}: required string \"${_key}\" key must not be empty")
    endif()
  endforeach()

  # Augment: add abi_major + family. string(JSON SET) preserves other keys.
  set(_sidecar_json "${_src_json}")
  string(JSON _sidecar_json SET "${_sidecar_json}" "abi_major" "${ARG_ABI_MAJOR}")
  string(JSON _sidecar_json SET "${_sidecar_json}" "family"    "\"${ARG_FAMILY}\"")

  # Write to build tree. The file lives next to the DSO.
  set(_sidecar_path "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.pjmanifest.json")
  file(WRITE "${_sidecar_path}" "${_sidecar_json}\n")

  add_custom_command(
    TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_sidecar_path}"
      "$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.pjmanifest.json"
    COMMENT "Copying human-readable ${TARGET}.pjmanifest.json next to DSO"
    VERBATIM
  )

  get_target_property(_type ${TARGET} TYPE)
  if(_type STREQUAL "MODULE_LIBRARY" OR _type STREQUAL "SHARED_LIBRARY")
    install(FILES "${_sidecar_path}"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
  endif()
endfunction()
