#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
: "${VITASDK:?Set VITASDK to your VitaSDK directory}"
prefix="$root/build/deps/ffmpeg-vita"
source_dir="$root/build/deps/FFmpeg-ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2"
mkdir -p "$root/build/deps"
archive="$root/build/deps/ffmpeg-n6.0.tar.gz"
patch_file="$root/build/deps/wiliwili-ffmpeg.patch"
if [ ! -f "$archive" ]; then curl -fL https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/ea3d24bbe3c58b171e55fe2151fc7ffaca3ab3d2 -o "$archive"; fi
if [ ! -f "$patch_file" ]; then curl -fL https://raw.githubusercontent.com/xfangfang/wiliwili/88e5876bea9502d06f46a8656e3530684d3aaf7d/scripts/psv/ffmpeg/ffmpeg.patch -o "$patch_file"; fi
printf '%s  %s\n' aacce24d5bb6c67fbf1e3343bc15a6977ad700cdb4f43c8551f6544fa54ab7ec "$archive" | sha256sum -c -
printf '%s  %s\n' 2d38529d10c74560db3909cf8c2f3e359b128c04fb29f6dd0085770ed81cef6c "$patch_file" | sha256sum -c -
if [ ! -d "$source_dir" ]; then tar -xzf "$archive" -C "$root/build/deps"; fi
cd "$source_dir"
if [ ! -f README-vita.md ]; then git apply "$patch_file"; fi
export PATH="$VITASDK/bin:$PATH"
./configure --prefix="$prefix" --enable-vita --target-os=vita --enable-cross-compile --arch=arm \
 --cross-prefix=arm-vita-eabi- --ar=arm-vita-eabi-gcc-ar --ranlib=arm-vita-eabi-gcc-ranlib --nm=arm-vita-eabi-gcc-nm \
 --disable-runtime-cpudetect --disable-armv5te --enable-small \
 --extra-cflags="-Os -ffunction-sections -fdata-sections -Wno-error=incompatible-pointer-types -Wno-error=enum-int-mismatch" \
 --extra-ldflags="-Wl,--gc-sections" --disable-shared --enable-static --disable-programs --disable-doc --disable-autodetect --disable-network \
 --disable-avfilter --disable-swscale --disable-swresample --disable-avdevice --disable-encoders \
 --disable-decoders --enable-decoder=h264,h264_vita,aac_vita --disable-demuxers --enable-demuxer=mpegts --disable-muxers \
 --disable-parsers --enable-parser=aac,aac_latm,ac3,h264,mpegaudio --disable-protocols --disable-bsfs \
 --disable-iconv --disable-lzma --disable-sdl2 --disable-xlib --enable-pthreads
make -j"${PLEX_CODEC_JOBS:-8}"
make install
