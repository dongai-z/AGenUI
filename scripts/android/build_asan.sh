#!/bin/bash
set -euo pipefail

# ============================================================================
# scripts/android/build_asan.sh
# Build the Android Playground APK with AddressSanitizer (ASan) enabled.
#
# This script:
#   1. Locates the ASan runtime library from the NDK toolchain
#   2. Copies it + wrap.sh into the Playground APK resources
#   3. Builds the SDK AAR with ASan flags
#   4. Builds the Playground APK (debug) with ASan
#
# Prerequisites:
#   - ANDROID_NDK_HOME or NDK installed via Android SDK (sdk/ndk/<version>)
#   - Device/emulator running Android 9+ (API 28+) for wrap.sh support
#
# Usage:
#   ./scripts/android/build_asan.sh [--install]
#
# Options:
#   --install    Install the APK to connected device after build
#   --clean      Clean build artifacts before building
#   -h, --help   Show this help message
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PLAYGROUND_ROOT="${REPO_ROOT}/playground/android"
RESOURCES_LIB="${PLAYGROUND_ROOT}/app/src/main/resources/lib/arm64-v8a"

DO_INSTALL=false
DO_CLEAN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install)  DO_INSTALL=true; shift ;;
        --clean)    DO_CLEAN=true; shift ;;
        -h|--help)  grep "^#" "$0" | sed 's/^#//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

# --- Locate NDK ---
find_ndk() {
    if [[ -n "${ANDROID_NDK_HOME:-}" ]] && [[ -d "$ANDROID_NDK_HOME" ]]; then
        echo "$ANDROID_NDK_HOME"
        return
    fi
    # Try to find NDK via ANDROID_HOME/sdk
    local sdk_root="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
    if [[ -z "$sdk_root" ]]; then
        # Try local.properties
        local lp="${PLAYGROUND_ROOT}/local.properties"
        if [[ -f "$lp" ]]; then
            sdk_root=$(grep '^sdk.dir=' "$lp" | cut -d= -f2)
        fi
    fi
    if [[ -n "$sdk_root" ]] && [[ -d "${sdk_root}/ndk" ]]; then
        # Pick the latest NDK version
        local ndk_dir
        ndk_dir=$(ls -d "${sdk_root}/ndk/"* 2>/dev/null | sort -V | tail -1)
        if [[ -n "$ndk_dir" ]]; then
            echo "$ndk_dir"
            return
        fi
    fi
    echo ""
}

NDK_DIR=$(find_ndk)
if [[ -z "$NDK_DIR" ]]; then
    echo "ERROR: Cannot find Android NDK. Set ANDROID_NDK_HOME or install NDK via SDK Manager." >&2
    exit 1
fi
echo "[ASan] Using NDK: ${NDK_DIR}"

# --- Locate ASan runtime .so ---
ASAN_LIB=""
# NDK r23+ layout: toolchains/llvm/prebuilt/<host>/lib/clang/<version>/lib/linux/
for candidate in \
    "${NDK_DIR}/toolchains/llvm/prebuilt/"*/lib/clang/*/lib/linux/libclang_rt.asan-aarch64-android.so \
    "${NDK_DIR}/toolchains/llvm/prebuilt/"*/lib64/clang/*/lib/linux/libclang_rt.asan-aarch64-android.so; do
    if [[ -f "$candidate" ]]; then
        ASAN_LIB="$candidate"
        break
    fi
done

if [[ -z "$ASAN_LIB" ]]; then
    echo "ERROR: Cannot find libclang_rt.asan-aarch64-android.so in NDK at: ${NDK_DIR}" >&2
    echo "       Searched: toolchains/llvm/prebuilt/*/lib/clang/*/lib/linux/" >&2
    exit 1
fi
echo "[ASan] Found ASan runtime: ${ASAN_LIB}"

# --- Copy ASan runtime + wrap.sh into resources ---
mkdir -p "${RESOURCES_LIB}"
cp -f "${ASAN_LIB}" "${RESOURCES_LIB}/"
echo "[ASan] Copied ASan runtime to: ${RESOURCES_LIB}/libclang_rt.asan-aarch64-android.so"

# Copy wrap.sh from scripts/android/ (kept outside the source set so
# normal non-ASan builds never include it in the APK).
WRAP_SH_SRC="${SCRIPT_DIR}/wrap.sh"
if [[ ! -f "${WRAP_SH_SRC}" ]]; then
    echo "ERROR: wrap.sh template not found at ${WRAP_SH_SRC}" >&2
    exit 1
fi
cp -f "${WRAP_SH_SRC}" "${RESOURCES_LIB}/wrap.sh"
chmod +x "${RESOURCES_LIB}/wrap.sh"
echo "[ASan] Copied wrap.sh to: ${RESOURCES_LIB}/wrap.sh"

# --- Build ---
cd "${PLAYGROUND_ROOT}"

if [[ "$DO_CLEAN" == true ]]; then
    echo "[ASan] Cleaning build artifacts..."
    ./gradlew clean 2>&1 | cat
fi

echo "[ASan] Building Playground APK with ASan enabled..."
./gradlew assembleDebug -PagenuiEnableAsan=true 2>&1 | cat

APK_PATH="${PLAYGROUND_ROOT}/app/build/outputs/apk/debug/app-debug.apk"
if [[ -f "$APK_PATH" ]]; then
    echo ""
    echo "============================================================"
    echo "[ASan] BUILD SUCCESS"
    echo "[ASan] APK: ${APK_PATH}"
    echo "============================================================"
else
    echo "ERROR: APK not found at expected path: ${APK_PATH}" >&2
    exit 1
fi

# --- Install ---
if [[ "$DO_INSTALL" == true ]]; then
    echo "[ASan] Installing APK to device..."
    adb install -r "$APK_PATH"
    echo "[ASan] INSTALLED. Launch the app and check logcat for ASan output:"
    echo "       adb logcat | grep -i 'asan\\|addresssanitizer'"
fi

echo ""
echo "=== ASan Testing Guide ==="
echo "1. Install: adb install -r ${APK_PATH}"
echo "2. Launch the app on device"
echo "3. Monitor ASan output:"
echo "   adb logcat | grep -iE 'asan|AddressSanitizer|ERROR:'"
echo "4. Check ASan log file on device:"
echo "   adb shell cat /data/local/tmp/asan.log.*"
echo ""
echo "NOTE: Device must be running Android 9+ (API 28) for wrap.sh support."
echo "      For rooted devices or emulators, you can also use:"
echo "      adb shell setprop wrap.com.amap.agenuiplayground '\"logwrapper\"'"
