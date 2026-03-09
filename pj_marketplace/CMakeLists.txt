cmake_minimum_required(VERSION 3.22)
project(marketplace LANGUAGES CXX)


set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(GTest REQUIRED)


# ---------------------------------------------------------------------------
# marketplace — main executable
# ---------------------------------------------------------------------------

add_executable(marketplace
  main.cpp
)

target_link_libraries(marketplace PRIVATE
  Qt6::Widgets
  Qt6::Network
)

target_compile_options(marketplace PRIVATE ${PJ_WARNING_FLAGS})
