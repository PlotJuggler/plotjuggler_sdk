cmake_minimum_required(VERSION 3.22)
project(test_package LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(plotjuggler_sdk REQUIRED CONFIG)

add_executable(test_package test_package.cpp)
# plugin_sdk transitively pulls in the `base` component (libpj_base.a) and
# nlohmann_json, exercising real linkage — not just header availability.
target_link_libraries(test_package PRIVATE plotjuggler_sdk::plugin_sdk)
