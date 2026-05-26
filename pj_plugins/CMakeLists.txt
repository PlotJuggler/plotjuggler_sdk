find_package(nlohmann_json REQUIRED)

add_library(pj_plugin_loader_detail STATIC
  src/detail/vtable_validation.cpp
)
target_include_directories(pj_plugin_loader_detail PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dialog_protocol/include>
)
target_compile_features(pj_plugin_loader_detail PUBLIC cxx_std_20)
target_compile_options(pj_plugin_loader_detail PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_plugin_loader_detail PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME plugin_loader_detail
)
target_link_libraries(pj_plugin_loader_detail PUBLIC pj_base)

add_subdirectory(dialog_protocol)

# ---------------------------------------------------------------------------
# pj_plugin_sdk — umbrella INTERFACE library that exposes the full plugin-author
# SDK surface: C++ SDK headers (MessageParserPluginBase, ObjectIngestPolicy,
# parser trampolines, ...) PLUS the dialog SDK (widget_data, widget_event) so
# plugin authors can link one target and get everything they need.
# ---------------------------------------------------------------------------

add_library(pj_plugin_sdk INTERFACE)
target_include_directories(pj_plugin_sdk INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(pj_plugin_sdk INTERFACE pj_base pj_dialog_sdk)
set_target_properties(pj_plugin_sdk PROPERTIES EXPORT_NAME plugin_sdk)
add_library(plotjuggler_core::plugin_sdk ALIAS pj_plugin_sdk)

# ---------------------------------------------------------------------------
# pj_plugin_catalog — host-side embedded-manifest discovery
# ---------------------------------------------------------------------------

add_library(pj_plugin_catalog STATIC
  src/plugin_catalog.cpp
)
target_include_directories(pj_plugin_catalog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_plugin_catalog PUBLIC cxx_std_20)
target_compile_options(pj_plugin_catalog PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_plugin_catalog PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME plugin_catalog
)
target_link_libraries(pj_plugin_catalog
  PUBLIC
    pj_base
  PRIVATE
    pj_plugin_loader_detail
    pj_dialog_protocol
    $<BUILD_INTERFACE:pj_internal_fmt>
    ${CMAKE_DL_LIBS}
    nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------------------------
# pj_data_source_host — host-side DataSource library loader
# ---------------------------------------------------------------------------

add_library(pj_data_source_host STATIC
  src/data_source_library.cpp
)
target_include_directories(pj_data_source_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_data_source_host PUBLIC cxx_std_20)
target_compile_options(pj_data_source_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_data_source_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME data_source_host
)
target_link_libraries(pj_data_source_host
  PUBLIC
    pj_base
    pj_dialog_protocol
    pj_plugin_catalog
  PRIVATE
    pj_plugin_loader_detail
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

add_library(missing_id_data_source_plugin SHARED tests/missing_id_data_source_plugin.cpp)
target_compile_features(missing_id_data_source_plugin PRIVATE cxx_std_20)
target_compile_options(missing_id_data_source_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(missing_id_data_source_plugin PRIVATE pj_base)

add_library(mock_data_source_v2_plugin SHARED tests/mock_data_source_v2_plugin.cpp)
target_compile_features(mock_data_source_v2_plugin PRIVATE cxx_std_20)
target_compile_options(mock_data_source_v2_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_data_source_v2_plugin PRIVATE pj_base)

add_library(invalid_optional_manifest_data_source_plugin SHARED tests/invalid_optional_manifest_data_source_plugin.cpp)
target_compile_features(invalid_optional_manifest_data_source_plugin PRIVATE cxx_std_20)
target_compile_options(invalid_optional_manifest_data_source_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(invalid_optional_manifest_data_source_plugin PRIVATE pj_base)

add_library(missing_required_slots_plugin SHARED tests/missing_required_slots_plugin.cpp)
target_compile_features(missing_required_slots_plugin PRIVATE cxx_std_20)
target_compile_options(missing_required_slots_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(missing_required_slots_plugin PRIVATE pj_base)

endif() # PJ_BUILD_TESTS

# ---------------------------------------------------------------------------
# pj_message_parser_host — host-side MessageParser library loader
# ---------------------------------------------------------------------------

add_library(pj_message_parser_host STATIC
  src/message_parser_library.cpp
)
target_include_directories(pj_message_parser_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_message_parser_host PUBLIC cxx_std_20)
target_compile_options(pj_message_parser_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_message_parser_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME message_parser_host
)
target_link_libraries(pj_message_parser_host
  PUBLIC
    pj_base
    pj_dialog_protocol
  PRIVATE
    pj_plugin_loader_detail
    ${CMAKE_DL_LIBS}
)

if(PJ_BUILD_TESTS)

# ---------------------------------------------------------------------------
# Mock MessageParser plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_json_parser_plugin SHARED examples/mock_json_parser.cpp)
target_compile_features(mock_json_parser_plugin PRIVATE cxx_std_20)
target_compile_options(mock_json_parser_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_json_parser_plugin PRIVATE pj_plugin_sdk)

# ---------------------------------------------------------------------------
# Mock Schema Parser plugin (shared library, high-throughput bound-fields example)
# ---------------------------------------------------------------------------

add_library(mock_schema_parser_plugin SHARED examples/mock_schema_parser.cpp)
target_compile_features(mock_schema_parser_plugin PRIVATE cxx_std_20)
target_compile_options(mock_schema_parser_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_schema_parser_plugin PRIVATE pj_plugin_sdk)

endif() # PJ_BUILD_TESTS

# ---------------------------------------------------------------------------
# pj_toolbox_host — host-side Toolbox library loader
# ---------------------------------------------------------------------------

add_library(pj_toolbox_host STATIC
  src/toolbox_library.cpp
)
target_include_directories(pj_toolbox_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_toolbox_host PUBLIC cxx_std_20)
target_compile_options(pj_toolbox_host PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_toolbox_host PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME toolbox_host
)
target_link_libraries(pj_toolbox_host
  PUBLIC
    pj_base
    pj_dialog_protocol
  PRIVATE
    pj_plugin_loader_detail
    ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# pj_plugin_runtime_catalog — shared host-side plugin loading catalog
# ---------------------------------------------------------------------------

add_library(pj_plugin_runtime_catalog STATIC
  src/plugin_runtime_catalog.cpp
)
target_include_directories(pj_plugin_runtime_catalog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_plugin_runtime_catalog PUBLIC cxx_std_20)
target_compile_options(pj_plugin_runtime_catalog PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(pj_plugin_runtime_catalog PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME plugin_runtime_catalog
)
target_link_libraries(pj_plugin_runtime_catalog
  PUBLIC
    pj_data_source_host
    pj_message_parser_host
    pj_toolbox_host
    pj_plugin_catalog
    pj_base
  PRIVATE
    nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------------------------
# pj_plugin_host — umbrella INTERFACE that bundles every host-side loader.
# Hosts (the PlotJuggler app, test rigs) link this single target to get
# discovery + all four plugin-family loaders + the runtime catalog.
# ---------------------------------------------------------------------------

add_library(pj_plugin_host INTERFACE)
target_link_libraries(pj_plugin_host INTERFACE
  pj_plugin_runtime_catalog
  pj_data_source_host
  pj_message_parser_host
  pj_toolbox_host
  pj_dialog_library
  pj_plugin_catalog
)
set_target_properties(pj_plugin_host PROPERTIES EXPORT_NAME plugin_host)
add_library(plotjuggler_core::plugin_host ALIAS pj_plugin_host)

if(PJ_BUILD_TESTS)

# ---------------------------------------------------------------------------
# Mock Toolbox plugin (shared library, for dlopen tests)
# ---------------------------------------------------------------------------

add_library(mock_toolbox_plugin SHARED examples/mock_toolbox.cpp)
target_compile_features(mock_toolbox_plugin PRIVATE cxx_std_20)
target_compile_options(mock_toolbox_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_toolbox_plugin PRIVATE pj_base)

add_library(static_manifest_dialog_plugin SHARED tests/static_manifest_dialog_plugin.cpp)
target_compile_features(static_manifest_dialog_plugin PRIVATE cxx_std_20)
target_compile_options(static_manifest_dialog_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(static_manifest_dialog_plugin PRIVATE pj_dialog_protocol)

add_library(legacy_macro_dialog_plugin SHARED tests/legacy_macro_dialog_plugin.cpp)
target_compile_features(legacy_macro_dialog_plugin PRIVATE cxx_std_20)
target_compile_options(legacy_macro_dialog_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(legacy_macro_dialog_plugin PRIVATE pj_dialog_protocol)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

add_executable(data_source_library_test tests/data_source_library_test.cpp)
target_compile_definitions(data_source_library_test PRIVATE
  PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
  PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_required_slots_plugin>"
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
  PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_required_slots_plugin>"
)
target_compile_options(message_parser_library_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(message_parser_library_test PRIVATE
  pj_message_parser_host pj_base GTest::gtest_main
)
add_test(NAME message_parser_library_test COMMAND message_parser_library_test)

# Unit test: ObjectIngestPolicyResolver cascade rules.
add_executable(object_ingest_policy_test tests/object_ingest_policy_test.cpp)
target_compile_options(object_ingest_policy_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(object_ingest_policy_test PRIVATE
  pj_base GTest::gtest_main
)
target_include_directories(object_ingest_policy_test PRIVATE include)
add_test(NAME object_ingest_policy_test COMMAND object_ingest_policy_test)

# TODO(v3-port): delegated_ingest_integration_test.cpp uses old bindWriteHost /
# bindRuntimeHost methods and get_last_error slots removed in v3. Pending port
# to the service registry + PJ_error_t* pattern. Its coverage (parser binding +
# raw-message dispatch) remains verified by data_source_library_test.cpp and
# message_parser_library_test.cpp individually.
#
# add_executable(delegated_ingest_integration_test tests/delegated_ingest_integration_test.cpp)
# target_compile_definitions(delegated_ingest_integration_test PRIVATE
#   PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
#   PJ_MOCK_JSON_PARSER_PLUGIN_PATH="$<TARGET_FILE:mock_json_parser_plugin>"
# )
# target_compile_options(delegated_ingest_integration_test PRIVATE ${PJ_WARNING_FLAGS})
# target_link_libraries(delegated_ingest_integration_test PRIVATE
#   pj_data_source_host pj_message_parser_host pj_base GTest::gtest_main
# )
# add_test(NAME delegated_ingest_integration_test COMMAND delegated_ingest_integration_test)

# Integration test: Toolbox library loader
add_executable(toolbox_plugin_test tests/toolbox_plugin_test.cpp)
target_compile_definitions(toolbox_plugin_test PRIVATE
  PJ_MOCK_TOOLBOX_PLUGIN_PATH="$<TARGET_FILE:mock_toolbox_plugin>"
  PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_required_slots_plugin>"
)
target_compile_options(toolbox_plugin_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(toolbox_plugin_test PRIVATE
  pj_toolbox_host pj_base GTest::gtest_main
)
add_test(NAME toolbox_plugin_test COMMAND toolbox_plugin_test)

# ---------------------------------------------------------------------------
# Plugin catalog DSO scanner test.
# ---------------------------------------------------------------------------
add_executable(plugin_catalog_test tests/plugin_catalog_test.cpp)
target_compile_definitions(plugin_catalog_test PRIVATE
  PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
  PJ_MOCK_JSON_PARSER_PLUGIN_PATH="$<TARGET_FILE:mock_json_parser_plugin>"
  PJ_MOCK_TOOLBOX_PLUGIN_PATH="$<TARGET_FILE:mock_toolbox_plugin>"
  PJ_MOCK_DIALOG_PLUGIN_PATH="$<TARGET_FILE:mock_dialog_plugin>"
  PJ_STATIC_MANIFEST_DIALOG_PLUGIN_PATH="$<TARGET_FILE:static_manifest_dialog_plugin>"
  PJ_LEGACY_MACRO_DIALOG_PLUGIN_PATH="$<TARGET_FILE:legacy_macro_dialog_plugin>"
  PJ_MISSING_DIALOG_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_dialog_required_slots_plugin>"
  PJ_MISSING_ID_PLUGIN_PATH="$<TARGET_FILE:missing_id_data_source_plugin>"
  PJ_INVALID_OPTIONAL_PLUGIN_PATH="$<TARGET_FILE:invalid_optional_manifest_data_source_plugin>"
  PJ_MISSING_REQUIRED_SLOTS_PLUGIN_PATH="$<TARGET_FILE:missing_required_slots_plugin>"
)
target_compile_options(plugin_catalog_test PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(plugin_catalog_test PRIVATE
  pj_plugin_catalog GTest::gtest_main
)
add_dependencies(plugin_catalog_test mock_data_source_plugin mock_json_parser_plugin
                 mock_toolbox_plugin mock_dialog_plugin missing_id_data_source_plugin
                 invalid_optional_manifest_data_source_plugin missing_required_slots_plugin
                 static_manifest_dialog_plugin legacy_macro_dialog_plugin
                 missing_dialog_required_slots_plugin)
add_test(NAME plugin_catalog_test COMMAND plugin_catalog_test)

endif() # PJ_BUILD_TESTS

# ---------------------------------------------------------------------------
# Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt)
# ---------------------------------------------------------------------------

if(PJ_INSTALL_SDK)
  install(TARGETS
      pj_plugin_loader_detail
      pj_plugin_sdk
      pj_plugin_catalog
      pj_data_source_host
      pj_message_parser_host
      pj_toolbox_host
      pj_plugin_runtime_catalog
      pj_plugin_host
    EXPORT plotjuggler_coreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
