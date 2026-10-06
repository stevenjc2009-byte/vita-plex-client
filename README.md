# Plex Client for PS Vita (jailbroken)

Browse your Plex library and play transcoded video on a jailbroken PS Vita.

## Use

1. Install the VPK from the [latest release](../../releases/latest).
2. Launch, press X, enter the link code at plex.tv/link.
3. Browse sections, pick a video, watch. X stops, O goes back.

On every launch the app checks this repo's latest release and offers to
download + install the update on the spot.

## Release a new version

1. Bump `APP_VERSION` in `src/update.h` **and** `VITA_VERSION` in
   `CMakeLists.txt` (keep them identical, no leading `v`).
2. Commit, then tag: `git tag v01.02 && git push origin v01.02`
3. CI builds the VPK and attaches it to the release. Vitas pick it up
   automatically.

## Build locally (Windows)

```bat
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/Users/steve/vita-dev/vitasdk/vitasdk/share/vita.toolchain.cmake -B build
cmake --build build
```
