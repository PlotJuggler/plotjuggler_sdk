# ---------------------------------------------------------------------------
# pj_scene_protocol — schema + canonical wire codec (writer + reader) for
# foxglove.ImageAnnotations and forthcoming scene primitive types. SDK
# boundary for plugin authors that produce or consume markers / scene data.
# Depends on pj_base only.
# ---------------------------------------------------------------------------

add_library(pj_scene_protocol STATIC
  src/image_annotation_codec.cpp
  src/scene_decoder.cpp
  src/scene_decoder_protobuf.cpp
)
target_include_directories(pj_scene_protocol PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_options(pj_scene_protocol PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(pj_scene_protocol PUBLIC pj_base)
set_target_properties(pj_scene_protocol PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

# ---------------------------------------------------------------------------
# Install (guarded by PJ_INSTALL_SDK in root CMakeLists.txt)
# ---------------------------------------------------------------------------

if(PJ_INSTALL_SDK)
  install(TARGETS pj_scene_protocol EXPORT PlotJugglerSDKTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
  )
  install(DIRECTORY include/ DESTINATION include)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  set(PJ_SCENE_PROTOCOL_TESTS
    tests/image_annotation_codec_test.cpp
    tests/scene_decoder_test.cpp
  )

  foreach(test_src ${PJ_SCENE_PROTOCOL_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE pj_scene_protocol GTest::gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
