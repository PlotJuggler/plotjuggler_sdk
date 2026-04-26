#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

QT_VERSION="6.8.3"
QT_DIR="${SCRIPT_DIR}/.qt/${QT_VERSION}/gcc_64"

if [[ -d "$QT_DIR" ]]; then
  echo "Qt ${QT_VERSION} already installed at ${QT_DIR}"
  echo "export CMAKE_PREFIX_PATH=${QT_DIR}"
  exit 0
fi

if ! command -v aqt &>/dev/null; then
  echo "Installing aqtinstall..."
  pip install 'aqtinstall>=3.1'
fi

echo "Installing Qt ${QT_VERSION} via aqtinstall..."
aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
  --modules qtcharts qtwebsockets \
  --outputdir "${SCRIPT_DIR}/.qt"

echo ""
echo "Qt ${QT_VERSION} installed at ${QT_DIR}"
echo ""
echo "To use it, run:"
echo "  export CMAKE_PREFIX_PATH=${QT_DIR}"
