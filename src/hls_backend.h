#pragma once
#include <stdint.h>
typedef struct {const unsigned char *data;unsigned width,height;float aspect;uint64_t stamp;} hls_video_t;
typedef struct {const unsigned char *data;unsigned channels,rate,size;uint64_t stamp;} hls_audio_t;
int hls_backend_start(const char *url);
void hls_backend_stop(void);
int hls_backend_active(void);
int hls_backend_video(hls_video_t *frame);
int hls_backend_audio(hls_audio_t *frame);
uint64_t hls_backend_time(void);
int hls_backend_pause(int pause);
int hls_backend_error(void);
const char *hls_backend_stage(void);
