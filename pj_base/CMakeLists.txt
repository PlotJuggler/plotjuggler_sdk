# ---------------------------------------------------------------------------
# pj_base — SDK vocabulary types and ABI surface
# ---------------------------------------------------------------------------

set(PJ_BASE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
  cmake/version.hpp.in
  "${PJ_BASE_GENERATED_INCLUDE_DIR}/pj_base/sdk/version.hpp"
  @ONLY
)

add_library(pj_base STATIC
  src/builtin/builtin_object_codec.cpp
  src/builtin/camera_info_codec.cpp
  src/builtin/compressed_point_cloud_codec.cpp
  src/builtin/depth_image_codec.cpp
  src/builtin/frame_transforms_codec.cpp
  src/builtin/image_annotations_codec.cpp
  src/builtin/image_codec.cpp
  src/builtin/log_codec.cpp
  src/builtin/mesh3d_codec.cpp
  src/builtin/occupancy_grid_codec.cpp
  src/builtin/occupancy_grid_update_codec.cpp
  src/builtin/plot_markers_codec.cpp
  src/builtin/point_cloud_codec.cpp
  src/builtin/poses_in_frame_codec.cpp
  src/builtin/robot_description_codec.cpp
  src/builtin/scene_entities_codec.cpp
  src/builtin/video_frame_codec.cpp
  src/builtin/voxel_grid_codec.cpp
  src/builtin/grid_map_codec.cpp
  src/number_parse.cpp
  src/parser_module_abi.cpp
  src/parser_module_manifest.cpp
  src/parser_module_wasm.cpp
  src/semver.cpp
  src/type_tree.cpp
  src/data_source_host_views.cpp
  src/data_source_patterns.cpp
  src/data_source_plugin_base.cpp
  src/data_source_trampolines.cpp
  src/toolbox_plugin_base.cpp
  src/toolbox_trampolines.cpp
)
target_include_directories(pj_base PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PJ_BASE_GENERATED_INCLUDE_DIR}>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_base PUBLIC cxx_std_20)
target_compile_options(pj_base PRIVATE
  ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
)
# fast_float is an implementation detail of PJ::parseNumber. BUILD_INTERFACE
# scoping keeps it off the installed plotjuggler_sdkTargets file so
# downstream consumers never inherit a fast_float include path or link req.
target_link_libraries(pj_base PRIVATE
  $<BUILD_INTERFACE:FastFloat::fast_float>
)
set_target_properties(pj_base PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME base
)
# Build-tree alias so subdirectory-mode consumers can use the same namespaced
# target name that the install/export tree exposes (and that the Conan package
# emits). Keeps add_subdirectory() and find_package(plotjuggler_sdk) consumers
# writing identical link declarations.
add_library(plotjuggler_sdk::base ALIAS pj_base)
if(PJ_ASSERT_THROWS)
  target_compile_definitions(pj_base PUBLIC PJ_ASSERT_THROWS)
endif()

