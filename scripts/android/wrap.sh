#!/system/bin/sh
# wrap.sh — required for running ASan-instrumented native code on Android.
#
# Android does not automatically load the ASan runtime; this script ensures
# LD_PRELOAD is set before the app's process starts. The script MUST reside
# at lib/<abi>/wrap.sh inside the APK and be marked executable.
#
# This file is a *template* stored outside the Android source set so it does
# NOT get packaged into non-ASan APKs.  scripts/android/build_asan.sh copies
# it into the correct resources directory at build time.
#
# References:
#   https://developer.android.com/ndk/guides/asan
#   https://source.android.com/docs/security/test/asan

HERE="$(cd "$(dirname "$0")" && pwd)"

# The ASan runtime .so is bundled alongside this script in the same directory
# by the Gradle build when ASan is enabled.
# Output to stderr (appears in logcat) instead of a file to avoid permission issues.
export ASAN_OPTIONS=halt_on_error=0:detect_leaks=0:print_stats=1:abort_on_error=0
export LD_PRELOAD="${HERE}/libclang_rt.asan-aarch64-android.so"

exec "$@"
