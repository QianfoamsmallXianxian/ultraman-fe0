#!/usr/bin/env bash
set -o pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/libretro-ppsspp"
OUT="$ROOT/app/src/main/jniLibs/arm64-v8a"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
[ -n "$NDK" ] || { echo "no NDK"; exit 1; }
[ -d "$SRC/libretro" ] || { echo "no src"; exit 1; }
mkdir -p "$OUT"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
[ -d "$TC" ] || TC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
[ -d "$TC" ] || { echo "no tc"; exit 1; }
export PATH="$TC/bin:$PATH"
export NDK_ROOT="$NDK"
export NDK="$NDK"
cd "$SRC/libretro"
J="${BUILD_JOBS:-4}"
take() {
    local s
    s=$(find "$SRC" -name "*ppsspp*.so" -o -name "*libretro*.so" 2>/dev/null | head -1)
    [ -n "$s" ] && [ -f "$s" ] && cp "$s" "$OUT/libppsspp_libretro_android.so" && ls -lh "$OUT/libppsspp_libretro_android.so" && return 0
    return 1
}
make -j"$J" platform=android-arm64 NDK_ROOT="$NDK" 2>&1 | tail -5
take && exit 0
make clean >/dev/null 2>&1 || true
make -j"$J" platform=android TARGET_ARCH=arm64 NDK_ROOT="$NDK" 2>&1 | tail -5
take && exit 0
if [ -d jni ]; then
    cd jni
    "$NDK/ndk-build" NDK_PROJECT_PATH=. APP_ABI=arm64-v8a -j"$J" 2>&1 | tail -5 || true
    take && exit 0
fi
echo "core build failed"
exit 1
