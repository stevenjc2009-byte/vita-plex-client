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
mkdir -p "$scratch/include/psp2/net"
printf '#include <psp2/mock.h>\n#define SCE_NETCTL_STATE_CONNECTED 1\nint sceNetCtlInetGetState(int*);\n' > "$scratch/include/psp2/net/netctl.h"
for component in video touch; do
 "$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -pthread -D__vita__ -DPLEX_MOCK_VITA -I"$scratch/include" -Itools/mock-vita -Isrc -c "src/$component.c" -o "$scratch/$component.o"
done
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -pthread -D__vita__ -Dmemalign=mock_memalign -I"$scratch/include" -Itools/mock-vita -Isrc tools/player_stream_test.c src/player.c "$scratch/video.o" "$scratch/touch.o" -o "$scratch/player-stream"
"$scratch/player-stream"
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
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Isrc tools/connection_test.c src/connection.c src/browse.c src/plex_auth.c src/settings.c -o "$scratch/connection"
"$scratch/connection"
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Isrc tools/hls_test.c src/hls.c -o "$scratch/hls"
"$scratch/hls"
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Isrc tools/media_test.c src/media.c src/http.c src/settings.c src/browse.c src/plex_auth.c src/update.c src/progress.c -o "$scratch/media"
(cd "$scratch" && ./media)
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -pthread -DPLEX_TEST_OFFLINE -Isrc tools/offline_test.c src/offline.c src/hls.c -o "$scratch/offline"
(cd "$scratch" && ./offline)
python_bin=${PYTHON:-python3}
command -v "$python_bin" >/dev/null 2>&1 || python_bin=python
"$python_bin" tools/check-release.py
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-unused-function -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_APIS -DMINIZ_NO_TIME -Isrc tools/package_test.c src/package.c src/miniz/miniz.c src/miniz/miniz_tinfl.c src/miniz/miniz_zip.c -o "$scratch/package"
"$python_bin" tools/package-fixtures.py "$scratch/packages"
"$scratch/package" "$scratch/packages/valid.vpk" "$scratch/stage" 01.41 0
for name in wrong-title wrong-version traversal duplicate bomb corrupt; do "$scratch/package" "$scratch/packages/$name.vpk" "$scratch/stage-reject" 01.41 1; done
for header in io/dirent.h io/stat.h io/fcntl.h sysmodule.h promoterutil.h kernel/processmgr.h; do
 printf '#include <psp2/update_mock.h>\n' > "$scratch/include/psp2/$header"
done
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -Dfopen=update_test_fopen -I"$scratch/include" -Itools/mock-vita -Isrc -c src/update.c -o "$scratch/update.o"
"$compiler" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Itools/mock-vita -Isrc tools/update_installer_test.c src/plex_auth.c "$scratch/update.o" -o "$scratch/install-test"
"$scratch/install-test"
