find_package(mcap REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(TURBOJPEG IMPORTED_TARGET libturbojpeg)
endif()

# --- CLI tool: extract a frame to PPM ---
add_executable(extract_frame extract_frame.cpp)
target_compile_features(extract_frame PRIVATE cxx_std_20)
target_compile_options(extract_frame PRIVATE ${PJ_WARNING_FLAGS})
target_link_libraries(extract_frame PRIVATE
  pj_media_core pj_datastore mcap::mcap PkgConfig::TURBOJPEG
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
    pj_media_qt pj_media_core pj_datastore mcap::mcap PkgConfig::TURBOJPEG
  )
elseif(PJ_BUILD_DIALOG_ENGINE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  set(CMAKE_AUTOMOC ON)
  add_executable(mcap_image_viewer mcap_image_viewer.cpp image_widget.hpp)
  target_compile_features(mcap_image_viewer PRIVATE cxx_std_20)
  target_compile_options(mcap_image_viewer PRIVATE ${PJ_WARNING_FLAGS})
  target_link_libraries(mcap_image_viewer PRIVATE
    pj_media_core pj_datastore mcap::mcap PkgConfig::TURBOJPEG
    Qt6::Widgets
  )
endif()
