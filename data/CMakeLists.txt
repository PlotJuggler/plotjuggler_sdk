cmake_minimum_required(VERSION 3.24)
project(pj_engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(absl REQUIRED)
find_package(GTest REQUIRED)

add_library(pj_engine STATIC
  src/type_registry.cpp
  src/buffer.cpp
  src/column_buffer.cpp
  src/encoding.cpp
  src/chunk.cpp
  src/topic_storage.cpp
  src/query.cpp
  src/writer.cpp
  src/reader.cpp
  src/engine.cpp
)
target_include_directories(pj_engine PUBLIC include)
target_compile_features(pj_engine PUBLIC cxx_std_20)
target_compile_options(pj_engine PRIVATE
  -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-qual -Wconversion -Woverloaded-virtual -Wpedantic
)
target_link_libraries(pj_engine PUBLIC
  absl::flat_hash_map
  absl::flat_hash_set
  absl::btree
  absl::status
  absl::statusor
  absl::strings
  absl::str_format
  absl::hash
  absl::inlined_vector
  absl::span
  absl::time
)

enable_testing()

set(TEST_SOURCES
  tests/types_test.cpp
  tests/type_tree_test.cpp
  tests/type_registry_test.cpp
  tests/buffer_test.cpp
  tests/column_buffer_test.cpp
  tests/encoding_test.cpp
  tests/chunk_test.cpp
  tests/topic_storage_test.cpp
  tests/query_test.cpp
  tests/engine_integration_test.cpp
)

foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE pj_engine GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
