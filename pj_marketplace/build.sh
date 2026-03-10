#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

MODE=""

for arg in "$@"; do
  case "$arg" in
    --debug)    MODE="debug" ;;
    *)          echo "Usage: ./build.sh [--debug]"
                echo "  (default)  RelWithDebInfo build (build/)"
                echo "  --debug    Debug build with ASAN (build/debug_asan)"
                exit 1 ;;
  esac
done

# ---------------------------------------------------------------------------
# ccache (use if available)
# ---------------------------------------------------------------------------

CMAKE_CCACHE_ARGS=()
if command -v ccache &>/dev/null; then
  CMAKE_CCACHE_ARGS+=("-DCMAKE_C_COMPILER_LAUNCHER=ccache" "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
  echo "--- Using ccache ---"
fi

# ---------------------------------------------------------------------------
# Build helper
# ---------------------------------------------------------------------------

build_config() {
  local build_dir="$1"
  local build_type="$2"
  local sanitizer="${3:-}"
  shift 2
  [[ -n "$sanitizer" ]] && shift
  local extra_args=("$@")

  echo ""
  echo "=== Building: ${build_dir} (${build_type}) ==="
  echo ""

  local conan_extra=()
  if [[ "$sanitizer" == "asan" ]]; then
    conan_extra+=(
      "-c" "tools.build:cxxflags=['-fsanitize=address', '-fno-omit-frame-pointer']"
      "-c" "tools.build:cflags=['-fsanitize=address', '-fno-omit-frame-pointer']"
      "-c" "tools.build:sharedlinkflags=['-fsanitize=address']"
      "-c" "tools.build:exelinkflags=['-fsanitize=address']"
    )
  fi

  conan install "$SCRIPT_DIR" --output-folder="$build_dir" --build=missing \
    -s build_type="$build_type" -s compiler.cppstd=20 \
    "${conan_extra[@]+"${conan_extra[@]}"}"

  cmake -S "$SCRIPT_DIR" -B "$build_dir" \
    -DCMAKE_TOOLCHAIN_FILE="$build_dir/conan_toolchain.cmake" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    "${CMAKE_CCACHE_ARGS[@]+"${CMAKE_CCACHE_ARGS[@]}"}" \
    "${extra_args[@]+"${extra_args[@]}"}"

  cmake --build "$build_dir" -j "$(nproc)"
}

# ---------------------------------------------------------------------------
# Execute builds
# ---------------------------------------------------------------------------

case "${MODE}" in
  debug)
    build_config "${SCRIPT_DIR}/build/debug_asan" Debug asan \
      -DPJ_ENABLE_SANITIZERS=ON
    ;;
  *)
    build_config "${SCRIPT_DIR}/build" RelWithDebInfo
    ;;
esac
