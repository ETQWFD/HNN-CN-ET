#!/usr/bin/env bash
# build.sh — HNN CN ET Server v1.3.4 四目标编译脚本
# Copyright (c) 2026 ETC · MIT License
# 用法: bash build.sh            # 全部目标
#       bash build.sh linux      # 只编 Linux amd64
#       bash build.sh termux64   # 只编 Termux arm64
#       bash build.sh termux32   # 只编 Termux armv7
#       bash build.sh windows    # 只编 Windows DLL+EXE
set -u
cd "$(dirname "$0")"

NDK_BIN=/home/user/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin
MINGW_BIN=/home/user/tools/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64/bin

do_linux() {
    echo "[1/4] Linux amd64 ..."
    gcc -O2 -Wall -pthread -o ai-linux ai.c
}
do_termux64() {
    echo "[2/4] Termux arm64 (bionic, 无需 -lpthread) ..."
    "$NDK_BIN/aarch64-linux-android21-clang" -O2 -fPIE -pie -o ai-termux-arm64 ai.c
}
do_termux32() {
    echo "[3/4] Termux armv7 (bionic, 无需 -lpthread) ..."
    "$NDK_BIN/armv7a-linux-androideabi21-clang" -O2 -fPIE -pie -march=armv7-a -o ai-termux-armv7 ai.c
}
do_windows() {
    echo "[4/4] Windows: windres -> DLL -> EXE ..."
    "$MINGW_BIN/x86_64-w64-mingw32-windres" versioninfo_dll.rc -O coff -o dllres.o
    "$MINGW_BIN/x86_64-w64-mingw32-gcc" -O2 -DHNN_CORE_DLL -shared ai.c dllres.o \
        -Wl,--out-implib,libhnn_core.a -lws2_32 -lwinpthread -o hnn_core.dll
    "$MINGW_BIN/x86_64-w64-mingw32-gcc" -O2 launcher_win.c dllres.o -L. -lhnn_core -lws2_32 -lwinpthread -o ai.exe
}

case "${1:-all}" in
    linux)    do_linux ;;
    termux64) do_termux64 ;;
    termux32) do_termux32 ;;
    windows)  do_windows ;;
    all)      do_linux; do_termux64; do_termux32; do_windows ;;
    *) echo "unknown target: $1"; exit 1 ;;
esac
echo "done."