# Installed build-time frontend for embedding exact manifest bytes and running
# the same static wasm audit used by the SDK conformance gate.
add_executable(pj_wasm_embed_manifest tools/pj_wasm_embed_manifest.cpp)
target_compile_features(pj_wasm_embed_manifest PRIVATE cxx_std_20)
target_compile_options(pj_wasm_embed_manifest PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(pj_wasm_embed_manifest PRIVATE pj_base)
set_target_properties(pj_wasm_embed_manifest PROPERTIES
  OUTPUT_NAME pj-wasm-embed-manifest
  EXPORT_NAME wasm_embed_manifest
)
add_executable(plotjuggler_sdk::wasm_embed_manifest ALIAS pj_wasm_embed_manifest)

# Standalone, header-only functional parser-module authoring kit. This target
# intentionally carries include paths and a C++ floor only: modules link no SDK
# library and remain suitable for native and WASI reactor builds.
add_library(pj_parser_module INTERFACE)
target_include_directories(pj_parser_module INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(pj_parser_module INTERFACE cxx_std_17)
set_target_properties(pj_parser_module PROPERTIES EXPORT_NAME parser_module)
add_library(plotjuggler_sdk::parser_module ALIAS pj_parser_module)

# ---------------------------------------------------------------------------
# descriptor_import_support — the callee side of pj.descriptor_import.v1 for
# provider plugins: origin policy, canonical descriptors, the request-
# addressed artifact cache, and the provider job runner. Compiled (file
# locks, fsync, threads) and therefore its own component: plugins that do not
# provide descriptor import never link it.
# ---------------------------------------------------------------------------
find_package(nlohmann_json REQUIRED)
add_library(pj_descriptor_import_support STATIC
  src/descriptor_import/source_descriptor.cpp
  src/descriptor_import/file_lock.cpp
  src/descriptor_import/fs_durability.cpp
  src/descriptor_import/origin.cpp
  src/descriptor_import/provider_job.cpp
  src/descriptor_import/request_cache.cpp
  src/descriptor_import/sha256.cpp
)
target_include_directories(pj_descriptor_import_support
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(pj_descriptor_import_support PUBLIC cxx_std_20)
target_compile_options(pj_descriptor_import_support PRIVATE
  ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS}
)
target_link_libraries(pj_descriptor_import_support PUBLIC
  pj_base
  nlohmann_json::nlohmann_json
)
set_target_properties(pj_descriptor_import_support PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  EXPORT_NAME descriptor_import_support
)
add_library(plotjuggler_sdk::descriptor_import_support ALIAS pj_descriptor_import_support)

# ---------------------------------------------------------------------------
# Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt)
# ---------------------------------------------------------------------------

if(PJ_INSTALL_SDK)
  install(TARGETS pj_base pj_parser_module pj_wasm_embed_manifest pj_descriptor_import_support EXPORT plotjuggler_sdkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES
    "${PJ_BASE_GENERATED_INCLUDE_DIR}/pj_base/sdk/version.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pj_base/sdk"
  )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  set(PJ_BASE_TESTS
    tests/types_test.cpp
    tests/type_tree_test.cpp
    tests/span_test.cpp
    tests/expected_test.cpp
    tests/version_test.cpp
    tests/number_parse_test.cpp
    tests/semver_test.cpp
    tests/plugin_data_api_test.cpp
    tests/parser_module_abi_test.cpp
    tests/parser_module_manifest_test.cpp
    tests/parser_module_wasm_test.cpp
    tests/data_processors_api_test.cpp
    tests/settings_store_host_test.cpp
    tests/parser_runtime_host_test.cpp
    tests/data_source_protocol_test.cpp
    # TODO: data_source_plugin_base_test.cpp and
    # message_parser_plugin_base_test.cpp exercised old bind_write_host /
    # bind_runtime_host slots that were already removed by the time the
    # service-registry API landed. Coverage for those paths is provided
    # today by the *_library_test.cpp integration tests. The old unit
    # tests can either be rewritten to target the v4 service-registry
    # flow or deleted outright.
    tests/abi_layout_sentinels_test.cpp
    tests/platform_test.cpp
    tests/arrow_holders_test.cpp
    tests/builtin_object_test.cpp
    tests/builtin_object_codec_test.cpp
    tests/frame_transforms_codec_test.cpp
    tests/image_annotations_codec_test.cpp
    tests/image_annotations_decoder_test.cpp
    tests/media_metadata_test.cpp
    tests/object_topic_metadata_test.cpp
    tests/push_message_test.cpp
    tests/attach_source_record_test.cpp
    tests/notify_available_topics_test.cpp
    tests/dataset_ingest_view_test.cpp
    tests/descriptor_import_extension_test.cpp
    tests/source_promotion_service_test.cpp
    tests/image_codec_test.cpp
    tests/depth_image_codec_test.cpp
    tests/point_cloud_codec_test.cpp
    tests/compressed_point_cloud_codec_test.cpp
    tests/occupancy_grid_codec_test.cpp
    tests/occupancy_grid_update_codec_test.cpp
    tests/camera_info_codec_test.cpp
    tests/log_codec_test.cpp
    tests/mesh3d_codec_test.cpp
    tests/video_frame_codec_test.cpp
    tests/scene_entities_codec_test.cpp
    tests/time_spine_test.cpp
    tests/poses_in_frame_codec_test.cpp
    tests/voxel_grid_codec_test.cpp
    tests/grid_map_codec_test.cpp
    tests/plot_markers_codec_test.cpp
  )

  foreach(test_src ${PJ_BASE_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE pj_base GTest::gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  set(PJ_DESCRIPTOR_IMPORT_TESTS
    tests/descriptor_import_origin_test.cpp
    tests/descriptor_import_source_descriptor_test.cpp
    tests/descriptor_import_request_cache_test.cpp
    tests/descriptor_import_provider_job_test.cpp
  )
  foreach(test_src ${PJ_DESCRIPTOR_IMPORT_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_compile_options(${test_name} PRIVATE ${PJ_WARNING_FLAGS})
    target_link_libraries(${test_name} PRIVATE pj_descriptor_import_support GTest::gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  # C++17 standalone authoring-kit gate. This target intentionally does not
  # link pj_base; GTest is test infrastructure only.
  add_executable(parser_module_kit_test tests/parser_module_kit_test.cpp)
  set_target_properties(parser_module_kit_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  target_compile_options(parser_module_kit_test PRIVATE ${PJ_WARNING_FLAGS})
  target_link_libraries(parser_module_kit_test PRIVATE pj_parser_module GTest::gtest_main)
  add_test(NAME parser_module_kit_test COMMAND parser_module_kit_test)

  add_executable(parser_module_object_writer_test tests/parser_module_object_writer_test.cpp)
  target_compile_definitions(parser_module_object_writer_test PRIVATE PJ_PARSER_MODULE_CLAIM_COUNT=1)
  target_compile_options(parser_module_object_writer_test PRIVATE ${PJ_WARNING_FLAGS})
  target_link_libraries(parser_module_object_writer_test PRIVATE pj_parser_module pj_base GTest::gtest_main)
  add_test(NAME parser_module_object_writer_test COMMAND parser_module_object_writer_test)

  # Static wasm ABI conformance. The production authoring helper compiles the
  # fixture through wasi-sdk, embeds its manifest, and audits the result.
  set(_pj_build_toy_wasm OFF)
  set(PJ_WASI_SDK_ROOT "$ENV{PJ_WASI_SDK_ROOT}" CACHE PATH
    "wasi-sdk 27 root used for parser-module conformance tests")
  set(_pj_wasi_clang "${PJ_WASI_SDK_ROOT}/bin/clang++")
  set(_pj_wasi_sysroot "${PJ_WASI_SDK_ROOT}/share/wasi-sysroot")
  if(NOT PJ_WASI_SDK_ROOT)
    message(STATUS
      "Parser-module WASI conformance skipped: PJ_WASI_SDK_ROOT cache/env variable is not set")
  elseif(NOT EXISTS "${_pj_wasi_clang}" OR NOT IS_DIRECTORY "${_pj_wasi_sysroot}")
    message(STATUS
      "Parser-module WASI conformance skipped: wasi-sdk is missing under PJ_WASI_SDK_ROOT=${PJ_WASI_SDK_ROOT}")
  else()
    file(STRINGS "${PJ_WASI_SDK_ROOT}/VERSION" _pj_wasi_version LIMIT_COUNT 1)
    if(NOT _pj_wasi_version MATCHES "^27\\.")
      message(FATAL_ERROR
        "Parser-module WASI conformance requires wasi-sdk 27; found '${_pj_wasi_version}'")
    endif()
    message(STATUS
      "Parser-module WASI conformance enabled with wasi-sdk ${_pj_wasi_version}: ${PJ_WASI_SDK_ROOT}")

    set(_pj_build_toy_wasm ON)
  endif()

  set(_pj_toy_targets native)
  if(_pj_build_toy_wasm)
    list(APPEND _pj_toy_targets wasm)
  endif()
  pj_add_parser_module(toy_cdr_pointcloud_module
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud_module.cpp"
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud.module.json"
    TARGETS ${_pj_toy_targets}
  )
  if(_pj_build_toy_wasm)
    get_property(_pj_wasm_embedded TARGET toy_cdr_pointcloud_module_wasm
      PROPERTY PJ_PARSER_MODULE_WASM_PATH)
    set(_pj_wasm_manifest "${CMAKE_CURRENT_SOURCE_DIR}/tests/toy_cdr_pointcloud.module.json")
    add_custom_target(parser_module_wasm_conformance_fixture ALL
      DEPENDS toy_cdr_pointcloud_module_wasm)
    set_property(TARGET parser_module_wasm_conformance_fixture PROPERTY
      PJ_PARSER_MODULE_WASM_FIXTURE_PATH "${_pj_wasm_embedded}")
    add_test(NAME parser_module_wasm_conformance_test
      COMMAND pj_wasm_embed_manifest verify "${_pj_wasm_embedded}" "${_pj_wasm_manifest}")
    set_tests_properties(parser_module_wasm_conformance_test PROPERTIES
      LABELS "parser_module;wasi"
      REQUIRED_FILES "${_pj_wasm_embedded}")
  endif()
endif()
