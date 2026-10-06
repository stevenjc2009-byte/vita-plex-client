#pragma once
// SceAvPlayer HLS playback with real video + audio output.
// player_play_hls() starts the stream; player_run_blocking() waits up
// to 15s for it to go active, pumps video frames to the screen and
// audio to the speakers until X is pressed or the stream ends, then
// restores the text UI. Returns 0 after playback, <0 when nothing
// played (-1 no handle, -2 fb alloc fail, -3 never active, -4 cancel).
// Vita-only implementation in player.c; host builds see stubs.

int player_play_hls(const char *hls_url);
int player_active(void);
int player_run_blocking(void);
void player_stop(void);
