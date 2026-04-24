cmake_minimum_required(VERSION 3.22)
project(plotjuggler_core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(PJ_ASSERT_THROWS "Use exceptions instead of assert() for PJ_ASSERT" OFF)
option(PJ_ENABLE_SANITIZERS "Enable ASAN for Debug builds" OFF)
option(PJ_ENABLE_TSAN "Enable ThreadSanitizer for Debug builds" OFF)
option(PJ_INSTALL_SDK "Install PlotJugglerSDK package (DataSource + Dialog)" OFF)
option(
  PJ_BUILD_PARQUET_IMPORT_EXAMPLE
  "Build parquet_import example (requires full Arrow C++ and Parquet)"
  ON
)
# Auto-detect Qt6: enable dialog engine if Qt6 is findable
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
  option(PJ_BUILD_DIALOG_ENGINE_QT "Build Qt dialog engine (requires Qt6)" ON)
else()
  option(PJ_BUILD_DIALOG_ENGINE_QT "Build Qt dialog engine (requires Qt6)" OFF)
endif()

option(PJ_BUILD_DATASTORE "Build pj_datastore module (requires nanoarrow)" ON)
option(PJ_BUILD_PORTED_PLUGINS "Build pj_ported_plugins (ported plugins collection)" ON)
option(PJ_BUILD_TESTS "Build tests, benchmarks, and examples" ON)

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------

if(MSVC)
  set(PJ_WARNING_FLAGS /W4 /WX /permissive-)
else()
  set(PJ_WARNING_FLAGS
    -Wall -Wextra -Werror -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Wcast-qual -Wconversion -Woverloaded-virtual -Wpedantic
  )
endif()

# ---------------------------------------------------------------------------
# Sanitizers (opt-in, Debug only)
# ---------------------------------------------------------------------------

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(PJ_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    add_compile_definitions(PJ_ASAN_ACTIVE)
  endif()
  if(PJ_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
  endif()
endif()

# ---------------------------------------------------------------------------
# External dependencies
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  find_package(GTest REQUIRED)
endif()

if(PJ_BUILD_DATASTORE)
  find_package(fmt REQUIRED)
  find_package(tsl-robin-map REQUIRED)
  if(PJ_BUILD_TESTS)
    find_package(benchmark CONFIG REQUIRED)
  endif()

# --- nanoarrow core ---

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
    "nanoarrow is required for pj_datastore. "
    "Install nanoarrow and expose a CMake target "
    "(nanoarrow::nanoarrow / nanoarrow::nanoarrow_static / nanoarrow::nanoarrow_shared) "
    "or provide a pkg-config entry named 'nanoarrow'."
  )
endif()

# --- nanoarrow IPC ---

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
    "nanoarrow IPC is required for pj_datastore. "
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

endif() # PJ_BUILD_DATASTORE

# ---------------------------------------------------------------------------
# Modules
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(pj_base)
if(PJ_BUILD_DATASTORE)
  add_subdirectory(pj_datastore)
  add_subdirectory(pj_media/pj_media_core)
  add_subdirectory(pj_media/pj_media_qt)
  add_subdirectory(pj_media/demos)
endif()
add_subdirectory(pj_plugins)

if(PJ_BUILD_DIALOG_ENGINE_QT)
  add_subdirectory(pj_marketplace)
  find_package(Qt6 COMPONENTS Charts QUIET)
  if(Qt6Charts_FOUND)
    add_subdirectory(pj_proto_app)
  endif()
endif()

if(PJ_BUILD_PORTED_PLUGINS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pj_ported_plugins/CMakeLists.txt")
  add_subdirectory(pj_ported_plugins)
endif()

# ---------------------------------------------------------------------------
# PlotJugglerSDK package install
# ---------------------------------------------------------------------------

if(PJ_INSTALL_SDK)
  include(CMakePackageConfigHelpers)

  set(PJ_SDK_VERSION "1.0.0")
  set(PJ_SDK_CMAKE_DIR lib/cmake/PlotJugglerSDK)

  install(EXPORT PlotJugglerSDKTargets
    NAMESPACE PlotJugglerSDK::
    DESTINATION ${PJ_SDK_CMAKE_DIR}
  )

  configure_package_config_file(
    cmake/PlotJugglerSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/PlotJugglerSDKConfig.cmake"
    INSTALL_DESTINATION ${PJ_SDK_CMAKE_DIR}
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PlotJugglerSDKConfigVersion.cmake"
    VERSION ${PJ_SDK_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PlotJugglerSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PlotJugglerSDKConfigVersion.cmake"
    DESTINATION ${PJ_SDK_CMAKE_DIR}
  )
endif()
