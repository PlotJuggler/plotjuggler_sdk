# Compile packaged fixture sources without fetching the SDK source archive.
include_guard(GLOBAL)

# The one authoritative fixture list: <name>=<source path relative to pj_plugins/>.
# Consumed by pj_add_sdk_test_fixture() below and by the root install(FILES) rule.
set(PJ_SDK_TEST_FIXTURES
  mock_data_source=examples/mock_data_source.cpp
  mock_file_source=examples/mock_file_source.cpp
  mock_toolbox=examples/mock_toolbox.cpp
  missing_id_data_source=tests/missing_id_data_source_plugin.cpp
)

# pj_add_sdk_test_fixture(<name> <out_target>)
# name: one of the PJ_SDK_TEST_FIXTURES names above.
# out_target receives the reusable SHARED target name. Link/load it as usual;
# missing_id_data_source intentionally has an invalid plugin manifest.
function(pj_add_sdk_test_fixture name out_target)
  set(_relpath "")
  set(_names "")
  foreach(_entry IN LISTS PJ_SDK_TEST_FIXTURES)
    string(REPLACE "=" ";" _pair "${_entry}")
    list(GET _pair 0 _entry_name)
    list(APPEND _names ${_entry_name})
    if(_entry_name STREQUAL name)
      list(GET _pair 1 _relpath)
    endif()
  endforeach()
  if(_relpath STREQUAL "")
    message(FATAL_ERROR "Unknown SDK test fixture: ${name}. Available: ${_names}")
  endif()
  if(NOT TARGET plotjuggler_sdk::plugin_sdk)
    message(FATAL_ERROR "pj_add_sdk_test_fixture requires plotjuggler_sdk::plugin_sdk")
  endif()
  set(_target "pj_sdk_fixture_${name}")
  if(NOT TARGET ${_target})
    get_filename_component(_file "${_relpath}" NAME)
    set(_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/plotjuggler_sdk/test_fixtures/${_file}")
    if(NOT EXISTS "${_source}")
      set(_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../pj_plugins/${_relpath}")
    endif()
    add_library(${_target} SHARED "${_source}")
    target_compile_features(${_target} PRIVATE cxx_std_20)
    target_link_libraries(${_target} PRIVATE plotjuggler_sdk::plugin_sdk)
  endif()
  set(${out_target} "${_target}" PARENT_SCOPE)
endfunction()
