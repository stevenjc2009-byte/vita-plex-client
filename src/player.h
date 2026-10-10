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

int player_run_media(const char *title,unsigned duration,unsigned offset,int audio_only);

void player_start_paused(int paused);
int player_was_paused(void);
const char *player_error_stage(void);

void player_adaptive(int enabled);
void player_volume(int percent);

int player_play_file(const char *url,int kind,unsigned offset);

#include "media.h"
void player_set_markers(const media_marker_t *markers,unsigned count);
