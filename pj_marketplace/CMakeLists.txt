cmake_minimum_required(VERSION 3.22)
project(pj_marketplace VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)
find_package(GTest REQUIRED)

add_executable(pj_marketplace_standalone
  src/main.cpp
)

target_include_directories(pj_marketplace_standalone PRIVATE src)
target_link_libraries(pj_marketplace_standalone PRIVATE Qt6::Widgets Qt6::Network)
target_compile_options(pj_marketplace_standalone PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------

add_executable(registry_manager_test
  tests/registry_manager_test.cpp
  src/core/RegistryManager.cpp
)

target_include_directories(registry_manager_test PRIVATE src)
target_link_libraries(registry_manager_test PRIVATE
  Qt6::Network
  Qt6::Test
  GTest::gtest
)
target_compile_options(registry_manager_test PRIVATE -Wall -Wextra)
add_test(NAME registry_manager_test COMMAND registry_manager_test)
