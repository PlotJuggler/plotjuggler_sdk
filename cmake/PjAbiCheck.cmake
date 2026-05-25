# PjAbiCheck.cmake
#
# ABI drift detection via libabigail (abidw / abidiff).
#
# This provides two CMake targets and wraps them in CTest for CI:
#
#   abi_check            — diff the current build against the checked-in
#                          baseline. Exit 0 on no change or
#                          backward-compatible additions; non-zero if
#                          incompatible changes snuck in.
#
#   abi_update_baseline  — regenerate the baseline .abi file. Run this
#                          intentionally when landing a planned ABI change
#                          (e.g. promoting a tail slot into MIN_VTABLE_SIZE,
#                          bumping PJ_ABI_VERSION for a major break).
#
# Baseline location: pj_base/abi/baseline.abi — the single source of
# truth for what the current ABI looks like. The baseline is generated from
# a canary plugin DSO (mock_data_source_plugin) whose symbol surface
# exercises the full ABI header set via the SDK.
#
# Scope: only types/symbols reachable from pj_base/include headers are
# tracked (via --headers-dir). Plugin-internal types and stdlib-internal
# symbols are filtered out.
#
# abidiff exit-bit semantics (from libabigail docs):
#   bit 0 (value 1)  tool error               (hard fail)
#   bit 1 (value 2)  user-error               (hard fail)
#   bit 2 (value 4)  ABI change               (warn — may be compatible)
#   bit 3 (value 8)  ABI INCOMPATIBLE change  (hard fail)
#
# We gate on bit 8. Bit 4 alone (compatible addition) is allowed without
# baseline update, but tends to mean the baseline is stale — the
# abi_check target prints a nudge in that case.

if(NOT PJ_ENABLE_ABI_CHECK)
  return()
endif()

find_program(ABIDW_EXECUTABLE abidw)
find_program(ABIDIFF_EXECUTABLE abidiff)

if(NOT ABIDW_EXECUTABLE OR NOT ABIDIFF_EXECUTABLE)
  message(WARNING
    "PJ_ENABLE_ABI_CHECK=ON but libabigail (abidw/abidiff) not found. "
    "Install with `apt-get install abigail-tools` or equivalent. "
    "Skipping ABI gate.")
  return()
endif()

set(_pj_abi_baseline "${CMAKE_SOURCE_DIR}/pj_base/abi/baseline.abi")
set(_pj_abi_canary_target mock_data_source_plugin)
set(_pj_abi_headers_dir "${CMAKE_SOURCE_DIR}/pj_base/include")

# --- Regenerate the baseline -------------------------------------------------
# Use this when landing an intentional, reviewed ABI change. The output is
# checked in so CI has something to diff against.
add_custom_target(abi_update_baseline
  COMMAND ${ABIDW_EXECUTABLE}
    --headers-dir ${_pj_abi_headers_dir}
    --drop-private-types
    --no-show-locs
    $<TARGET_FILE:${_pj_abi_canary_target}>
    -o ${_pj_abi_baseline}
  DEPENDS ${_pj_abi_canary_target}
  COMMENT "Regenerating pj_base/abi/baseline.abi (intentional ABI change — review the diff)"
  VERBATIM
)

# --- Check the current build against the baseline ----------------------------
add_custom_target(abi_check
  COMMAND ${CMAKE_COMMAND}
    -DABIDIFF_EXECUTABLE=${ABIDIFF_EXECUTABLE}
    -DABI_BASELINE=${_pj_abi_baseline}
    -DABI_CANARY=$<TARGET_FILE:${_pj_abi_canary_target}>
    -DABI_HEADERS_DIR=${_pj_abi_headers_dir}
    -P ${CMAKE_SOURCE_DIR}/cmake/PjAbiCheckRun.cmake
  DEPENDS ${_pj_abi_canary_target}
  COMMENT "Checking ABI drift vs pj_base/abi/baseline.abi"
  VERBATIM
)

# --- CTest integration -------------------------------------------------------
# A ctest entry makes the gate part of the default ./test.sh workflow.
if(PJ_BUILD_TESTS)
  add_test(NAME abi_check_test
    COMMAND ${CMAKE_COMMAND}
      -DABIDIFF_EXECUTABLE=${ABIDIFF_EXECUTABLE}
      -DABI_BASELINE=${_pj_abi_baseline}
      -DABI_CANARY=$<TARGET_FILE:${_pj_abi_canary_target}>
      -DABI_HEADERS_DIR=${_pj_abi_headers_dir}
      -P ${CMAKE_SOURCE_DIR}/cmake/PjAbiCheckRun.cmake
  )
  # The canary target is built as part of the normal build; no extra
  # dependency wiring needed for ctest.
endif()
