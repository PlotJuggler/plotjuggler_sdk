#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

STAGING_DIR="$(mktemp -d)"
BUILD_DIR="${SCRIPT_DIR}/build/_sdk_install_test"
CONSUMER_BUILD_DIR="${STAGING_DIR}/consumer_build"

cleanup() {
  rm -rf "$STAGING_DIR" "$BUILD_DIR" "$CONSUMER_BUILD_DIR"
}
trap cleanup EXIT

echo "=== plotjuggler_sdk install test ==="
echo "Staging dir: ${STAGING_DIR}"
echo ""

# ---------------------------------------------------------------------------
# 1. Build the project with PJ_INSTALL_SDK=ON
# ---------------------------------------------------------------------------

echo "--- Step 1: Configure + build with PJ_INSTALL_SDK=ON ---"

conan install "$SCRIPT_DIR" --output-folder="$BUILD_DIR" --build=missing \
  -s build_type=Release -s compiler.cppstd=20

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$BUILD_DIR/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPJ_INSTALL_SDK=ON \
  -DPJ_BUILD_TESTS=OFF \
  -DPJ_BUILD_PORTED_PLUGINS=OFF \
  -DPJ_BUILD_PARQUET_IMPORT_EXAMPLE=OFF \
  -DCMAKE_INSTALL_PREFIX="$STAGING_DIR"

cmake --build "$BUILD_DIR" -j "$(nproc)"

# ---------------------------------------------------------------------------
# 2. Install to staging directory
# ---------------------------------------------------------------------------

echo ""
echo "--- Step 2: Install to staging ---"

cmake --install "$BUILD_DIR"

echo ""
echo "Installed CMake package files:"
find "$STAGING_DIR" -path '*/cmake/plotjuggler_sdk*' | sort

echo ""
echo "Installed libraries:"
find "$STAGING_DIR" -name 'libpj_*' | sort

# ---------------------------------------------------------------------------
# 3. Build the out-of-tree consumer against the installed package
# ---------------------------------------------------------------------------

echo ""
echo "--- Step 3: Configure + build sdk_consumer example ---"

cmake -S "$SCRIPT_DIR/examples/sdk_consumer" -B "$CONSUMER_BUILD_DIR" \
  -DCMAKE_PREFIX_PATH="$STAGING_DIR;$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$CONSUMER_BUILD_DIR" -j "$(nproc)"

# ---------------------------------------------------------------------------
# 4. Verify each public component is independently findable
# ---------------------------------------------------------------------------

echo ""
echo "--- Step 4: Smoke-test find_package COMPONENTS ---"

for comp in base plugin_sdk plugin_host; do
  COMP_DIR="$(mktemp -d)"
  cat > "$COMP_DIR/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.22)
project(comp_smoke LANGUAGES CXX)
find_package(plotjuggler_sdk REQUIRED COMPONENTS ${comp})
if(NOT TARGET plotjuggler_sdk::${comp})
  message(FATAL_ERROR "plotjuggler_sdk::${comp} target missing")
endif()
EOF
  if cmake -S "$COMP_DIR" -B "$COMP_DIR/build" \
       -DCMAKE_PREFIX_PATH="$STAGING_DIR;$BUILD_DIR" \
       -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1; then
    echo "  OK  plotjuggler_sdk::${comp}"
  else
    echo "  FAIL plotjuggler_sdk::${comp}"
    cmake -S "$COMP_DIR" -B "$COMP_DIR/build" \
       -DCMAKE_PREFIX_PATH="$STAGING_DIR;$BUILD_DIR" \
       -DCMAKE_BUILD_TYPE=Release
    rm -rf "$COMP_DIR"
    exit 1
  fi
  rm -rf "$COMP_DIR"
done

# ---------------------------------------------------------------------------
# 5. Verify no host-internal headers leaked
# ---------------------------------------------------------------------------

echo ""
echo "--- Step 5: Verify installed-header sanity ---"

# Nothing source-tree-private should appear under include/. tests/ and
# examples/ directories from the module source trees must NOT be exported.
# (The pj_plugins/testing/ headers ARE intentionally public test helpers.)
LEAKED=0
for forbidden_dir in /tests/ /examples/ /src/; do
  if find "$STAGING_DIR/include" -path "*${forbidden_dir}*" -type f 2>/dev/null | grep -q .; then
    echo "ERROR: Found forbidden directory '${forbidden_dir}' in installed include/"
    find "$STAGING_DIR/include" -path "*${forbidden_dir}*" -type f
    LEAKED=1
  fi
done
if [[ $LEAKED -ne 0 ]]; then
  exit 1
fi

echo ""
echo "=== plotjuggler_sdk install test PASSED ==="
