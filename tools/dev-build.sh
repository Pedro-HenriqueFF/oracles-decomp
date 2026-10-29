#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${1:-linux}"
JOBS="${JOBS:-4}"

COMMON=(
  -G Ninja
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  -DORACLES_SDL_VENDORED=ON
)

case "$PLATFORM" in
  linux)
    BUILD="$ROOT/build/linux"
    cmake -S "$ROOT" -B "$BUILD" "${COMMON[@]}"
    cmake --build "$BUILD" --parallel "$JOBS"
    ;;
  windows)
    BUILD="$ROOT/build/windows"
    cmake -S "$ROOT" -B "$BUILD" "${COMMON[@]}" \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64-x86_64.cmake"
    cmake --build "$BUILD" \
      --target oracles-native --parallel "$JOBS"
    ;;
  *)
    echo "Usage: $0 [linux|windows]" >&2
    exit 2
    ;;
esac
