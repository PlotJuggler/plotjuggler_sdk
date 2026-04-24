# ---------------------------------------------------------------------------
# pj_base — vocabulary types, zero external dependencies
# ---------------------------------------------------------------------------

add_library(pj_base STATIC
  src/type_tree.cpp
)
target_include_directories(pj_base PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_base PUBLIC cxx_std_20)
target_compile_options(pj_base PRIVATE
  ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
)
set_target_properties(pj_base PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME DataSource
)
if(PJ_ASSERT_THROWS)
  target_compile_definitions(pj_base PUBLIC PJ_ASSERT_THROWS)
endif()

# ---------------------------------------------------------------------------
# Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt)
# ---------------------------------------------------------------------------

if(PJ_INSTALL_SDK)
  install(TARGETS pj_base EXPORT PlotJugglerSDKTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
  )
  install(DIRECTORY include/ DESTINATION include)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  set(PJ_BASE_TESTS
    tests/types_test.cpp
    tests/type_tree_test.cpp
    tests/span_test.cpp
    tests/expected_test.cpp
    tests/plugin_data_api_test.cpp
    tests/data_source_protocol_test.cpp
    # TODO: data_source_plugin_base_test.cpp and
    # message_parser_plugin_base_test.cpp exercised old bind_write_host /
    # bind_runtime_host slots that were already removed by the time the
    # service-registry API landed. Coverage for those paths is provided
    # today by the *_library_test.cpp integration tests. The old unit
    # tests can either be rewritten to target the v4 service-registry
    # flow or deleted outright.
    tests/abi_layout_sentinels_test.cpp
    tests/platform_test.cpp
    tests/arrow_holders_test.cpp
    tests/media_metadata_test.cpp
  )

  foreach(test_src ${PJ_BASE_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE pj_base GTest::gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
