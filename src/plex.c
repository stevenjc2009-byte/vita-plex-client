#include "plex.h"
#include <stdio.h>

void plex_build_sections_url(
  const char *server, const char *token, char *out, unsigned out_len) {
  snprintf(out, out_len, "%s/library/sections?X-Plex-Token=%s", server, token);
}

void plex_build_vita_transcode_url(
  const char *server, const char *token, const char *key,
  char *out, unsigned out_len) {
  // Universal transcode pinned to Vita hardware decode limits.
  // Forces 720p->544p H.264 + AAC, directPlay=0 so PMS burns/transcodes.
  snprintf(out, out_len,
    "%s/video/:/transcode/universal/start.m3u8?"
    "path=%s&"
    "mediaIndex=0&partIndex=0&protocol=hls&"
    "videoResolution=960x544&maxVideoBitrate=%d&"
    "videoCodec=h264&audioCodec=aac&audioChannels=2&"
    "directPlay=0&directStream=0&"
    "X-Plex-Token=%s",
    server, key, VITA_PLEX_VIDEO_BITRATE_KBPS, token);
}
