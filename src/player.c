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
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// Own append-log (main.c owns debug.log truncated at boot; we append).
static SceUID p_log = -1;
static void plog(const char *fmt, ...) {
  if (p_log < 0) {
    p_log = sceIoOpen("ux0:data/plex-client/debug.log",
      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (p_log < 0) return;
  }
  char tmp[256];
  int n = 0;
  va_list ap;
  va_start(ap, fmt);
  n = vsnprintf(tmp, sizeof(tmp) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof(tmp) - 3) n = sizeof(tmp) - 3;
  tmp[n++] = '\n';
  sceIoWrite(p_log, tmp, (SceSize)n);
}

void psvDebugScreenInit(void);

#define FB_W 960
#define FB_H 544

static SceAvPlayerHandle handle = -1;
static volatile int stop_flag = 0;
static volatile int audio_port = -1;
static SceUID audio_tid = -1;
static int av_loaded;

// 960x544x32bpp scanout buffer. Must be CDRAM (physically contiguous):
// malloc'd heap is not scanout-capable and shows white (gui v01.24).
static SceUID fb_block = -1;
static unsigned int *framebuf = NULL;

static int clamp8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return v;
}

// YVU420 semi-planar (Y plane, then interleaved V,U) -> A8B8G8R8.
// BT.601 integer math, 2 pixels per loop. Any source size is
// nearest-neighbor scaled and centered on the 960x544 screen.
static void blit_yvu420(const unsigned char *p, unsigned w, unsigned h) {
  if (!p || !w || !h || (w & 1) || (h & 1)) return;
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
  player_stop();
  int r = 0;
  if (!av_loaded) {
    r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if (r < 0) { plog("play sysmod FAIL r=0x%X", r); return r; }
    av_loaded = 1;
  }

  SceAvPlayerInitData init;
  memset(&init, 0, sizeof(init));
  init.autoStart = SCE_FALSE;
  init.debugLevel = 0;

  handle = sceAvPlayerInit(&init);
  if (handle < 0) { plog("play init FAIL r=0x%X", handle); return handle; }

  r = sceAvPlayerAddSource(handle, hls_url);
  plog("play addsrc r=0x%X", r); // URLs contain the private Plex token
  if (r < 0) { player_stop(); return r; }

  r = sceAvPlayerStart(handle);
  plog("play start r=0x%X", r);
  if (r < 0) player_stop();
  return r;
}

int player_active(void) {
  if (handle < 0) return 0;
  return sceAvPlayerIsActive(handle) == SCE_TRUE;
}

// Returns 0 after normal playback, <0 when nothing ever played:
// -1 no handle, -2 framebuffer alloc fail, -3 stream never went
// active within 15s (bad URL / server refused), -4 X cancelled wait.
int player_run_blocking(void) {
  if (handle < 0) return -1;
  stop_flag = 0;
  fb_block = sceKernelAllocMemBlock("plex_video",
    SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
    2 * 1024 * 1024, NULL); // CDRAM sizes must be 256KB aligned
  if (fb_block >= 0)
    sceKernelGetMemBlockBase(fb_block, (void **)&framebuf);
  if (!framebuf) {
    plog("play fb ALLOC FAIL block=0x%X", fb_block);
    player_stop();
    return -2;
  }
  memset(framebuf, 0, FB_W * FB_H * sizeof(*framebuf));

  SceCtrlData pad, old;
  memset(&old, 0, sizeof(old));
  sceCtrlPeekBufferPositive(0, &old, 1); // opening X is not a stop press
  SceAvPlayerFrameInfo vf;
  memset(&vf, 0, sizeof(vf));

  // HLS needs seconds to buffer: the old code checked IsActive once
  // and instantly bailed when the stream wasn't up yet, so X looked
  // dead. Wait up to 15s for active (X cancels), then pump.
  int waited = 0, cancelled = 0;
  while (!stop_flag && sceAvPlayerIsActive(handle) != SCE_TRUE &&
      waited < 150) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned pressed = pad.buttons & ~old.buttons;
    old = pad;
    if (pressed & SCE_CTRL_CROSS) { cancelled = 1; break; }
    sceKernelDelayThread(100000);
    waited++;
  }
  plog("play active=%d waited=%dms cancelled=%d",
    sceAvPlayerIsActive(handle) == SCE_TRUE, waited * 100, cancelled);
  if (cancelled) { player_stop(); return -4; }
  if (sceAvPlayerIsActive(handle) != SCE_TRUE) {
    player_stop();
    return -3;
  }

  // Starting before IsActive made the audio thread exit during buffering.
  audio_tid = sceKernelCreateThread("plex_audio", audio_thread,
    0x10000100, 0x4000, 0, 0, NULL);
  int r = audio_tid;
  if (r < 0) { player_stop(); return r; }
  r = sceKernelStartThread(audio_tid, 0, NULL);
  if (r < 0) {
    sceKernelDeleteThread(audio_tid);
    audio_tid = -1;
    player_stop();
    return r;
  }

  int frames = 0;
  while (!stop_flag && sceAvPlayerIsActive(handle) == SCE_TRUE) {
    if (sceAvPlayerGetVideoData(handle, &vf) && vf.pData) {
      if (!frames)
        plog("play first frame %ux%u", vf.details.video.width,
          vf.details.video.height);
      frames++;
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
  plog("play end frames=%d", frames);

  player_stop();
  return 0;
}

void player_stop(void) {
  stop_flag = 1;
  if (audio_tid >= 0) {
    sceKernelWaitThreadEnd(audio_tid, NULL, NULL);
    sceKernelDeleteThread(audio_tid);
    audio_tid = -1;
  }
  if (audio_port >= 0) {
    sceAudioOutReleasePort(audio_port);
    audio_port = -1;
  }
  if (handle >= 0) {
    sceAvPlayerStop(handle);
    sceAvPlayerClose(handle);
    handle = -1;
  }
  if (fb_block >= 0) {
    psvDebugScreenInit(); // restore scanout before freeing its old buffer
    sceDisplayWaitVblankStart();
    sceKernelFreeMemBlock(fb_block);
    fb_block = -1;
  }
  framebuf = NULL;
}

#else

int player_play_hls(const char *hls_url) {
  (void)hls_url;
  return -1; // host stub: no AvPlayer
}
int player_active(void) { return 0; }
int player_run_blocking(void) { return -1; }
void player_stop(void) {}

#endif
