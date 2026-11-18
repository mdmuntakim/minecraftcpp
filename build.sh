#!/usr/bin/env bash
# ============================================================================
# build.sh -- build MinecraftC++ for the host and for Android
# ----------------------------------------------------------------------------
#   ./build.sh              host tests + benchmark + APK
#   ./build.sh host         host tests + benchmark only
#   ./build.sh apk          the APK (host tools are built first if needed)
#   ./build.sh check        host tests + compile the native library for every
#                           supported ABI (arm64-v8a is shipped, x86_64 is a
#                           compile-only portability check)
#
# The APK is written to dist/MinecraftCpp-<version>-arm64-v8a.apk and is signed
# with the project key in build/keys (generated on first use).
# ============================================================================
set -euo pipefail

cd "$(dirname "$0")"

VERSION="0.1.0"
MODE="${1:-all}"
ROOT="$(pwd)"
BUILD="$ROOT/build"
DIST="$ROOT/dist"

log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

find_sdk() {
    local sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
    [ -n "$sdk" ] || die "ANDROID_HOME is not set (Android SDK required for the APK)"
    [ -d "$sdk" ] || die "ANDROID_HOME does not exist: $sdk"
    printf '%s' "$sdk"
}

find_ndk() {
    local sdk="$1"
    if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then
        printf '%s' "$ANDROID_NDK_HOME"
        return
    fi
    [ -d "$sdk/ndk" ] || die "no NDK under $sdk/ndk"
    local ndk
    ndk="$(find "$sdk/ndk" -maxdepth 1 -mindepth 1 -type d | sort -V | tail -1)"
    [ -n "$ndk" ] || die "no NDK installed"
    printf '%s' "$ndk"
}

host_build() {
    log "host build (tests + benchmark + art generator)"
    cmake -S "$ROOT" -B "$BUILD/host" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD/host" >/dev/null
    log "unit tests"
    ctest --test-dir "$BUILD/host" --output-on-failure
    log "benchmark"
    "$BUILD/host/mcc_bench"
}

generate_art() {
    host_build_if_needed
    log "generating the icon and the launch screen"
    rm -rf "$BUILD/generated"
    "$BUILD/host/mcc_icongen" --out "$BUILD/generated/res"
}

host_build_if_needed() {
    if [ ! -x "$BUILD/host/mcc_icongen" ] || [ ! -x "$BUILD/host/mcc_tests" ]; then
        host_build
    fi
}

android_build_one() {
    local abi="$1"
    local ndk="$2"
    local dir="$BUILD/android-$abi"
    log "native build for $abi"
    cmake -S "$ROOT" -B "$dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-21 \
        -DANDROID_STL=c++_static \
        -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$dir" >&2
    local so="$dir/libminecraftcpp.so"
    [ -f "$so" ] || die "native library missing: $so"
    local strip
    strip="$(find "$ndk/toolchains/llvm/prebuilt" -name llvm-strip | head -1)"
    [ -n "$strip" ] && "$strip" --strip-unneeded "$so"
    printf '%s' "$so"
}

check_elf() {
    local so="$1" abi="$2" ndk="$3"
    local reader
    reader="$(find "$ndk/toolchains/llvm/prebuilt" -name llvm-readelf | head -1)"
    [ -n "$reader" ] || die "llvm-readelf not found"
    local machine
    machine="$("$reader" -h "$so" | awk -F: '/Machine/ {gsub(/^ +/, "", $2); print $2}')"
    case "$abi" in
        arm64-v8a) [ "$machine" = "AArch64" ] || die "$so is not AArch64: $machine" ;;
        x86_64)    [ "$machine" = "Advanced Micro Devices X86-64" ] || die "$so is not x86_64: $machine" ;;
    esac
    log "$abi ELF OK ($machine)"
}

package_apk() {
    local sdk ndk so
    sdk="$(find_sdk)"
    ndk="$(find_ndk "$sdk")"
    so="$(android_build_one arm64-v8a "$ndk")"
    check_elf "$so" arm64-v8a "$ndk"

    generate_art
    cp -r "$ROOT/res/." "$BUILD/generated/res/"

    local build_tools aapt2 zipalign apksigner android_jar
    build_tools="$sdk/build-tools/$(ls "$sdk/build-tools" | sort -V | tail -1)"
    [ -x "$build_tools/aapt2" ] || die "aapt2 not found in $build_tools"
    aapt2="$build_tools/aapt2"
    zipalign="$build_tools/zipalign"
    apksigner="$build_tools/apksigner"
    android_jar="$(ls "$sdk"/platforms/android-*/android.jar | sort -V | tail -1)"
    [ -f "$android_jar" ] || die "android.jar not found"

    log "packaging resources"
    rm -rf "$BUILD/apk"
    mkdir -p "$BUILD/apk/lib/arm64-v8a"
    "$aapt2" compile --dir "$BUILD/generated/res" -o "$BUILD/apk/resources.zip" >/dev/null
    "$aapt2" link -o "$BUILD/apk/base.apk" \
        -I "$android_jar" \
        --manifest "$ROOT/AndroidManifest.xml" \
        -R "$BUILD/apk/resources.zip" \
        --auto-add-overlay >/dev/null

    cp "$so" "$BUILD/apk/lib/arm64-v8a/libminecraftcpp.so"
    (cd "$BUILD/apk" && zip -q -X base.apk lib/arm64-v8a/libminecraftcpp.so 2>/dev/null ||
        zip -q base.apk lib/arm64-v8a/libminecraftcpp.so)

    log "aligning and signing"
    "$zipalign" -f -p 4 "$BUILD/apk/base.apk" "$BUILD/apk/aligned.apk"

    local keystore="$BUILD/keys/minecraftcpp.jks"
    if [ ! -f "$keystore" ]; then
        mkdir -p "$BUILD/keys"
        keytool -genkeypair -keystore "$keystore" -storepass minecraftcpp \
            -keypass minecraftcpp -alias minecraftcpp -keyalg RSA -keysize 2048 \
            -validity 10000 -dname "CN=Minecraft Cpp, O=Minecraft Cpp, C=BD" \
            >/dev/null 2>&1 || die "keytool failed to create the project key"
        log "generated a project signing key at $keystore"
    fi

    mkdir -p "$DIST"
    local apk="$DIST/MinecraftCpp-$VERSION-arm64-v8a.apk"
    rm -f "$apk"
    "$apksigner" sign --ks "$keystore" --ks-pass pass:minecraftcpp \
        --key-pass pass:minecraftcpp --out "$apk" "$BUILD/apk/aligned.apk"
    "$apksigner" verify --verbose "$apk" >/dev/null || die "signature verification failed"

    local badging
    badging="$("$aapt2" dump badging "$apk" | head -6)"
    log "APK: $apk ($(du -h "$apk" | cut -f1))"
    printf '%s\n' "$badging"
    sha256sum "$apk"
}

case "$MODE" in
    host)
        host_build
        ;;
    apk)
        host_build_if_needed
        package_apk
        ;;
    check)
        host_build
        sdk="$(find_sdk)"
        ndk="$(find_ndk "$sdk")"
        for abi in arm64-v8a x86_64; do
            so="$(android_build_one "$abi" "$ndk")"
            check_elf "$so" "$abi" "$ndk"
        done
        log "all ABIs compiled"
        ;;
    all|*)
        host_build
        package_apk
        ;;
esac
