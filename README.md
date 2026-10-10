# Plex for PlayStation Vita

01.42 adds Settings > Volume boost (100%, 125%, 150%, 200%, 300%). Boost applies to video, music and offline playback; high boosts can distort loud passages. The Vita volume buttons still control the system volume. Drag vertically through settings and choice menus; swipe up/down or left/right over library cards and media posters to turn pages. Tapping still opens an item; releasing a drag does not.

Settings > Check for app updates uses releases from https://github.com/stevenjc2009-byte/vita-plex-client. Settings > Check GitHub updates at launch controls the automatic check. Installation requires a complete matching `vita-plex-client-<version>.vpk` release asset and an existing VitaShell installation of this app. Releases are version checked and extracted into a validated staging directory before promotion. Future releases are built and attached by the GitHub tag workflow.


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

Playback progress is journalled without a token and retried after a failed save/restart, scoped to the server and client. A full recovery journal or failing storage is reported. A library scan happens only when explicitly selected and confirmed. Refresh reloads listings. Settings checks GitHub releases and offers Download and install update. Launch-time update checks can be disabled. Packages are validated and extracted to staging before the Vita promoter installs the matching title. Installation closes the app; relaunch it from LiveArea.

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

01.40 separates bounded segment prefetch, video decoding and audio decoding; infers missing AAC timestamps from sample counts; freezes progress during buffering; drains final partial PCM blocks; retries transient transfers and stops on Wi-Fi disconnection. Progress follows verified server identities across network changes. Changed settings retain a validated backup; unchanged settings do not rotate it. Playback gains touch seeking, track switching, diagnostics, adaptive quality and a small up-next card. Browsing gains genre/year/watch/media filters, collections/playlists, named server/account profiles, and automatic scan monitoring. 01.41 completes compatible Direct Play, account-qualified skip markers, optional offline downloads and the in-app GitHub installer.

Run `sh tools/run-tests.sh` for unit/integration regressions. `sh tools/run-stream-tests.sh` additionally requires desktop FFmpeg 6.x development libraries and the `ffmpeg` command (or `STREAM_CODEC_PREFIX` and `FFMPEG` overrides). It generates synthetic fixtures for real backend decoding, including slow segments, missing AAC timestamps, unequal track endings, 30-second playback, pause, restart and cancellation. These tests do not replace real Vita hardware, full-film, remote/relay or suspend/resume validation.

## 01.41 playback, downloads and updates

Compatible Direct Play is enabled by default: MP4, H.264 at most 1280x720 / 30 fps / level 4.1, 8-bit, and one mono/stereo AAC track at 44.1 or 48 kHz. The original bitrate must fit the connection quality setting. Selected subtitles requiring burning and other formats use HLS transcoding. Failed Direct Play startup falls back to HLS. File reads use bounded 512 KiB verified HTTP ranges, including 64-bit offsets and MP4 files whose metadata is at the end. Native codec sources include MOV demuxing and the decoder's required AVC bitstream filters.

Skip Intro and Skip Credits appear in a small touch button during valid marker intervals (R also skips). The server's owner must expose Plex Pass and the viewing account must qualify. No subscription is inferred just from the presence of markers. Plex account checks run on the Vita; host tests use fixtures and do not send the saved token to plex.tv.

Offline downloads default to Off. Enable Settings > Offline downloads, choose a 128 / 256 / 512 / 1024 MB global limit, then use a video's Options > Download / resume download. Compatible originals use ranged MP4; other supported videos use the existing H.264/AAC HLS transcode saved as TS. Partial transfers checkpoint committed bytes and segment sequence. Resume checks account/server identity and media/track preferences, opens a fresh authorized URL and continues retained data. Eight slots bound catalog growth; quota includes partial files and seek indexes. Ordinary playback never writes movie segments to storage. Download recovery can also be started in Settings > Manage / play downloads.

Completed downloads play and seek without Wi-Fi. TS segment indexes avoid seeking into undecodable GOPs. Local position is saved every ten seconds and at stop; Plex progress is journalled without tokens for later recovery when connected. Audio and subtitle choices are fixed when downloading; change the online selection and download again to alter them.

The GitHub updater expects an official vAPP_VERSION release and its `vita-plex-client-APP_VERSION.vpk` asset. It rejects other URLs, traversal, duplicate paths, symbolic links, excessive entry/expanded sizes, failed CRCs, mismatched title IDs and versions. It copies the existing app's installation header and loads PAF / promoter modules with cleanup. It does not install until Download and install update is selected. No release is published by a local build; GitHub CI publishes when the matching tag is pushed.

Validation covers fixture-based quota/recovery/account isolation, package rejection, HTTP ranges, decoder playback and seeking. Installing over a running app, native hardware decoding and account-qualified markers still require testing on a real Vita; a successful build or desktop decode does not certify those hardware behaviors.
