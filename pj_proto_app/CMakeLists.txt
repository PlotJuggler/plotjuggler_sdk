set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)
find_package(nlohmann_json REQUIRED)

add_executable(pj_proto_app
  src/main.cpp
  src/main_window.cpp
  src/plugin_registry.cpp
  src/data_source_session.cpp
  src/toolbox_session.cpp
  src/series_tree_model.cpp
  src/chart_panel.cpp
  src/time_range_slider.cpp
)

target_compile_features(pj_proto_app PRIVATE cxx_std_20)
target_compile_options(pj_proto_app PRIVATE ${PJ_WARNING_FLAGS})

target_link_libraries(pj_proto_app PRIVATE
  pj_datastore
  pj_data_source_host
  pj_message_parser_host
  pj_toolbox_host
  pj_plugin_runtime_catalog
  pj_dialog_engine_qt
  pj_marketplace
  pj_marketplace_ui
  nlohmann_json::nlohmann_json
  Qt6::Widgets
  Qt6::Charts
)

if(PJ_BUILD_TESTS)
  add_executable(proto_plugin_registry_test
    tests/plugin_registry_test.cpp
    src/plugin_registry.cpp
  )
  target_include_directories(proto_plugin_registry_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(proto_plugin_registry_test PRIVATE
    PJ_MOCK_DATA_SOURCE_PLUGIN_PATH="$<TARGET_FILE:mock_data_source_plugin>"
    PJ_MOCK_JSON_PARSER_PLUGIN_PATH="$<TARGET_FILE:mock_json_parser_plugin>"
  )
  target_link_libraries(proto_plugin_registry_test PRIVATE
    pj_datastore
    pj_data_source_host
    pj_message_parser_host
    pj_toolbox_host
    pj_plugin_runtime_catalog
    pj_marketplace
    GTest::gtest_main
    nlohmann_json::nlohmann_json
    Qt6::Core
  )
  add_dependencies(proto_plugin_registry_test mock_data_source_plugin mock_json_parser_plugin)
  set_target_properties(proto_plugin_registry_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
  )
  add_test(NAME proto_plugin_registry_test COMMAND proto_plugin_registry_test)
endif()
