cmake_minimum_required(VERSION 3.22)
project(plotjuggler_sdk LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Make project cmake/ helpers discoverable to sub-trees and plugins.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(GNUInstallDirs)  # CMAKE_INSTALL_LIBDIR, etc. used by PjPluginManifest
include(PjPluginManifest)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(PJ_ASSERT_THROWS "Use exceptions instead of assert() for PJ_ASSERT" OFF)
option(PJ_ENABLE_SANITIZERS "Enable ASAN for Debug builds" OFF)
option(PJ_ENABLE_TSAN "Enable ThreadSanitizer for Debug builds" OFF)
option(PJ_INSTALL_SDK "Install plotjuggler_sdk CMake package (base/plugin_sdk/plugin_host)" OFF)
option(PJ_BUILD_PORTED_PLUGINS "Build pj_ported_plugins (ported plugins collection)" ON)
option(PJ_BUILD_TESTS "Build tests, benchmarks, and examples" ON)
option(PJ_ENABLE_ABI_CHECK "Enable abidiff-based ABI drift gate (requires libabigail)" OFF)

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------

if(MSVC)
  # /Zc:preprocessor: conformant preprocessor — required so __VA_ARGS__ inside
  # nested macro calls (e.g. PJ_DIALOG_PLUGIN's overload-by-arg-count idiom)
  # splits on commas instead of being passed as a single token.
  set(PJ_WARNING_FLAGS /W4 /WX /permissive- /Zc:preprocessor)
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

find_package(fmt REQUIRED)

if(TARGET fmt::fmt-header-only)
  set(PJ_FMT_TARGET fmt::fmt-header-only)
elseif(TARGET fmt::fmt)
  set(PJ_FMT_TARGET fmt::fmt)
else()
  message(FATAL_ERROR "fmt is required, but no fmt::fmt or fmt::fmt-header-only target was found.")
endif()

add_library(pj_internal_fmt INTERFACE)
target_compile_definitions(pj_internal_fmt INTERFACE FMT_HEADER_ONLY=1)
target_link_libraries(pj_internal_fmt INTERFACE ${PJ_FMT_TARGET})

# fast_float backs the floating-point branch of PJ::parseNumber. Header-only;
# linked PRIVATE under a BUILD_INTERFACE scope by pj_base so it never appears
# in the installed plotjuggler_sdkTargets file. Conan ships the CMake
# package under the capitalised name `FastFloat`.
find_package(FastFloat REQUIRED)

# ---------------------------------------------------------------------------
# Modules
# ---------------------------------------------------------------------------

if(PJ_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(pj_base)
add_subdirectory(pj_plugins)

if(PJ_BUILD_PORTED_PLUGINS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pj_ported_plugins/CMakeLists.txt")
  set(PJ_HAS_PORTED_PLUGINS TRUE)
  add_subdirectory(pj_ported_plugins)
endif()

# ---------------------------------------------------------------------------
# plotjuggler_sdk package install
#
# Exported CMake namespace: plotjuggler_sdk::
# Components:
#   base         — vocabulary types (always available)
#   plugin_sdk   — plugin-author surface: base + dialog SDK + parser SDK
#   plugin_host  — host-side loaders (data_source, message_parser, toolbox,
#                  dialog, catalogs)
# ---------------------------------------------------------------------------

if(PJ_INSTALL_SDK)
  include(CMakePackageConfigHelpers)

  set(PJ_PACKAGE_VERSION "0.8.1")
  set(PJ_PACKAGE_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/plotjuggler_sdk)

  install(EXPORT plotjuggler_sdkTargets
    NAMESPACE plotjuggler_sdk::
    DESTINATION ${PJ_PACKAGE_CMAKE_DIR}
  )

  configure_package_config_file(
    cmake/plotjuggler_sdkConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/plotjuggler_sdkConfig.cmake"
    INSTALL_DESTINATION ${PJ_PACKAGE_CMAKE_DIR}
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/plotjuggler_sdkConfigVersion.cmake"
    VERSION ${PJ_PACKAGE_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/plotjuggler_sdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/plotjuggler_sdkConfigVersion.cmake"
    cmake/PjPluginManifest.cmake
    DESTINATION ${PJ_PACKAGE_CMAKE_DIR}
  )
endif()

# ---------------------------------------------------------------------------
# ABI drift gate (opt-in via -DPJ_ENABLE_ABI_CHECK=ON). Requires libabigail.
# Included last so the canary target (mock_data_source_plugin) already exists.
# ---------------------------------------------------------------------------
include(PjAbiCheck)
