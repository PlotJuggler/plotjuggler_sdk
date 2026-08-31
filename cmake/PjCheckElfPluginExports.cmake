# PjCheckElfPluginExports.cmake — post-build gate for ELF plugin DSOs.
#
# Run in CMake script mode by pj_harden_plugin_exports() (cmake/PjPlugin.cmake):
#
#   cmake -DPLUGIN_SO=<path/to/plugin.so>
#         -DREQUIRED_EXPORTS=<sym1,sym2,...>
#         [-DNM_TOOL=<toolchain nm>]
#         -P PjCheckElfPluginExports.cmake
#
# Fails when:
#   1. the DSO exports any STB_GNU_UNIQUE symbol ('u' in `nm -D`). Exported
#      unique symbols are process-global despite RTLD_LOCAL: glibc pins the
#      first DSO providing such a name (dlclose stops unmapping it) and a
#      second copy of the plugin loaded from another path binds into the first
#      copy's statics — skipped constructors and cross-build state mixing.
#   2. any of REQUIRED_EXPORTS (comma-separated) is missing from the dynamic
#      symbol table — catches an over-aggressive export allowlist that would
#      make the host reject the plugin at the ABI handshake.

if(NOT PLUGIN_SO OR NOT REQUIRED_EXPORTS)
  message(FATAL_ERROR "PjCheckElfPluginExports: PLUGIN_SO and REQUIRED_EXPORTS are required")
endif()

# NM_TOOL lets the caller pass the toolchain's nm (CMAKE_NM) so cross-builds
# do not inspect target ELF files with an incompatible host nm.
if(NOT NM_TOOL)
  set(NM_TOOL nm)
endif()

execute_process(
  COMMAND "${NM_TOOL}" -D "${PLUGIN_SO}"
  OUTPUT_VARIABLE _dynsym
  ERROR_VARIABLE _nm_err
  RESULT_VARIABLE _nm_rc
)
if(NOT _nm_rc EQUAL 0)
  message(FATAL_ERROR
    "PjCheckElfPluginExports: ${NM_TOOL} -D failed on ${PLUGIN_SO} (rc=${_nm_rc}): ${_nm_err}")
endif()

string(REGEX MATCHALL "[^\n]*[ \t]u[ \t][^\n]*" _unique_syms "${_dynsym}")
list(LENGTH _unique_syms _unique_count)
if(_unique_count GREATER 0)
  list(SUBLIST _unique_syms 0 5 _unique_sample)
  list(JOIN _unique_sample "\n  " _unique_sample_text)
  message(FATAL_ERROR
    "PjCheckElfPluginExports: ${PLUGIN_SO} exports ${_unique_count} STB_GNU_UNIQUE "
    "symbol(s); they must all be localized (pj_configure_plugin / "
    "pj_harden_plugin_exports apply the version script that does). First few:\n"
    "  ${_unique_sample_text}")
endif()

# T/W: code, D/B/V: data (initialized / bss / weak), R: read-only data — the
# boot symbol pj_plugin_abi_version is a const object in some plugins.
string(REPLACE "," ";" _required "${REQUIRED_EXPORTS}")
foreach(_symbol IN LISTS _required)
  if(NOT _dynsym MATCHES "[ \t][TWVDBR][ \t]+${_symbol}(\n|$)")
    message(FATAL_ERROR
      "PjCheckElfPluginExports: required export \"${_symbol}\" is missing from "
      "${PLUGIN_SO} — the host would reject the plugin at the ABI handshake. "
      "Check the allowlist's REQUIRED_EXPORTS / FAMILIES.")
  endif()
endforeach()

message(STATUS "PjCheckElfPluginExports: ${PLUGIN_SO} — 0 unique symbols, all required exports present")
