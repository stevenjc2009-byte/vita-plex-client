# Plex for PlayStation Vita

01.36 fixes a native decoder handle being mistaken for a negative error: opaque handles such as `0x81400280` are valid even though the SDK exposes a signed integer type. The player now uses an empty handle of zero and separately recognizes AvPlayer error codes. High-bit handles are retained through playback, audio pumping and cleanup. Artwork retains its aspect ratio in both grids and details, including wide episode thumbnails; empty space is padded instead of stretching the image.

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

Music tracks can request AAC stereo HLS and use the audio-only player. Photos use a bounded, server-resized JPEG viewer. Both additions require testing with real music/photo libraries and on the Vita. Fonts include supported Latin extensions, Greek, Cyrillic and punctuation; unavailable glyphs fall back. Actual decoding, clock/plugin behavior and frame rate must be tested on your Vita. Debug logs record conversion timings and decoder errors without playback URLs.

## Build and test

Set VITASDK to your SDK, then configure with its share/vita.toolchain.cmake and build using CMake. The VPK filename includes the version. Windows runners: tools/run-regression-tests.ps1, run-player-tests.ps1, run-core-tests.ps1 and run-http-tests.ps1; supply -Compiler if gcc is not on PATH. Linux: sh tools/run-tests.sh.

Release CI runs these tests, validates the release tag and matching version definitions, and uses a digest-pinned VitaSDK container. Update APP_VERSION and VITA_VERSION together, then tag v followed by that version. CI release execution has to be verified on GitHub; local tests do not run that service.

Scan library files is available in the left sidebar: press Left from the first poster column, choose Scan library, then scan the current library or choose another library / all libraries. It asks Plex Media Server to check its media folders in the background. Check scan status reports Plex's `refreshing` state; Square / Refresh list reloads the displayed results after scanning. Settings also contains the scan action. A successful request means the scan was accepted, not that scanning has finished. New media must already be in folders configured on the server.

01.34 also protects the final progress snapshot, preserves a checkpoint when the decoder has no newer valid position, and recovers a complete backup when the journal is truncated. Cancelling progress saving leaves the durable checkpoint for retry. Playback stalls time out after 45 seconds without a video frame while playing; paused playback is exempt. Invalid frames and audio errors do not trigger next-episode autoplay. Cancelled connection probes stop trying alternate addresses. Network initialization is retried by subsequent requests.

01.35 adds front touch input for library cards, posters, sidebar actions, menus, details, keyboard and playback controls. Actions activate on release; a drag or multiple fingers does not activate a button. Tap anywhere during an app-managed network wait to cancel. The on-screen keyboard has accented Latin, Greek and Cyrillic pages selected with L/R or Characters; Delete removes a complete UTF-8 character. Supported glyphs are packed into sparse atlases and missing font glyphs are excluded to save memory.

The progress journal now holds 32 entries and reads previous eight-entry journals. Settings > Retry saved playback progress opens recovery controls, including confirmed removal of old-connection or all pending records. If the journal is full, playback still attempts online timeline updates and reports failures; it cannot retain that new item's position offline until a slot is freed.

Network failure notices include the initialization/worker stage and the actual error; debug.log records these without URLs or tokens. Already-loaded modules are retained rather than unloaded. LAN HTTP browsing remains usable if optional HTTPS initialization fails; HTTPS is rejected safely in that case. Thread creation also tries the documented default priority if its preferred priority cannot be used.

Power callbacks rebuild an active stream at its last observed position after resume, preserve pause state across restarts, and reapply the selected boost mode. Physical suspend/reconnect behavior still needs validation. A premature decoder shutdown more than five seconds before the metadata duration ends returns an error and suppresses autoplay. Diagnostics/logs report the CPU-core mask actually observed during conversion (F means cores 0-3; 7 means cores 0-2); this is distinct from merely accepting a thread affinity request. A rejected fourth-core affinity uses a balanced three-core conversion pool.
