# Plex for PlayStation Vita

Browse libraries, search movies and series, and play H.264/AAC streams prepared by your Plex server on a jailbroken Vita.

## Install and use

Install the complete VPK with VitaShell. Existing PLEX00001 settings are retained. Link your account at plex.tv/link or enter a server/token in Settings. Your Plex server must be able to transcode video.

D-pad moves; X selects; Circle returns; Triangle searches; Square refreshes; L/R changes pages; SELECT opens Settings; START exits. Left at the first grid column enters the sidebar. Home offers Continue Watching, Recently Added and Unwatched.

In video details, Square opens audio/subtitle selection, watched status and next-episode options. During playback X pauses/resumes, Circle stops, Left/Right seeks ten seconds, and Triangle toggles controls. Seeking restarts HLS at the desired absolute position and may briefly buffer.

## Performance

The default requests 500 MHz CPU / 222 MHz GPU, with a 444 MHz CPU fallback when 500 is rejected. Settings also offers 444/222 and plugin/original clocks. The overclock plugin can override requests; Diagnostics displays clock readback. Save a Plex00001 profile in your installed PSVshellPlus if required. The app never modifies plugin configuration and attempts to restore captured clocks on normal exit.

Video conversion uses the main thread plus three row workers. The extra worker requests CPU3 through CapUnlocker's affinity mask and falls back if rejected. Audio and network/poster work have separate threads. Idle workers sleep on semaphores. No plugin is installed by this app. Hardware decoding and system threads remain controlled by the Vita SDK/OS.

## Reliability and settings

Controller input remains active during network jobs. Circle cancels them. Settings includes quality, resume, autoplay, subtitles, diagnostics, recovery retry, poster cache clearing, preference reset and account unlinking. Artwork downloads are bounded to 512 KiB and the cache to 32 MiB; VPK downloads are bounded separately.

Playback progress is journalled without a token and retried after a failed save/restart, scoped to the server and client. A full recovery journal or failing storage is reported. A library scan happens only when explicitly selected and confirmed. Refresh reloads listings. Updates are downloaded from Settings and installed manually with VitaShell after exit.

Music and photos are browsable but not played. Fonts include Latin extensions, Greek, Cyrillic and punctuation; other characters fall back. Actual decoding, clock/plugin behavior and frame rate must be tested on your Vita. Debug logs record conversion timings and decoder errors without playback URLs.

## Build and test

Set VITASDK to your SDK, then configure with its share/vita.toolchain.cmake and build using CMake. The VPK filename includes the version. Windows runners: tools/run-regression-tests.ps1, run-player-tests.ps1, run-core-tests.ps1 and run-http-tests.ps1; supply -Compiler if gcc is not on PATH. Linux: sh tools/run-tests.sh.

Release CI runs these tests, validates the release tag and matching version definitions, and uses a digest-pinned VitaSDK container. Update APP_VERSION and VITA_VERSION together, then tag v followed by that version. CI release execution has to be verified on GitHub; local tests do not run that service.
