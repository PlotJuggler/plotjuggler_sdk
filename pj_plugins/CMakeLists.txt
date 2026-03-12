add_subdirectory(dialog_protocol)

# ---------------------------------------------------------------------------
# pj_data_source_host — host-side DataSource library loader
# ---------------------------------------------------------------------------

add_library(pj_data_source_host STATIC
  src/data_source_library.cpp
)
target_include_directories(pj_data_source_host PUBLIC include)
target_compile_features(pj_data_source_host PUBLIC cxx_std_20)
target_compile_options(pj_data_source_host PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(pj_data_source_host
  PUBLIC
    pj_base
    pj_dialog_protocol
  PRIVATE
    ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# Mock DataSource plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_data_source_plugin SHARED examples/mock_data_source.cpp)
target_compile_features(mock_data_source_plugin PRIVATE cxx_std_20)
target_compile_options(mock_data_source_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_data_source_plugin PRIVATE pj_base)

# ---------------------------------------------------------------------------
# Combined DataSource+Dialog mock plugin (shared library)
# ---------------------------------------------------------------------------

add_library(mock_source_with_dialog_plugin SHARED examples/mock_source_with_dialog.cpp)
target_compile_features(mock_source_with_dialog_plugin PRIVATE cxx_std_20)
target_compile_options(mock_source_with_dialog_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_source_with_dialog_plugin PRIVATE pj_base pj_dialog_sdk)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

add_executable(data_source_library_test tests/data_source_library_test.cpp)
target_compile_definitions(data_source_library_test PRIVATE
  PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
)
target_compile_options(data_source_library_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(data_source_library_test PRIVATE
  pj_data_source_host pj_base GTest::gtest_main
)
add_test(NAME data_source_library_test COMMAND data_source_library_test)

# Integration test: DataSource + Dialog
add_executable(source_dialog_integration_test tests/source_dialog_integration_test.cpp)
target_compile_definitions(source_dialog_integration_test PRIVATE
  PJ_MOCK_SOURCE_WITH_DIALOG_PLUGIN_PATH="$<TARGET_FILE:mock_source_with_dialog_plugin>"
  PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
)
target_compile_options(source_dialog_integration_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(source_dialog_integration_test PRIVATE
  pj_data_source_host pj_dialog_host pj_base GTest::gtest_main
)
add_test(NAME source_dialog_integration_test COMMAND source_dialog_integration_test)
