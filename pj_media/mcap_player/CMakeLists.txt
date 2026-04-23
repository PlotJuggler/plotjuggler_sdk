cmake_minimum_required(VERSION 3.20)
project(mcap_player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets ShaderTools)
find_package(mcap REQUIRED)
find_package(zstd REQUIRED)
find_package(lz4 REQUIRED)
find_package(libjpeg-turbo REQUIRED)


add_executable(mcap_player main.cpp video_widget.hpp)

target_include_directories(mcap_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

set_target_properties(mcap_player PROPERTIES
  INSTALL_RPATH_USE_LINK_PATH TRUE
  BUILD_RPATH "${Qt6_DIR}/../../../lib"
)

target_link_libraries(mcap_player PRIVATE
  Qt6::Widgets
  Qt6::GuiPrivate
  mcap::mcap
  zstd::libzstd_static
  LZ4::lz4_static
  libjpeg-turbo::libjpeg-turbo
)

qt_add_shaders(mcap_player "shaders"
  PREFIX "/"
  FILES
    shaders/texture.vert
    shaders/texture.frag
)
