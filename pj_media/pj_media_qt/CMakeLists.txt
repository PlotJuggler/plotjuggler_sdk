# ---------------------------------------------------------------------------
# pj_media_qt — Qt widget layer for media display (requires Qt 6.8+)
# ---------------------------------------------------------------------------

find_package(Qt6 6.8 QUIET COMPONENTS Widgets OpenGLWidgets)
if(NOT Qt6_FOUND)
  message(STATUS "pj_media_qt: Qt 6.8+ not found, skipping")
  return()
endif()
message(STATUS "pj_media_qt: using Qt ${Qt6_VERSION}")

find_package(PkgConfig QUIET)
set(PJ_HAS_MPV OFF)
if(PkgConfig_FOUND)
  pkg_check_modules(MPV IMPORTED_TARGET mpv)
  if(MPV_FOUND)
    set(PJ_HAS_MPV ON)
    message(STATUS "pj_media_qt: libmpv found")
  endif()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(PJ_MEDIA_QT_SOURCES
  src/media_viewer_widget.cpp
  include/pj_media_qt/media_viewer_widget.h
  shaders/shaders.qrc
)

if(PJ_HAS_MPV)
  list(APPEND PJ_MEDIA_QT_SOURCES
    src/mpv_backend.cpp
    src/video_viewer_widget.cpp
    include/pj_media_qt/mpv_backend.h
    include/pj_media_qt/video_viewer_widget.h
  )
endif()

add_library(pj_media_qt STATIC ${PJ_MEDIA_QT_SOURCES})
target_include_directories(pj_media_qt PUBLIC include)
target_compile_features(pj_media_qt PUBLIC cxx_std_20)
target_compile_options(pj_media_qt PRIVATE ${PJ_WARNING_FLAGS} ${PJ_SANITIZER_FLAGS})
target_link_libraries(pj_media_qt
  PUBLIC
    pj_media_core
    Qt6::Widgets
    Qt6::Gui
)

if(PJ_HAS_MPV)
  target_link_libraries(pj_media_qt PRIVATE PkgConfig::MPV)
  target_compile_definitions(pj_media_qt PUBLIC PJ_HAS_MPV)
  target_link_libraries(pj_media_qt PUBLIC Qt6::OpenGLWidgets)
endif()

# QRhiWidget requires private Gui headers (rhi/qrhi.h)
get_target_property(_qt6_gui_dir Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
foreach(_dir ${_qt6_gui_dir})
  if(EXISTS "${_dir}/${Qt6_VERSION}/QtGui")
    target_include_directories(pj_media_qt PUBLIC "${_dir}/${Qt6_VERSION}/QtGui")
    break()
  endif()
endforeach()
