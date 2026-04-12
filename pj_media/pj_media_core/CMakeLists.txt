# ---------------------------------------------------------------------------
# pj_media_core — media decode/playback core, no Qt dependency
# ---------------------------------------------------------------------------

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(TURBOJPEG IMPORTED_TARGET libturbojpeg)
  pkg_check_modules(LIBPNG IMPORTED_TARGET libpng)
endif()

add_library(pj_media_core STATIC
  src/image_decoder.cpp
  src/codec_pipeline.cpp
  src/codecs.cpp
)
target_include_directories(pj_media_core PUBLIC include)
target_compile_features(pj_media_core PUBLIC cxx_std_20)
target_compile_options(pj_media_core PRIVATE
  ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
)
target_link_libraries(pj_media_core
  PUBLIC pj_base pj_datastore
  PRIVATE PkgConfig::TURBOJPEG PkgConfig::LIBPNG
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  find_package(mcap REQUIRED)
  find_package(nlohmann_json REQUIRED)

  set(PJ_MEDIA_CORE_TESTS
    tests/frame_slot_test.cpp
    tests/cancel_token_test.cpp
    tests/image_decoder_test.cpp
  )

  foreach(test_src ${PJ_MEDIA_CORE_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE pj_media_core PkgConfig::TURBOJPEG PkgConfig::LIBPNG GTest::gtest_main)
    target_compile_options(${test_name} PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  # MCAP integration test (needs mcap + nlohmann_json + ObjectStore)
  add_executable(mcap_integration_test tests/mcap_integration_test.cpp)
  target_link_libraries(mcap_integration_test PRIVATE
    pj_media_core pj_datastore mcap::mcap nlohmann_json::nlohmann_json
    PkgConfig::TURBOJPEG GTest::gtest_main
  )
  target_compile_options(mcap_integration_test PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
  add_test(NAME mcap_integration_test COMMAND mcap_integration_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # Streaming test: push + retention + concurrent read (M13)
  add_executable(streaming_test tests/streaming_test.cpp)
  target_link_libraries(streaming_test PRIVATE pj_media_core pj_datastore GTest::gtest_main)
  target_compile_options(streaming_test PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
  add_test(NAME streaming_test COMMAND streaming_test)

  # Dual-store test: ObjectStore + DataEngine from same MCAP (M11)
  add_executable(dual_store_test tests/dual_store_test.cpp)
  target_link_libraries(dual_store_test PRIVATE
    pj_media_core pj_datastore mcap::mcap GTest::gtest_main
  )
  target_compile_options(dual_store_test PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
  add_test(NAME dual_store_test COMMAND dual_store_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()
