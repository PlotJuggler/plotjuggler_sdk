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

echo "=== SDK Install Test ==="
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
  -DCMAKE_INSTALL_PREFIX="$STAGING_DIR"

cmake --build "$BUILD_DIR" -j "$(nproc)"

# ---------------------------------------------------------------------------
# 2. Install to staging directory
# ---------------------------------------------------------------------------

echo ""
echo "--- Step 2: Install to staging ---"

cmake --install "$BUILD_DIR"

echo ""
echo "Installed files:"
find "$STAGING_DIR" -type f | sort

# ---------------------------------------------------------------------------
# 3. Build the out-of-tree consumer against the installed SDK
# ---------------------------------------------------------------------------

echo ""
echo "--- Step 3: Configure + build sdk_consumer example ---"

cmake -S "$SCRIPT_DIR/examples/sdk_consumer" -B "$CONSUMER_BUILD_DIR" \
  -DCMAKE_PREFIX_PATH="$STAGING_DIR" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$CONSUMER_BUILD_DIR" -j "$(nproc)"

echo ""
echo "--- Step 4: Verify no host-internal headers leaked ---"

LEAKED=0
for pattern in 'pj_datastore/'; do
  if grep -r "$pattern" "$STAGING_DIR/include/" 2>/dev/null; then
    echo "ERROR: Found leaked dependency: ${pattern}"
    LEAKED=1
  fi
done
if [[ $LEAKED -ne 0 ]]; then
  echo "FAIL: Installed headers reference host-internal dependencies."
  exit 1
fi

echo ""
echo "=== SDK Install Test PASSED ==="
