# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)

# The fixture checks implicit dependency search, not explicit loader-environment
# injection. On ELF, an empty LD_LIBRARY_PATH component means CWD and relative
# components change meaning when the fixture enters its decoy directory. Remove
# those before launching the process (glibc snapshots this variable at startup),
# preserving absolute dependency directories needed by Conan/system libraries.
if(NOT DEFINED TEST_EXECUTABLE)
  message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()
set(_absolute_library_paths)
string(REPLACE ":" ";" _library_paths "$ENV{LD_LIBRARY_PATH}")
foreach(_path IN LISTS _library_paths)
  if(IS_ABSOLUTE "${_path}")
    list(APPEND _absolute_library_paths "${_path}")
  endif()
endforeach()
if(_absolute_library_paths)
  list(JOIN _absolute_library_paths ":" _clean_library_path)
  set(ENV{LD_LIBRARY_PATH} "${_clean_library_path}")
else()
  unset(ENV{LD_LIBRARY_PATH})
endif()
execute_process(COMMAND "${TEST_EXECUTABLE}" ${TEST_ARGUMENTS} RESULT_VARIABLE _result)
if(NOT _result STREQUAL "0")
  message(FATAL_ERROR "plugin_catalog_test failed: ${_result}")
endif()
