# PjAbiCheckRun.cmake
#
# Helper invoked by the abi_check custom target and ctest runner. Runs
# abidiff, interprets the exit-bit mask per libabigail's convention, and
# exits with an appropriate pass/warn/fail signal.

if(NOT ABIDIFF_EXECUTABLE)
  message(FATAL_ERROR "PjAbiCheckRun: ABIDIFF_EXECUTABLE not set")
endif()
if(NOT ABI_BASELINE)
  message(FATAL_ERROR "PjAbiCheckRun: ABI_BASELINE not set")
endif()
if(NOT ABI_CANARY)
  message(FATAL_ERROR "PjAbiCheckRun: ABI_CANARY not set")
endif()

if(NOT EXISTS "${ABI_BASELINE}")
  message(FATAL_ERROR
    "ABI baseline not found at ${ABI_BASELINE}. Run "
    "`cmake --build <build-dir> --target abi_update_baseline` to create it.")
endif()
if(NOT EXISTS "${ABI_CANARY}")
  message(FATAL_ERROR
    "ABI canary DSO not found at ${ABI_CANARY}. Build mock_data_source_plugin first.")
endif()

set(_abidiff_args "${ABI_BASELINE}" "${ABI_CANARY}")
if(ABI_HEADERS_DIR)
  list(PREPEND _abidiff_args --headers-dir2 "${ABI_HEADERS_DIR}")
endif()

execute_process(
  COMMAND "${ABIDIFF_EXECUTABLE}" ${_abidiff_args}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
)

# abidiff exit mask:
#   0x01 tool error       (fatal)
#   0x02 user-error       (fatal)
#   0x04 ABI change       (compatible — warn)
#   0x08 ABI INCOMPATIBLE (fatal)
math(EXPR _bit_tool     "${_rc} & 1")
math(EXPR _bit_user     "${_rc} & 2")
math(EXPR _bit_compat   "${_rc} & 4")
math(EXPR _bit_incompat "${_rc} & 8")

if(_bit_tool OR _bit_user)
  message(FATAL_ERROR
    "abidiff hit a tool/user error (exit=${_rc}):\n"
    "stdout:\n${_out}\n"
    "stderr:\n${_err}")
endif()

if(_bit_incompat)
  message(FATAL_ERROR
    "ABI INCOMPATIBLE change detected vs baseline.\n"
    "This is a v-bump situation — either revert the offending change, or "
    "if the break is intentional, bump PJ_ABI_VERSION and run "
    "`cmake --build <build-dir> --target abi_update_baseline` to adopt "
    "the new baseline.\n\n"
    "abidiff output:\n${_out}")
endif()

if(_bit_compat)
  message(WARNING
    "ABI change vs baseline (backward-compatible — e.g. tail slot added).\n"
    "If the change is intentional, run "
    "`cmake --build <build-dir> --target abi_update_baseline` to refresh "
    "the baseline so CI stops nagging.\n\n"
    "abidiff output:\n${_out}")
  # Do NOT fail the build — backward-compatible additions are allowed.
endif()

if(_rc EQUAL 0)
  message(STATUS "ABI check passed — no drift vs baseline.")
endif()
