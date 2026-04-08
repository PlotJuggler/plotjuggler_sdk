cmake_minimum_required(VERSION 3.20)
project(mcap_player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(mcap REQUIRED)
find_package(zstd REQUIRED)
find_package(lz4 REQUIRED)
find_package(libjpeg-turbo REQUIRED)

add_executable(mcap_player main.cpp)

target_include_directories(mcap_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(mcap_player PRIVATE
  Qt6::Widgets
  mcap::mcap
  zstd::libzstd_static
  LZ4::lz4_static
  libjpeg-turbo::libjpeg-turbo
)
