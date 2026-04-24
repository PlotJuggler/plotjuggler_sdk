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

if(PJ_BUILD_TESTS)

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
# FileSourceBase mock plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_file_source_plugin SHARED examples/mock_file_source.cpp)
target_compile_features(mock_file_source_plugin PRIVATE cxx_std_20)
target_compile_options(mock_file_source_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_file_source_plugin PRIVATE pj_base)

endif() # PJ_BUILD_TESTS

# ---------------------------------------------------------------------------
# pj_message_parser_host — host-side MessageParser library loader
# ---------------------------------------------------------------------------

add_library(pj_message_parser_host STATIC
  src/message_parser_library.cpp
)
target_include_directories(pj_message_parser_host PUBLIC include)
target_compile_features(pj_message_parser_host PUBLIC cxx_std_20)
target_compile_options(pj_message_parser_host PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(pj_message_parser_host
  PUBLIC
    pj_base
    pj_dialog_protocol
  PRIVATE
    ${CMAKE_DL_LIBS}
)

if(PJ_BUILD_TESTS)

# ---------------------------------------------------------------------------
# Mock MessageParser plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_json_parser_plugin SHARED examples/mock_json_parser.cpp)
target_compile_features(mock_json_parser_plugin PRIVATE cxx_std_20)
target_compile_options(mock_json_parser_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_json_parser_plugin PRIVATE pj_base)

# ---------------------------------------------------------------------------
# Mock Schema Parser plugin (shared library, high-throughput bound-fields example)
# ---------------------------------------------------------------------------

add_library(mock_schema_parser_plugin SHARED examples/mock_schema_parser.cpp)
target_compile_features(mock_schema_parser_plugin PRIVATE cxx_std_20)
target_compile_options(mock_schema_parser_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_schema_parser_plugin PRIVATE pj_base)

endif() # PJ_BUILD_TESTS

# ---------------------------------------------------------------------------
# pj_toolbox_host — host-side Toolbox library loader
# ---------------------------------------------------------------------------

add_library(pj_toolbox_host STATIC
  src/toolbox_library.cpp
)
target_include_directories(pj_toolbox_host PUBLIC include)
target_compile_features(pj_toolbox_host PUBLIC cxx_std_20)
target_compile_options(pj_toolbox_host PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(pj_toolbox_host
  PUBLIC
    pj_base
    pj_dialog_protocol
  PRIVATE
    ${CMAKE_DL_LIBS}
)

if(PJ_BUILD_TESTS)

# ---------------------------------------------------------------------------
# Mock Toolbox plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_toolbox_plugin SHARED examples/mock_toolbox.cpp)
target_compile_features(mock_toolbox_plugin PRIVATE cxx_std_20)
target_compile_options(mock_toolbox_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_toolbox_plugin PRIVATE pj_base)

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

# Integration test: FileSourceBase lifecycle
add_executable(file_source_integration_test tests/file_source_integration_test.cpp)
target_compile_definitions(file_source_integration_test PRIVATE
  PJ_MOCK_FILE_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_file_source_plugin>"
)
target_compile_options(file_source_integration_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(file_source_integration_test PRIVATE
  pj_data_source_host pj_base GTest::gtest_main
)
add_test(NAME file_source_integration_test COMMAND file_source_integration_test)

# Integration test: MessageParser library loader
add_executable(message_parser_library_test tests/message_parser_library_test.cpp)
target_compile_definitions(message_parser_library_test PRIVATE
  PJ_MOCK_JSON_PARSER_PLUGIN_PATH="$<TARGET_FILE:mock_json_parser_plugin>"
)
target_compile_options(message_parser_library_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(message_parser_library_test PRIVATE
  pj_message_parser_host pj_base GTest::gtest_main
)
add_test(NAME message_parser_library_test COMMAND message_parser_library_test)

# Integration test: Delegated ingest (DataSource + MessageParser end-to-end)
add_executable(delegated_ingest_integration_test tests/delegated_ingest_integration_test.cpp)
target_compile_definitions(delegated_ingest_integration_test PRIVATE
  PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
  PJ_MOCK_JSON_PARSER_PLUGIN_PATH="$<TARGET_FILE:mock_json_parser_plugin>"
)
target_compile_options(delegated_ingest_integration_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(delegated_ingest_integration_test PRIVATE
  pj_data_source_host pj_message_parser_host pj_base GTest::gtest_main
)
add_test(NAME delegated_ingest_integration_test COMMAND delegated_ingest_integration_test)

# Integration test: Toolbox library loader
add_executable(toolbox_plugin_test tests/toolbox_plugin_test.cpp)
target_compile_definitions(toolbox_plugin_test PRIVATE
  PJ_MOCK_TOOLBOX_PLUGIN_PATH="$<TARGET_FILE:mock_toolbox_plugin>"
)
target_compile_options(toolbox_plugin_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(toolbox_plugin_test PRIVATE
  pj_toolbox_host pj_base GTest::gtest_main
)
add_test(NAME toolbox_plugin_test COMMAND toolbox_plugin_test)

endif() # PJ_BUILD_TESTS
