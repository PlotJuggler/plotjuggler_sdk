find_package(mcap REQUIRED)
find_package(libjpeg-turbo REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBAVCODEC IMPORTED_TARGET libavcodec)
  pkg_check_modules(LIBAVFORMAT IMPORTED_TARGET libavformat)
  pkg_check_modules(LIBAVUTIL IMPORTED_TARGET libavutil)
endif()

# --- CLI tool: extract a frame to PPM ---
add_executable(extract_frame extract_frame.cpp)
target_compile_features(extract_frame PRIVATE cxx_std_20)
target_compile_options(extract_frame PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(extract_frame PRIVATE
  pj_media_core pj_datastore mcap::mcap libjpeg-turbo::libjpeg-turbo
)

# --- Qt demo: MCAP image viewer with ObjectStore pipeline ---
# Uses QRhiWidget (pj_media_qt) if Qt 6.8+ is available, else QLabel fallback
if(TARGET pj_media_qt)
  set(CMAKE_AUTOMOC ON)
  add_executable(mcap_image_viewer mcap_image_viewer.cpp)
  target_compile_features(mcap_image_viewer PRIVATE cxx_std_20)
  target_compile_options(mcap_image_viewer PRIVATE ${PJ_WARNING_FLAGS})
  target_compile_definitions(mcap_image_viewer PRIVATE PJ_HAS_RHI_WIDGET)
  target_link_libraries(mcap_image_viewer PRIVATE
    pj_media_qt pj_media_core pj_datastore mcap::mcap libjpeg-turbo::libjpeg-turbo
  )
  # --- Qt demo: multi-channel viewer (color + depth side by side) ---
  add_executable(multi_channel_viewer multi_channel_viewer.cpp)
  target_compile_features(multi_channel_viewer PRIVATE cxx_std_20)
  target_compile_options(multi_channel_viewer PRIVATE ${PJ_WARNING_FLAGS})
  target_link_libraries(multi_channel_viewer PRIVATE
    pj_media_qt pj_media_core pj_datastore mcap::mcap libjpeg-turbo::libjpeg-turbo
  )

  # --- Qt demo: simulated live stream (M13) ---
  add_executable(simulated_stream simulated_stream.cpp)
  target_compile_features(simulated_stream PRIVATE cxx_std_20)
  target_compile_options(simulated_stream PRIVATE ${PJ_WARNING_FLAGS})
  target_link_libraries(simulated_stream PRIVATE
    pj_media_qt pj_media_core pj_datastore libjpeg-turbo::libjpeg-turbo
  )

  # --- Qt demo: video stream (M18 — FFmpeg decode via ObjectStore) ---
  if(LIBAVCODEC_FOUND AND LIBAVFORMAT_FOUND)
    add_executable(video_stream_demo video_stream_demo.cpp)
    target_compile_features(video_stream_demo PRIVATE cxx_std_20)
    target_compile_options(video_stream_demo PRIVATE ${PJ_WARNING_FLAGS})
    target_link_libraries(video_stream_demo PRIVATE
      pj_media_qt pj_media_core pj_datastore
      PkgConfig::LIBAVCODEC PkgConfig::LIBAVFORMAT PkgConfig::LIBAVUTIL
    )
  endif()

  # --- Qt demo: MP4 video player (FileVideoSource) ---
  if(LIBAVCODEC_FOUND AND LIBAVFORMAT_FOUND)
    add_executable(mp4_video_viewer mp4_video_viewer.cpp)
    target_compile_features(mp4_video_viewer PRIVATE cxx_std_20)
    target_compile_options(mp4_video_viewer PRIVATE ${PJ_WARNING_FLAGS})
    target_link_libraries(mp4_video_viewer PRIVATE pj_media_qt pj_media_core)
  endif()
endif()
