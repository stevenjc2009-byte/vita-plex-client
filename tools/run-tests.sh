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
"$compiler" -std=c11 -Wall -Wextra -Werror -Isrc tools/text_test.c src/text.c -o "$scratch/text"
"$scratch/text"
"$compiler" -std=c11 -Wall -Wextra -Werror -Isrc tools/touch_test.c src/touch.c -o "$scratch/touch"
"$scratch/touch"
for header in audioout.h avplayer.h ctrl.h display.h kernel/sysmem.h io/fcntl.h io/stat.h kernel/threadmgr.h kernel/processmgr.h sysmodule.h types.h; do
 mkdir -p "$scratch/include/psp2/$(dirname "$header")"
 printf '#include <psp2/mock.h>\n' > "$scratch/include/psp2/$header"
done
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -pthread -D__vita__ -DPLEX_MOCK_VITA -Dmemalign=mock_memalign -I"$scratch/include" -Itools/mock-vita -Isrc tools/player_lifecycle_test.c src/player.c src/video.c src/touch.c -o "$scratch/player"
"$scratch/player"
"$compiler" -std=c11 -Os -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-unused-parameter -pthread -D__vita__ -DPLEX_MOCK_VITA -DPLEX_TOUCH_EXTERNAL -I"$scratch/include" -Itools/mock-vita -Isrc tools/gui_touch_test.c src/gui.c src/text.c src/touch.c src/plex_auth.c -o "$scratch/gui"
"$scratch/gui"
printf '#include <psp2/mock.h>\n' > "$scratch/include/psp2/power.h"
printf '#include <psp2/mock.h>\n' > "$scratch/include/psp2/kernel/cpu.h"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -pthread -I"$scratch/include" -Itools/mock-vita -Isrc tools/parallel_video_test.c src/video.c src/performance.c -o "$scratch/cores"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -pthread -I"$scratch/include" -Itools/mock-vita -Isrc tools/network_lifecycle_test.c src/network.c src/video.c src/performance.c -o "$scratch/network"
"$scratch/network"
"$scratch/cores"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -DPLEX_TEST_JOURNAL -pthread -I"$scratch/include" -Itools/mock-vita -Isrc tools/progress_recovery_test.c src/progress.c src/video.c src/performance.c src/plex_auth.c -o "$scratch/progress"
(cd "$scratch" && ./progress && ./progress recover && ./progress legacy)
for header in kernel/sysmem.h io/fcntl.h net/net.h net/netctl.h net/http.h libssl.h sysmodule.h kernel/threadmgr.h kernel/processmgr.h; do
 mkdir -p "$scratch/include/psp2/$(dirname "$header")"
 printf '#include <psp2/http_mock.h>\n' > "$scratch/include/psp2/$header"
done
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -I"$scratch/include" -Itools/mock-vita -Isrc tools/http_lifecycle_test.c src/http.c -o "$scratch/http"
"$scratch/http"
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Isrc tools/hls_test.c src/hls.c -o "$scratch/hls"
"$scratch/hls"
python3 tools/check-release.py
