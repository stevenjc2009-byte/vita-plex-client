// SceAvPlayer HLS playback: YVU420 video frames CPU-blitted to the
// display framebuffer + PCM16 audio pumped to SceAudioOut.
// Frame format confirmed against SonicMastr/Vita-Media-Player (real,
// working player code): pData is YVU420P2 semi-planar, dims from
// details.video.width/height; audio is S16 PCM.

#ifdef __vita__

#include "player.h"

#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>
#include <string.h>

void psvDebugScreenInit(void);

#define FB_W 960
#define FB_H 544

static SceAvPlayerHandle handle = -1;
static volatile int stop_flag = 0;
static volatile int audio_port = -1;

// 960x544x32bpp scanout buffer.
static unsigned int framebuf[FB_W * FB_H];

static int clamp8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return v;
}

// YVU420 semi-planar (Y plane, then interleaved V,U) -> A8B8G8R8.
// BT.601 integer math, 2 pixels per loop. Any source size is
// nearest-neighbor scaled and centered on the 960x544 screen.
static void blit_yvu420(const unsigned char *p, unsigned w, unsigned h) {
  if (!p || !w || !h) return;
  unsigned out_w = w > FB_W ? FB_W : w;
  unsigned out_h = h > FB_H ? FB_H : h;
  unsigned off_x = (FB_W - out_w) / 2;
  unsigned off_y = (FB_H - out_h) / 2;

  for (unsigned y = 0; y < out_h; y++) {
    unsigned sy = y * h / out_h;
    const unsigned char *row_y = p + sy * w;
    const unsigned char *row_vu = p + w * h + (sy / 2) * w;
    unsigned int *dst =
      framebuf + (off_y + y) * FB_W + off_x;
    for (unsigned x = 0; x < out_w; x += 2) {
      unsigned sx0 = x * w / out_w;
      unsigned sx1 = (x + 1) * w / out_w;
      if (sx1 >= w) sx1 = w - 1;
      unsigned char v = row_vu[(sx0 / 2) * 2];
      unsigned char u = row_vu[(sx0 / 2) * 2 + 1];
      int d = (int)u - 128, e = (int)v - 128;
      int c0 = (int)row_y[sx0] - 16;
      int c1 = (int)row_y[sx1] - 16;
      int r0 = (298 * c0 + 409 * e + 128) >> 8;
      int g0 = (298 * c0 - 100 * d - 208 * e + 128) >> 8;
      int b0 = (298 * c0 + 516 * d + 128) >> 8;
      int r1 = (298 * c1 + 409 * e + 128) >> 8;
      int g1 = (298 * c1 - 100 * d - 208 * e + 128) >> 8;
      int b1 = (298 * c1 + 516 * d + 128) >> 8;
      // A8B8G8R8 in memory = R,G,B,A bytes.
      dst[0] = (unsigned)clamp8(r0) | ((unsigned)clamp8(g0) << 8) |
        ((unsigned)clamp8(b0) << 16) | 0xFF000000u;
      dst[1] = (unsigned)clamp8(r1) | ((unsigned)clamp8(g1) << 8) |
        ((unsigned)clamp8(b1) << 16) | 0xFF000000u;
      dst += 2;
    }
  }
}

