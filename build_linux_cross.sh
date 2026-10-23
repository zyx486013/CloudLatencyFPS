#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
compiler_root="${LLVM_MINGW_DIR:-/tmp/llvm-mingw-20260616-ucrt-ubuntu-22.04-x86_64}"
compiler="$compiler_root/bin/x86_64-w64-mingw32-clang++"

if [[ ! -x "$compiler" ]]; then
  echo "LLVM-MinGW compiler not found: $compiler" >&2
  exit 1
fi

"$compiler" "$project_dir/CloudLatencyFPS.cpp" \
  -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE \
  -municode -mwindows -static -s \
  -Wl,--major-subsystem-version,6,--minor-subsystem-version,1,--stack,4194304 \
  -ldwmapi -lcomctl32 -lshell32 -lopengl32 -lgdi32 \
  -o "$project_dir/CloudLatencyFPS.exe"

echo "Built: $project_dir/CloudLatencyFPS.exe"
