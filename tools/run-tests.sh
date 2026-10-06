#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
root="$PWD"
mkdir -p "$root/build"
scratch=$(mktemp -d "$root/build/host-tests.XXXXXX")
case "$scratch" in "$root/build/host-tests."*) ;; *) exit 1 ;; esac
trap 'rm -rf "$scratch"' EXIT
compiler=${CC:-cc}
"$compiler" -std=c11 -Wall -Wextra -Werror -Isrc tools/regression_test.c src/plex_auth.c src/plex.c src/browse.c src/settings.c src/update.c -o "$scratch/regression"
(cd "$scratch" && ./regression)
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Isrc tools/library_scan_test.c src/library.c src/browse.c src/plex_auth.c -o "$scratch/library"
"$scratch/library"
for header in audioout.h avplayer.h ctrl.h display.h kernel/sysmem.h io/fcntl.h kernel/threadmgr.h kernel/processmgr.h sysmodule.h types.h; do
 mkdir -p "$scratch/include/psp2/$(dirname "$header")"
 printf '#include <psp2/mock.h>\n' > "$scratch/include/psp2/$header"
done
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -DPLEX_MOCK_VITA -Dmemalign=mock_memalign -I"$scratch/include" -Itools/mock-vita -Isrc tools/player_lifecycle_test.c src/player.c src/video.c -o "$scratch/player"
"$scratch/player"
printf '#include <psp2/mock.h>\n' > "$scratch/include/psp2/power.h"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -pthread -I"$scratch/include" -Itools/mock-vita -Isrc tools/parallel_video_test.c src/video.c src/performance.c -o "$scratch/cores"
"$scratch/cores"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -DPLEX_TEST_JOURNAL -pthread -I"$scratch/include" -Itools/mock-vita -Isrc tools/progress_recovery_test.c src/progress.c src/video.c src/performance.c src/plex_auth.c -o "$scratch/progress"
(cd "$scratch" && ./progress && ./progress recover)
for header in kernel/sysmem.h io/fcntl.h net/net.h net/netctl.h net/http.h libssl.h sysmodule.h kernel/threadmgr.h kernel/processmgr.h; do
 mkdir -p "$scratch/include/psp2/$(dirname "$header")"
 printf '#include <psp2/http_mock.h>\n' > "$scratch/include/psp2/$header"
done
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -I"$scratch/include" -Itools/mock-vita -Isrc tools/http_lifecycle_test.c src/http.c -o "$scratch/http"
"$scratch/http"
python3 tools/check-release.py