static void present(void) {
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  fb.base = framebuf;
  fb.pitch = FB_W;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = FB_W;
  fb.height = FB_H;
  sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

// Audio pump: AvPlayer hands us S16 PCM chunks; SceAudioOut wants
// fixed-size outputs, so accumulate into port-sized blocks.
#define PORT_SAMPLES 1024
#define ACC_SAMPLES (PORT_SAMPLES * 4)

static int audio_thread(SceSize argc, void *argv) {
  (void)argc; (void)argv;
  static short acc[ACC_SAMPLES * 2]; // room for stereo
  unsigned acc_frames = 0; // frames = samples per channel
  int channels = 2;

  while (!stop_flag && handle >= 0 &&
      sceAvPlayerIsActive(handle) == SCE_TRUE) {
    SceAvPlayerFrameInfo fr;
    memset(&fr, 0, sizeof(fr));
    if (!sceAvPlayerGetAudioData(handle, &fr) || !fr.pData) {
      sceKernelDelayThread(5000);
      continue;
    }
    channels = (int)fr.details.audio.channelCount;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;

    if (audio_port < 0) {
      int mode = channels == 2 ?
        SCE_AUDIO_OUT_MODE_STEREO : SCE_AUDIO_OUT_MODE_MONO;
      audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
        PORT_SAMPLES, (int)fr.details.audio.sampleRate, mode);
      if (audio_port < 0) return audio_port;
    }

    unsigned bytes = fr.details.audio.size;
    short *src = (short *)fr.pData;
    unsigned frames = bytes / (unsigned)(channels * 2);
    while (frames > 0) {
      unsigned room = ACC_SAMPLES - acc_frames;
      unsigned take = frames < room ? frames : room;
      memcpy(acc + acc_frames * channels, src,
        take * (unsigned)channels * 2);
      src += take * channels;
      acc_frames += take;
      frames -= take;
      if (acc_frames == ACC_SAMPLES) {
        short *o = acc;
        for (unsigned i = 0; i < ACC_SAMPLES / PORT_SAMPLES; i++) {
          sceAudioOutOutput(audio_port, o);
          o += PORT_SAMPLES * channels;
          if (stop_flag) break;
        }
        acc_frames = 0;
      }
    }
  }
  return 0;
}

int player_play_hls(const char *hls_url) {
  int r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
  if (r < 0) return r;

  SceAvPlayerInitData init;
  memset(&init, 0, sizeof(init));
  init.autoStart = SCE_TRUE;
  init.debugLevel = 0;

  handle = sceAvPlayerInit(&init);
  if (handle < 0) return handle;

  r = sceAvPlayerAddSource(handle, hls_url);
  if (r < 0) return r;

  return sceAvPlayerStart(handle);
}

int player_active(void) {
  if (handle < 0) return 0;
  return sceAvPlayerIsActive(handle) == SCE_TRUE;
}

void player_run_blocking(void) {
  if (handle < 0) return;
  stop_flag = 0;
  memset(framebuf, 0, sizeof(framebuf));

  SceUID atid = sceKernelCreateThread("plex_audio", audio_thread,
    0x10000100, 0x4000, 0, 0, NULL);
  if (atid >= 0) sceKernelStartThread(atid, 0, NULL);

  SceCtrlData pad, old;
  memset(&old, 0, sizeof(old));
  SceAvPlayerFrameInfo vf;
  memset(&vf, 0, sizeof(vf));

  while (!stop_flag && sceAvPlayerIsActive(handle) == SCE_TRUE) {
    if (sceAvPlayerGetVideoData(handle, &vf) && vf.pData) {
      blit_yvu420(vf.pData, vf.details.video.width,
        vf.details.video.height);
      present();
    }
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned pressed = pad.buttons & ~old.buttons;
    old = pad;
    if (pressed & SCE_CTRL_CROSS) break;
    sceDisplayWaitVblankStart();
  }

  stop_flag = 1;
  if (atid >= 0) {
    sceKernelWaitThreadEnd(atid, NULL, NULL);
    sceKernelDeleteThread(atid);
  }
  player_stop();
  psvDebugScreenInit(); // hand the screen back to the text UI
}

void player_stop(void) {
  stop_flag = 1;
  if (audio_port >= 0) {
    sceAudioOutReleasePort(audio_port);
    audio_port = -1;
  }
  if (handle >= 0) {
    sceAvPlayerStop(handle);
    sceAvPlayerClose(handle);
    handle = -1;
  }
}

#else

int player_play_hls(const char *hls_url) {
  (void)hls_url;
  return -1; // host stub: no AvPlayer
}
int player_active(void) { return 0; }
void player_run_blocking(void) {}
void player_stop(void) {}

#endif
