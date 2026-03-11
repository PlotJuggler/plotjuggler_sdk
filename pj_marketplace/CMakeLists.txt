# ---------------------------------------------------------------------------
# Standalone build support
#
# When this file is the root of the build (standalone), we set up the project
# and find all dependencies here. When used as a subdirectory of plotjuggler_core,
# the parent already provides CXX standard, warning flags, GTest, and testing.
# ---------------------------------------------------------------------------

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.22)
  project(pj_marketplace LANGUAGES CXX)

  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  set(PJ_WARNING_FLAGS -Wall -Wextra -Werror -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast -Wcast-qual -Wconversion -Woverloaded-virtual -Wpedantic
  )

  find_package(GTest REQUIRED)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test)
find_package(LibArchive REQUIRED)

# ---------------------------------------------------------------------------
# pj_marketplace — package download, checksum verification and zip extraction
# ---------------------------------------------------------------------------

add_library(pj_marketplace STATIC
  src/core/DownloadManager.cpp
  src/core/ExtensionManager.cpp
  src/core/PlatformUtils.cpp
  src/core/RegistryManager.cpp
)

set_target_properties(pj_marketplace PROPERTIES AUTOMOC ON)

target_include_directories(pj_marketplace PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pj_marketplace PUBLIC
  Qt6::Core
  Qt6::Network
  LibArchive::LibArchive
)

# pj_base provides expected.hpp (header-only for our usage).
# When built as part of plotjuggler_core the target already exists;
# in standalone mode we expose the include directory directly.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  target_include_directories(pj_marketplace PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../pj_base/include
  )
else()
  target_link_libraries(pj_marketplace PUBLIC pj_base)
endif()

target_compile_options(pj_marketplace PRIVATE ${PJ_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# pj_marketplace — main executable
# ---------------------------------------------------------------------------

add_executable(pj_marketplace_app
  main.cpp
)

set_target_properties(pj_marketplace_app PROPERTIES
  AUTOMOC ON
  AUTORCC ON
  AUTOUIC ON
)

target_link_libraries(pj_marketplace_app PRIVATE
  pj_marketplace
  Qt6::Widgets
)

target_compile_options(pj_marketplace_app PRIVATE ${PJ_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# download_manager_test has its own main() to initialise QCoreApplication
add_executable(download_manager_test tests/download_manager_test.cpp)
target_link_libraries(download_manager_test PRIVATE pj_marketplace GTest::gtest Qt6::Test LibArchive::LibArchive)
target_compile_options(download_manager_test PRIVATE ${PJ_WARNING_FLAGS})
set_target_properties(download_manager_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
)
add_test(NAME download_manager_test COMMAND download_manager_test)

add_executable(registry_manager_test
  tests/registry_manager_test.cpp
)

target_include_directories(registry_manager_test PRIVATE src)
target_link_libraries(registry_manager_test PRIVATE
  pj_marketplace
  Qt6::Network
  Qt6::Test
  GTest::gtest
)
set_target_properties(registry_manager_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
)
target_compile_options(registry_manager_test PRIVATE -Wall -Wextra)
add_test(NAME registry_manager_test COMMAND registry_manager_test)

add_executable(extension_manager_test
  tests/extension_manager_test.cpp
)

target_include_directories(extension_manager_test PRIVATE src)
target_link_libraries(extension_manager_test PRIVATE
  pj_marketplace
  Qt6::Network
  Qt6::Test
  GTest::gtest
  LibArchive::LibArchive
)
set_target_properties(extension_manager_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
)
target_compile_options(extension_manager_test PRIVATE ${PJ_WARNING_FLAGS})
add_test(NAME extension_manager_test COMMAND extension_manager_test)

# Integration test — requires network access, excluded from CTest by default.
# Run manually: ./tests/extension_manager_integration_test
add_executable(extension_manager_check_plugin_management
  tests/extension_manager_check_plugin_management.cpp
)

target_include_directories(extension_manager_check_plugin_management PRIVATE src)
target_link_libraries(extension_manager_check_plugin_management PRIVATE
  pj_marketplace
  Qt6::Network
  Qt6::Test
  GTest::gtest
)
set_target_properties(extension_manager_check_plugin_management PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
)
target_compile_definitions(extension_manager_check_plugin_management PRIVATE
  REGISTRY_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../pj-plugin-registry/registry.json"
  RESULTS_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/results"
)
target_compile_options(extension_manager_check_plugin_management PRIVATE ${PJ_WARNING_FLAGS})
