#pragma once
// 0 stop/end, 1 exit, 2 restart at absolute seek position, negative error.
int player_play_hls(const char *hls_url);
int player_active(void);
int player_run_blocking(void);
void player_stop(void);
int player_run(const char *title, unsigned duration_ms, unsigned base_offset_ms);
unsigned player_position(void);

unsigned player_seek_position(void);
int player_completed(void);
void player_progress_callback(void (*callback)(unsigned position,int state));
