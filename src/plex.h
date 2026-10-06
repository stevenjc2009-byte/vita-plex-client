#pragma once
#include "settings.h"

// Vita-safe Plex profile: 960x544 screen, H.264 Baseline/Main <= 720p,
// AAC stereo. Server must transcode everything else to this.

#define VITA_PLEX_MAX_W 960
#define VITA_PLEX_MAX_H 544
#define VITA_PLEX_VIDEO_BITRATE_KBPS 4000
#define VITA_PLEX_AUDIO_CODEC "aac"
#define VITA_PLEX_VIDEO_CODEC "h264"

// Builds a Plex /video/:/transcode/universal/decision style query for Vita.
// Caller provides server base URL, token, metadata key, out buffer.
void plex_build_vita_transcode_url(
  const char *server, const char *token, const char *key,
  char *out, unsigned out_len);

// Builds direct library-sections browse URL.
void plex_build_sections_url(
  const char *server, const char *token, char *out, unsigned out_len);
int plex_build_playback_url(const settings_t *settings, const char *key,
  const char *session, unsigned offset_ms, char *out, unsigned size);
int plex_hls_media_url(const char *playlist_url, const char *body,
  const char *token, char *out, unsigned size);

int plex_build_music_url(const settings_t *settings,const char *key,const char *session,unsigned offset_ms,char *out,unsigned cap);
int plex_build_photo_url(const settings_t *settings,const char *key,char *out,unsigned cap);
