cmake_minimum_required(VERSION 3.22)
project(pj_engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(absl REQUIRED)
find_package(GTest REQUIRED)
find_package(benchmark CONFIG REQUIRED)

option(PJ_ASSERT_THROWS "Use exceptions instead of assert() for PJ_ASSERT" OFF)
option(
  PJ_BUILD_PARQUET_IMPORT_EXAMPLE
  "Build parquet_import example (requires full Arrow C++ and Parquet)"
  OFF
)

# Sanitizers for Debug builds (opt-in via PJ_ENABLE_SANITIZERS)
# Note: ASAN/UBSan have compatibility issues with abseil on GCC 11.
# Use GCC 13+ or Clang for best results.
option(PJ_ENABLE_SANITIZERS "Enable ASAN and UBSan for Debug builds" OFF)
if(PJ_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(PJ_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_compile_options(${PJ_SANITIZER_FLAGS})
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# plotjuggler_base — vocabulary types, zero external dependencies
# ---------------------------------------------------------------------------

add_library(plotjuggler_base STATIC
  base/src/type_tree.cpp
)
target_include_directories(plotjuggler_base PUBLIC base/include)
target_compile_features(plotjuggler_base PUBLIC cxx_std_20)
target_compile_options(plotjuggler_base PRIVATE
  -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-qual -Wconversion -Woverloaded-virtual -Wpedantic
  ${PJ_SANITIZER_FLAGS}
)
if(PJ_ASSERT_THROWS)
  target_compile_definitions(plotjuggler_base PUBLIC PJ_ASSERT_THROWS)
endif()

# ---------------------------------------------------------------------------
# plotjuggler_engine — storage engine, depends on base + abseil
# ---------------------------------------------------------------------------

find_package(nanoarrow CONFIG QUIET)

set(PJ_NANOARROW_TARGET "")
if(TARGET nanoarrow::nanoarrow)
  set(PJ_NANOARROW_TARGET nanoarrow::nanoarrow)
elseif(TARGET nanoarrow::nanoarrow_static)
  set(PJ_NANOARROW_TARGET nanoarrow::nanoarrow_static)
elseif(TARGET nanoarrow::nanoarrow_shared)
  set(PJ_NANOARROW_TARGET nanoarrow::nanoarrow_shared)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(NANOARROW QUIET nanoarrow)
    if(NANOARROW_FOUND)
      add_library(pj_nanoarrow INTERFACE)
      target_include_directories(pj_nanoarrow INTERFACE
        ${NANOARROW_INCLUDE_DIRS}
      )
      target_link_libraries(pj_nanoarrow INTERFACE ${NANOARROW_LIBRARIES})
      set(PJ_NANOARROW_TARGET pj_nanoarrow)
    endif()
  endif()
endif()

if(PJ_NANOARROW_TARGET STREQUAL "")
  message(FATAL_ERROR
    "nanoarrow is required for plotjuggler_engine. "
    "Install nanoarrow and expose a CMake target "
    "(nanoarrow::nanoarrow / nanoarrow::nanoarrow_static / nanoarrow::nanoarrow_shared) "
    "or provide a pkg-config entry named 'nanoarrow'."
  )
endif()

# nanoarrow IPC target detection (same pattern as core nanoarrow)
set(PJ_NANOARROW_IPC_TARGET "")
if(TARGET nanoarrow::nanoarrow_ipc)
  set(PJ_NANOARROW_IPC_TARGET nanoarrow::nanoarrow_ipc)
elseif(TARGET nanoarrow::nanoarrow_ipc_static)
  set(PJ_NANOARROW_IPC_TARGET nanoarrow::nanoarrow_ipc_static)
elseif(TARGET nanoarrow::nanoarrow_ipc_shared)
  set(PJ_NANOARROW_IPC_TARGET nanoarrow::nanoarrow_ipc_shared)
else()
  if(PkgConfig_FOUND)
    pkg_check_modules(NANOARROW_IPC QUIET nanoarrow_ipc)
    if(NANOARROW_IPC_FOUND)
      add_library(pj_nanoarrow_ipc INTERFACE)
      target_include_directories(pj_nanoarrow_ipc INTERFACE
        ${NANOARROW_IPC_INCLUDE_DIRS}
      )
      target_link_libraries(pj_nanoarrow_ipc INTERFACE ${NANOARROW_IPC_LIBRARIES})
      set(PJ_NANOARROW_IPC_TARGET pj_nanoarrow_ipc)
    endif()
  endif()
endif()

if(PJ_NANOARROW_IPC_TARGET STREQUAL "")
  message(FATAL_ERROR
    "nanoarrow IPC is required for plotjuggler_engine. "
    "Install nanoarrow with IPC support and expose a CMake target "
    "(nanoarrow::nanoarrow_ipc / nanoarrow::nanoarrow_ipc_static / nanoarrow::nanoarrow_ipc_shared) "
    "or provide a pkg-config entry named 'nanoarrow_ipc'."
  )
endif()

# nanoarrow IPC internally depends on the flatcc runtime (libflatccrt).
# Some package managers (e.g. Conan) bundle it alongside the IPC library
# but don't declare it as a transitive link dependency. Since the IPC target
# already sets INTERFACE_LINK_DIRECTORIES, linking by name suffices.
target_link_libraries(${PJ_NANOARROW_IPC_TARGET} INTERFACE flatccrt)

add_library(plotjuggler_engine STATIC
  engine/src/buffer.cpp
  engine/src/column_buffer.cpp
  engine/src/encoding.cpp
  engine/src/chunk.cpp
  engine/src/topic_storage.cpp
  engine/src/type_registry.cpp
  engine/src/query.cpp
  engine/src/writer.cpp
  engine/src/reader.cpp
  engine/src/engine.cpp
  engine/src/arrow_import.cpp
  engine/src/derived_engine.cpp
  engine/src/builtin_transforms.cpp
)
target_include_directories(plotjuggler_engine PUBLIC engine/include)
target_compile_features(plotjuggler_engine PUBLIC cxx_std_20)
target_compile_options(plotjuggler_engine PRIVATE
  -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-qual -Wconversion -Woverloaded-virtual -Wpedantic
  ${PJ_SANITIZER_FLAGS}
)
if(PJ_ASSERT_THROWS)
  target_compile_definitions(plotjuggler_engine PUBLIC PJ_ASSERT_THROWS)
endif()
target_link_libraries(plotjuggler_engine
  PUBLIC
    plotjuggler_base
    absl::flat_hash_map
    absl::hash
  PRIVATE
    absl::strings
    ${PJ_NANOARROW_TARGET}
    ${PJ_NANOARROW_IPC_TARGET}
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

# Base-only tests (link plotjuggler_base only)
set(BASE_TEST_SOURCES
  tests/types_test.cpp
  tests/type_tree_test.cpp
  tests/span_test.cpp
  tests/expected_test.cpp
)

foreach(test_src ${BASE_TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE plotjuggler_base GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Engine tests (link plotjuggler_engine, which transitively provides base)
set(ENGINE_TEST_SOURCES
  tests/type_registry_test.cpp
  tests/buffer_test.cpp
  tests/column_buffer_test.cpp
  tests/encoding_test.cpp
  tests/chunk_test.cpp
  tests/topic_storage_test.cpp
  tests/query_test.cpp
  tests/engine_integration_test.cpp
  tests/derived_engine_test.cpp
  tests/array_expansion_test.cpp
)

foreach(test_src ${ENGINE_TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE plotjuggler_engine GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Arrow import test (needs nanoarrow explicitly for building test IPC data)
add_executable(arrow_import_test tests/arrow_import_test.cpp)
target_link_libraries(arrow_import_test PRIVATE
  plotjuggler_engine
  ${PJ_NANOARROW_TARGET}
  ${PJ_NANOARROW_IPC_TARGET}
  GTest::gtest_main)
add_test(NAME arrow_import_test COMMAND arrow_import_test)

# ---------------------------------------------------------------------------
# Benchmark
# ---------------------------------------------------------------------------

add_executable(read_benchmark benchmarks/read_benchmark.cpp)
target_link_libraries(read_benchmark PRIVATE plotjuggler_engine benchmark::benchmark)

add_executable(ingest_benchmark benchmarks/ingest_benchmark.cpp)
target_link_libraries(ingest_benchmark PRIVATE plotjuggler_engine benchmark::benchmark)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

if(PJ_BUILD_PARQUET_IMPORT_EXAMPLE)
  if(NOT TARGET Arrow::arrow_static)
    find_package(Arrow REQUIRED)
  endif()
  if(NOT TARGET Parquet::parquet_static)
    find_package(Parquet REQUIRED)
  endif()
  add_executable(parquet_import examples/parquet_import.cpp)
  target_link_libraries(parquet_import PRIVATE
    plotjuggler_engine Arrow::arrow_static Parquet::parquet_static
  )
endif()
