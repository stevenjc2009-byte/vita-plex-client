# Plex for PlayStation Vita

01.37 replaces the rejected native HLS URL source with application-managed HLS download and MPEG-TS demuxing, followed by explicit Vita H.264/AAC hardware codecs. It follows Plex master playlists, inherits authentication only within the same server origin, bounds downloads and queues, and stops the media request independently of metadata/progress requests. Error notices identify the failing stage; native codec errors are recorded without URLs or tokens. Local desktop decoding and cross-build checks do not prove physical Vita playback.

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

Set VITASDK to your SDK and run `bash tools/build-stream-codecs.sh` to build the pinned codec dependencies, then configure with its share/vita.toolchain.cmake and build using CMake. The VPK filename includes the version. Windows runners: tools/run-regression-tests.ps1, run-player-tests.ps1, run-core-tests.ps1 and run-http-tests.ps1; supply -Compiler if gcc is not on PATH. Linux: sh tools/run-tests.sh.

Release CI runs these tests, validates the release tag and matching version definitions, and uses a digest-pinned VitaSDK container. Update APP_VERSION and VITA_VERSION together, then tag v followed by that version. CI release execution has to be verified on GitHub; local tests do not run that service.

Scan library files is available in the left sidebar: press Left from the first poster column, choose Scan library, then scan the current library or choose another library / all libraries. It asks Plex Media Server to check its media folders in the background. Check scan status reports Plex's `refreshing` state; Square / Refresh list reloads the displayed results after scanning. Settings also contains the scan action. A successful request means the scan was accepted, not that scanning has finished. New media must already be in folders configured on the server.

01.34 also protects the final progress snapshot, preserves a checkpoint when the decoder has no newer valid position, and recovers a complete backup when the journal is truncated. Cancelling progress saving leaves the durable checkpoint for retry. Playback stalls time out after 45 seconds without a video frame while playing; paused playback is exempt. Invalid frames and audio errors do not trigger next-episode autoplay. Cancelled connection probes stop trying alternate addresses. Network initialization is retried by subsequent requests.

01.35 adds front touch input for library cards, posters, sidebar actions, menus, details, keyboard and playback controls. Actions activate on release; a drag or multiple fingers does not activate a button. Tap anywhere during an app-managed network wait to cancel. The on-screen keyboard has accented Latin, Greek and Cyrillic pages selected with L/R or Characters; Delete removes a complete UTF-8 character. Supported glyphs are packed into sparse atlases and missing font glyphs are excluded to save memory.

The progress journal now holds 32 entries and reads previous eight-entry journals. Settings > Retry saved playback progress opens recovery controls, including confirmed removal of old-connection or all pending records. If the journal is full, playback still attempts online timeline updates and reports failures; it cannot retain that new item's position offline until a slot is freed.

Network failure notices include the initialization/worker stage and the actual error; debug.log records these without URLs or tokens. Already-loaded modules are retained rather than unloaded. LAN HTTP browsing remains usable if optional HTTPS initialization fails; HTTPS is rejected safely in that case. Thread creation also tries the documented default priority if its preferred priority cannot be used.

Power callbacks rebuild an active stream at its last observed position after resume, preserve pause state across restarts, and reapply the selected boost mode. Physical suspend/reconnect behavior still needs validation. A premature decoder shutdown more than five seconds before the metadata duration ends returns an error and suppresses autoplay. Diagnostics/logs report the CPU-core mask actually observed during conversion (F means cores 0-3; 7 means cores 0-2); this is distinct from merely accepting a thread affinity request. A rejected fourth-core affinity uses a balanced three-core conversion pool.

The streaming codec build pins FFmpeg n6.0 and the wiliwili Vita codec patch by commit and SHA-256. Dependency notices and licenses are included in the VPK. The release includes the exact dependency sources and patch for rebuilding; no Sony firmware, BEAV library or overclock plugin is bundled. The current media backend supports MPEG-TS HLS with H.264 video and AAC mono/stereo audio up to 720p, with 8 MiB segments and a 512 KiB playlist limit. Unsupported encrypted, fragmented-MP4, discontinuous or cross-server playlists fail explicitly. The selected first variant is prepared by Plex at the requested quality.

`tools/hls_backend_test.c` exercises the real demux/decoder worker with local MPEG-TS HLS fixtures on desktop. Link it, src/hls_backend.c and src/hls.c against FFmpeg n6.0 avformat/avcodec/avutil (mpegts demuxer, H.264/AAC decoders and parsers), pthread and math. Pass a folder containing index.m3u8 and referenced segments. Desktop tests use software codecs; Vita builds select h264_vita and aac_vita explicitly.

## Watch away from home (01.38)

Enable Remote Access in Plex Media Server, keep the server online and link this Vita to the same account (or an account granted library access). On Vita use Settings > Find and select Plex server. The client remembers the server identity and discovers its current published connections. Automatic mode tries home HTTPS, home HTTP, direct remote HTTPS, then HTTPS Relay. Away from home mode skips local connections. Settings > Reconnect selected server refreshes the addresses; failed library requests also attempt one reconnection. Manually entered addresses retain manual control.

Remote requests verify certificates and hostname; insecure public HTTP endpoints are excluded from automatic discovery. The trust store now includes fingerprint-verified ISRG Root X1 alongside the Plex account certificate chain. Diagnostics show TLS errors rather than disabling verification. Relay requests 1 Mbps video to leave room for audio within Plex's 2 Mbps relay limit. Remote playback uses WAN context.

Use another Wi-Fi network or a phone hotspot to test. Home upload speed and the connection's download speed must support the selected quality. Plex currently requires Plex Pass on the server owner's account, or Plex Pass/Remote Watch Pass on the viewing account for remote video. Router/ISP configuration, including CGNAT or double NAT, can prevent direct access; configure Remote Access on the server and router following Plex's instructions. The app does not change your router or publish ports. Worldwide availability and actual remote playback on Vita are not proven by local tests.

Official setup: https://support.plex.tv/articles/200289506-remote-access/
Requirements: https://support.plex.tv/articles/requirements-for-remote-playback-of-personal-media/
Relay: https://support.plex.tv/articles/216766168-accessing-a-server-through-relay/

01.40 separates bounded segment prefetch, video decoding and audio decoding; infers missing AAC timestamps from sample counts; freezes progress during buffering; drains final partial PCM blocks; retries transient transfers and stops on Wi-Fi disconnection. Progress follows verified server identities across network changes. Changed settings retain a validated backup; unchanged settings do not rotate it. Playback gains touch seeking, track switching, diagnostics, adaptive quality and a small up-next card. Browsing gains genre/year/watch/media filters, collections/playlists, named server/account profiles, and automatic scan monitoring. Compatible Direct Play, account-qualified skip markers and optional offline downloads are still unfinished.

Run `sh tools/run-tests.sh` for unit/integration regressions. `sh tools/run-stream-tests.sh` additionally requires desktop FFmpeg 6.x development libraries and the `ffmpeg` command (or `STREAM_CODEC_PREFIX` and `FFMPEG` overrides). It generates synthetic fixtures for real backend decoding, including slow segments, missing AAC timestamps, unequal track endings, 30-second playback, pause, restart and cancellation. These tests do not replace real Vita hardware, full-film, remote/relay or suspend/resume validation.
