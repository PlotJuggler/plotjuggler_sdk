cmake_minimum_required(VERSION 3.20)
project(pj_dialog_protocol LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# --- Header-only interface libraries ---

# Pure C ABI header (no deps)
add_library(pj_dialog_protocol INTERFACE)
target_include_directories(pj_dialog_protocol INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# C++ SDK (adds nlohmann/json for widget_data/widget_event)
find_package(nlohmann_json REQUIRED)

add_library(pj_dialog_sdk INTERFACE)
target_link_libraries(pj_dialog_sdk INTERFACE pj_dialog_protocol nlohmann_json::nlohmann_json)
target_include_directories(pj_dialog_sdk INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
set_target_properties(pj_dialog_sdk PROPERTIES EXPORT_NAME Dialog)

# Host-side C++ API (depends on protocol header + nlohmann/json)
add_library(pj_dialog_host INTERFACE)
target_link_libraries(pj_dialog_host INTERFACE pj_dialog_protocol nlohmann_json::nlohmann_json)
target_include_directories(pj_dialog_host INTERFACE include)

# ---------------------------------------------------------------------------
# pj_dialog_library — host-side dialog .so loader
# ---------------------------------------------------------------------------

add_library(pj_dialog_library STATIC
  src/dialog_library.cpp
)
target_include_directories(pj_dialog_library PUBLIC include)
target_compile_features(pj_dialog_library PUBLIC cxx_std_20)
target_compile_options(pj_dialog_library PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(pj_dialog_library
  PUBLIC
    pj_dialog_host
    pj_base
  PRIVATE
    ${CMAKE_DL_LIBS}
)

# --- Mock plugins (shared libraries for integration testing) ---

add_library(mock_dialog_plugin SHARED examples/mock_dialog.cpp)
target_compile_options(mock_dialog_plugin PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(mock_dialog_plugin PRIVATE pj_dialog_sdk)

# --- Tests ---

enable_testing()

set(PJ_PLUGIN_TESTS
  widget_data_test
  widget_event_test
  dialog_plugin_typed_test
  plugin_lifecycle_test
  dialog_handle_test
  widget_data_view_test
  widget_event_builder_test
  dialog_library_test
)

add_executable(widget_data_test tests/widget_data_test.cpp)
target_link_libraries(widget_data_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_data_test COMMAND widget_data_test)

add_executable(widget_event_test tests/widget_event_test.cpp)
target_link_libraries(widget_event_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_event_test COMMAND widget_event_test)

add_executable(dialog_plugin_typed_test tests/dialog_plugin_typed_test.cpp)
target_link_libraries(dialog_plugin_typed_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME dialog_plugin_typed_test COMMAND dialog_plugin_typed_test)

# Links mock_dialog.cpp statically (no dlopen) for lifecycle testing
add_executable(plugin_lifecycle_test tests/plugin_lifecycle_test.cpp examples/mock_dialog.cpp)
target_link_libraries(plugin_lifecycle_test PRIVATE pj_dialog_sdk GTest::gtest_main)
add_test(NAME plugin_lifecycle_test COMMAND plugin_lifecycle_test)

# --- Host-side API tests ---

# Links mock_dialog.cpp statically for RAII handle testing
add_executable(dialog_handle_test tests/dialog_handle_test.cpp examples/mock_dialog.cpp)
target_link_libraries(dialog_handle_test PRIVATE pj_dialog_host pj_dialog_sdk GTest::gtest_main)
add_test(NAME dialog_handle_test COMMAND dialog_handle_test)

add_executable(widget_data_view_test tests/widget_data_view_test.cpp)
target_link_libraries(widget_data_view_test PRIVATE pj_dialog_host GTest::gtest_main)
add_test(NAME widget_data_view_test COMMAND widget_data_view_test)

# Round-trip test: build events with host API, parse with plugin-side WidgetEvent
add_executable(widget_event_builder_test tests/widget_event_builder_test.cpp)
target_link_libraries(widget_event_builder_test PRIVATE pj_dialog_host pj_dialog_sdk GTest::gtest_main)
add_test(NAME widget_event_builder_test COMMAND widget_event_builder_test)

# DialogLibrary test (loads mock_dialog_plugin via dlopen)
add_executable(dialog_library_test tests/dialog_library_test.cpp)
target_compile_definitions(dialog_library_test PRIVATE
  PJ_MOCK_DIALOG_PLUGIN_PATH="$<TARGET_FILE:mock_dialog_plugin>"
)
target_link_libraries(dialog_library_test PRIVATE
  pj_dialog_library pj_dialog_host pj_dialog_sdk GTest::gtest_main
)
add_test(NAME dialog_library_test COMMAND dialog_library_test)

foreach(_test ${PJ_PLUGIN_TESTS})
  target_compile_options(${_test} PRIVATE ${PJ_WARNING_FLAGS})
endforeach()

# --- Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt) ---

if(PJ_INSTALL_SDK)
  install(TARGETS pj_dialog_protocol pj_dialog_sdk EXPORT PlotJugglerSDKTargets)
  install(FILES include/pj_plugins/dialog_protocol.h
    DESTINATION include/pj_plugins
  )
  install(DIRECTORY include/pj_plugins/sdk/
    DESTINATION include/pj_plugins/sdk
  )
endif()

# --- Qt Dialog Engine (optional — requires Qt6) ---

if(PJ_BUILD_DIALOG_ENGINE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets UiTools)
  qt_standard_project_setup()

  # Workaround: Qt 6.5 links AGL framework which was removed in macOS 15+ SDK.
  foreach(_target WrapOpenGL::WrapOpenGL OpenGL::GL)
    if(TARGET ${_target})
      get_target_property(_libs ${_target} INTERFACE_LINK_LIBRARIES)
      if(_libs)
        string(REPLACE "-framework AGL" "" _libs "${_libs}")
        list(REMOVE_ITEM _libs "AGL")
        foreach(_l ${_libs})
          if(_l MATCHES "AGL")
            list(REMOVE_ITEM _libs "${_l}")
          endif()
        endforeach()
        set_target_properties(${_target} PROPERTIES INTERFACE_LINK_LIBRARIES "${_libs}")
      endif()
    endif()
  endforeach()

  add_library(pj_dialog_engine_qt STATIC
    src/widget_binding.cpp
    src/dialog_engine.cpp
  )
  target_compile_options(pj_dialog_engine_qt PRIVATE ${PJ_WARNING_FLAGS})
  target_link_libraries(pj_dialog_engine_qt
    PUBLIC pj_dialog_host Qt6::Widgets Qt6::UiTools
  )
  target_include_directories(pj_dialog_engine_qt PUBLIC include)

  # Qt dialog engine tests (uses custom main with QApplication)
  add_executable(dialog_engine_test
    tests/dialog_engine_test.cpp
    examples/mock_dialog.cpp
  )
  target_link_libraries(dialog_engine_test
    PRIVATE pj_dialog_engine_qt pj_dialog_sdk GTest::gtest
  )
  add_test(NAME dialog_engine_test COMMAND dialog_engine_test)
endif()
