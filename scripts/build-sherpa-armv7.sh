#!/usr/bin/env bash
# Build libsherpa-mnn-jni.so for armeabi-v7a from MNN 3.6.0 source.
# Prerequisites: NDK 27.2.12479018, CMake 3.22.1, JDK 17.
# Run from the repo root (where mnn/ and app/ live).
set -e

source "$(dirname "$0")/../localbuild/env.sh" 2>/dev/null || true

: "${ANDROID_NDK:?Please set ANDROID_NDK}"
: "${MNN_SRC_DIR:?Please set MNN_SRC_DIR (path to alibaba/MNN 3.6.0 checkout)}"
: "${MNN_LIB_ARMV7:?Please set MNN_LIB_ARMV7 (dir with include/ and lib/libMNN.so for armv7)}"

SHERPA_DIR="${MNN_SRC_DIR}/apps/frameworks/sherpa-mnn"
BUILD_DIR="${MNN_SRC_DIR}/../sherpa-build-armv7"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$SHERPA_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK}/build/cmake/android.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DANDROID_ABI=armeabi-v7a \
  -DANDROID_STL=c++_static \
  -DANDROID_PLATFORM=android-21 \
  -DMNN_LIB_DIR="$MNN_LIB_ARMV7" \
  -DBUILD_SHARED_LIBS=ON \
  -DSHERPA_MNN_ENABLE_JNI=ON \
  -DSHERPA_MNN_ENABLE_TTS=ON \
  -DSHERPA_MNN_ENABLE_SPEAKER_DIARIZATION=ON \
  -DSHERPA_MNN_ENABLE_BINARY=OFF \
  -DSHERPA_MNN_ENABLE_C_API=OFF \
  -DSHERPA_MNN_ENABLE_PYTHON=OFF \
  -DSHERPA_MNN_ENABLE_TESTS=OFF \
  -DSHERPA_MNN_ENABLE_CHECK=OFF \
  -DSHERPA_MNN_ENABLE_PORTAUDIO=OFF \
  -DSHERPA_MNN_LINK_LIBSTDCPP_STATICALLY=ON \
  -DBUILD_PIPER_PHONMIZE_EXE=OFF \
  -DBUILD_PIPER_PHONMIZE_TESTS=OFF \
  -DBUILD_ESPEAK_NG_EXE=OFF \
  -DBUILD_ESPEAK_NG_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=./install

make -j"$(nproc)"

# Strip debug symbols
STRIP="${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
$STRIP --strip-unneeded lib/libsherpa-mnn-jni.so

# Install into repo jniLibs
mkdir -p app/src/main/jniLibs/armeabi-v7a
cp lib/libsherpa-mnn-jni.so app/src/main/jniLibs/armeabi-v7a/libsherpa-mnn-jni.so

echo "DONE: app/src/main/jniLibs/armeabi-v7a/libsherpa-mnn-jni.so"
