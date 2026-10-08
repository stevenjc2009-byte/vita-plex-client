#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root="$PWD"
mkdir -p build
scratch=$(mktemp -d "$root/build/stream-tests.XXXXXX")
case "$scratch" in "$root/build/stream-tests."*) ;; *) exit 1 ;; esac
trap 'rm -rf "$scratch"' EXIT
compiler=${CC:-cc}
ffmpeg=${FFMPEG:-ffmpeg}
prefix=${STREAM_CODEC_PREFIX:-}
if [ -n "$prefix" ]; then
 cflags="-I$prefix/include"
 libs="$prefix/lib/libavformat.a $prefix/lib/libavcodec.a $prefix/lib/libavutil.a"
else
 cflags=$(pkg-config --cflags libavformat libavcodec libavutil)
 libs=$(pkg-config --libs libavformat libavcodec libavutil)
fi
platform=""
case $(uname -s) in MINGW*|MSYS*) platform="-luser32 -lbcrypt -latomic -static" ;; esac
# cflags/libs are compiler flags obtained from the supplied prefix/pkg-config.
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -pthread -Isrc $cflags tools/hls_backend_test.c src/hls_backend.c src/hls.c $libs -lm $platform -o "$scratch/stream-test"
make_fixture() {
 name=$1;video=$2;audio=$3
 mkdir -p "$scratch/$name"
 "$ffmpeg" -v error -y -f lavfi -i "testsrc2=size=320x180:rate=24:duration=$video" -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=$audio" -c:v libx264 -preset ultrafast -pix_fmt yuv420p -g 24 -c:a aac -ac 2 -b:a 128k -f hls -hls_time 1 -hls_playlist_type vod -hls_segment_filename "$scratch/$name/%d.ts" "$scratch/$name/index.m3u8"
}
make_fixture normal 3 3
make_fixture slow 6 6
make_fixture short-audio 4 1
make_fixture short-video 1 4
make_fixture long 30 30
for name in normal short-audio short-video long; do "$scratch/stream-test" "$scratch/$name"; done
"$scratch/stream-test" "$scratch/slow" slow
mkdir -p "$scratch/audio"
"$ffmpeg" -v error -y -f lavfi -i sine=frequency=440:sample_rate=48000:duration=1 -c:a aac -ac 2 -f hls -hls_time 1 -hls_playlist_type vod -hls_segment_filename "$scratch/audio/%d.ts" "$scratch/audio/index.m3u8"
"$scratch/stream-test" "$scratch/audio" audio

mkdir -p "$scratch/mp4"
"$ffmpeg" -v error -y -f lavfi -i testsrc2=size=320x180:rate=24:duration=3 -f lavfi -i sine=frequency=440:sample_rate=48000:duration=3 -c:v libx264 -preset ultrafast -pix_fmt yuv420p -g 24 -c:a aac -ac 2 "$scratch/mp4/test.mp4"
"$scratch/stream-test" "$scratch/mp4" mp4
"$scratch/stream-test" "$scratch/mp4" local
"$scratch/stream-test" "$scratch/mp4" seek
cat "$scratch/normal/0.ts" "$scratch/normal/1.ts" "$scratch/normal/2.ts" > "$scratch/mp4/test.ts"
"$scratch/stream-test" "$scratch/mp4" tsseek
"$ffmpeg" -v error -y -i "$scratch/mp4/test.mp4" -c copy -movflags +faststart "$scratch/mp4/fast.mp4"
mv "$scratch/mp4/fast.mp4" "$scratch/mp4/test.mp4"
"$scratch/stream-test" "$scratch/mp4" mp4

python_bin=${PYTHON:-python3}
command -v "$python_bin" >/dev/null 2>&1 || python_bin=python
"$python_bin" - "$scratch" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]);offset=0;points=[]
for i in range(3):
 points.append(struct.pack('<2I',i*1000,offset));offset+=(p/'normal'/f'{i}.ts').stat().st_size
(p/'mp4/test.ts.idx').write_bytes(b''.join(points))
PY
"$scratch/stream-test" "$scratch/mp4" tsseek
