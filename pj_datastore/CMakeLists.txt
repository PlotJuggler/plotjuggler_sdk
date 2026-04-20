# ---------------------------------------------------------------------------
# pj_datastore — storage engine, depends on pj_base + fmt + nanoarrow
# ---------------------------------------------------------------------------

add_library(pj_datastore STATIC
  src/buffer.cpp
  src/column_buffer.cpp
  src/encoding.cpp
  src/chunk.cpp
  src/topic_storage.cpp
  src/type_registry.cpp
  src/query.cpp
  src/writer.cpp
  src/reader.cpp
  src/engine.cpp
  src/arrow_import.cpp
  src/derived_engine.cpp
  src/builtin_transforms.cpp
  src/plugin_data_host.cpp
  src/colormap_registry.cpp
  src/colormap_registry_host.cpp
)
target_include_directories(pj_datastore PUBLIC include)
target_compile_features(pj_datastore PUBLIC cxx_std_20)
target_compile_options(pj_datastore PRIVATE
  ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
)
if(PJ_ASSERT_THROWS)
  target_compile_definitions(pj_datastore PUBLIC PJ_ASSERT_THROWS)
endif()
target_link_libraries(pj_datastore
  PUBLIC
    pj_base
  PRIVATE
    tsl::robin_map
    fmt::fmt
    ${PJ_NANOARROW_TARGET}
    ${PJ_NANOARROW_IPC_TARGET}
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  set(PJ_DATASTORE_TESTS
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
    tests/regression_test.cpp
    tests/plugin_host_read_test.cpp
  )

  foreach(test_src ${PJ_DATASTORE_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE pj_datastore GTest::gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  # Arrow import test (needs nanoarrow explicitly for building test IPC data)
  add_executable(arrow_import_test tests/arrow_import_test.cpp)
  target_link_libraries(arrow_import_test PRIVATE
    pj_datastore
    ${PJ_NANOARROW_TARGET}
    ${PJ_NANOARROW_IPC_TARGET}
    GTest::gtest_main)
  add_test(NAME arrow_import_test COMMAND arrow_import_test)

  # Plugin host write test (needs nanoarrow for Arrow IPC testing)
  add_executable(plugin_host_write_test tests/plugin_host_write_test.cpp)
  target_link_libraries(plugin_host_write_test PRIVATE
    pj_datastore
    ${PJ_NANOARROW_TARGET}
    ${PJ_NANOARROW_IPC_TARGET}
    GTest::gtest_main)
  add_test(NAME plugin_host_write_test COMMAND plugin_host_write_test)

  # ---------------------------------------------------------------------------
  # Benchmarks
  # ---------------------------------------------------------------------------

  add_executable(read_benchmark benchmarks/read_benchmark.cpp)
  target_link_libraries(read_benchmark PRIVATE pj_datastore benchmark::benchmark)

  add_executable(ingest_benchmark benchmarks/ingest_benchmark.cpp)
  target_link_libraries(ingest_benchmark PRIVATE pj_datastore benchmark::benchmark)
endif() # PJ_BUILD_TESTS

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
    pj_datastore Arrow::arrow_static Parquet::parquet_static
  )
endif()
