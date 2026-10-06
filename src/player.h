#pragma once
// SceAvPlayer HLS playback with real video + audio output.
// player_play_hls() starts the stream; player_run_blocking() pumps
// video frames to the screen and audio to the speakers until X is
// pressed or the stream ends, then restores the text UI.
// Vita-only implementation in player.c; host builds see stubs.

int player_play_hls(const char *hls_url);
int player_active(void);
void player_run_blocking(void);
void player_stop(void);
