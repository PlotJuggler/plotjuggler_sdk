set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)
find_package(nlohmann_json REQUIRED)

add_executable(pj_proto_app
  src/main.cpp
  src/main_window.cpp
  src/plugin_registry.cpp
  src/data_source_session.cpp
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
  pj_dialog_engine_qt
  pj_marketplace
  pj_marketplace_ui
  nlohmann_json::nlohmann_json
  Qt6::Widgets
  Qt6::Charts
)
