#!/bin/bash -eu

script_dir=${0%/*}
ws_dir=$(realpath "$script_dir")

# Check if clangd-22 is available
if ! command -v clangd-22 &> /dev/null; then
  echo "Error: clangd-22 is not installed or not in PATH."
  echo ""
  echo "To install on Ubuntu/Debian via https://apt.llvm.org/:"
  echo "  wget https://apt.llvm.org/llvm.sh"
  echo "  chmod +x llvm.sh"
  echo "  sudo ./llvm.sh 22"
  echo "  sudo apt install clangd-22 clang-tidy-22"
  exit 1
fi

if [[ "${1:-}" == "--help" ]]; then
  echo "Usage: $(basename "$0") [build_path]"
  echo "Run clang-tidy on all C++ sources in pj_base, pj_datastore, pj_plugins, and pj_scene_protocol."
  echo
  echo "Arguments:"
  echo "  build_path   Path to build directory containing compile_commands.json (default: build)"
  exit 0
fi

cmake_build_path="$ws_dir/${1:-build}"

if [ ! -f "$cmake_build_path/compile_commands.json" ]; then
  echo "Error: compile_commands.json not found in $cmake_build_path"
  echo "Please build the project first with CMake to generate compile_commands.json"
  exit 1
fi

source_dirs=(
  "$ws_dir/pj_base"
  "$ws_dir/pj_datastore"
  "$ws_dir/pj_plugins"
  "$ws_dir/pj_scene_protocol"
)

echo "-----------------------------------------------------------"
echo "Running clang-tidy on:"
for dir in "${source_dirs[@]}"; do
  echo "  $dir"
done
echo "-----------------------------------------------------------"

find "${source_dirs[@]}" -name '*.cpp' -print0 \
  | xargs -0 -n 1 -P $(nproc) bash -c '
    set -o pipefail
    echo "$@"
    cd "'"$ws_dir"'" && clangd-22 \
      --log=error \
      --clang-tidy \
      --compile-commands-dir="'"$cmake_build_path"'" \
      --check-locations=false \
      --check="$@" \
      2>&1 | sed "s/^/${1//\//\\/}: /"
    ' _

echo "-----------------------------------------------------------"
echo "Clang-tidy complete."
echo "-----------------------------------------------------------"
