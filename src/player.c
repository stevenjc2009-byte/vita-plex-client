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
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>
#include "gui.h"
#include "video.h"
#include "performance.h"

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
static unsigned final_position,seek_position;
static int valid_position,natural_end;
static void (*progress_callback)(unsigned,int);
static unsigned *clean_frame;
static SceUID second_fb=-1;
static unsigned *display_frames[2];
static int display_index;
static uint64_t conversion_total;static unsigned conversion_count,conversion_max;
static volatile int paused;
static volatile int audio_error;

static void *player_alloc(void *arg,uint32_t alignment,uint32_t size) {
  (void)arg;
  if(alignment<sizeof(void*))alignment=sizeof(void*);
  if(!size || (alignment&(alignment-1)))return NULL;
  return memalign(alignment,size);
}
static void player_free(void *arg,void *ptr){(void)arg;free(ptr);}
static void *frame_alloc(void *arg,uint32_t alignment,uint32_t size) {
  (void)arg;
  if(alignment<0x40000)alignment=0x40000;
  if(!size || (alignment&(alignment-1)) || size>0xFFFFFFFFu-(alignment-1))return NULL;
  SceKernelAllocMemBlockOpt opt={0};opt.size=sizeof(opt);opt.attr=4;opt.alignment=alignment;
  SceUID id=sceKernelAllocMemBlock("plex_decode",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
    (size+alignment-1)&~(alignment-1),&opt);
  void *ptr=NULL;
  if(id>=0 && sceKernelGetMemBlockBase(id,&ptr)<0){sceKernelFreeMemBlock(id);return NULL;}
  return ptr;
}
static void frame_free(void *arg,void *ptr) {
  (void)arg;
  if(ptr){SceUID id=sceKernelFindMemBlockByAddr(ptr,0);if(id>=0)sceKernelFreeMemBlock(id);}
}
static void player_event(void *arg,int32_t event,int32_t source,void *data) {
  (void)arg;(void)source;(void)data;plog("player event 0x%X",event);
}

// 960x544x32bpp scanout buffer. Must be CDRAM (physically contiguous):
// malloc'd heap is not scanout-capable and shows white (gui v01.24).
static SceUID fb_block = -1;
static unsigned int *framebuf = NULL;

static int blit_frame(const SceAvPlayerFrameInfo *vf) {
  static video_job_t job;
  uint64_t before=sceKernelGetProcessTimeWide();
  if(video_prepare(&job,vf->pData,vf->details.video.width,vf->details.video.height,vf->details.video.aspectRatio,clean_frame))return -1;
  video_convert(&job);
  unsigned elapsed=(unsigned)(sceKernelGetProcessTimeWide()-before);conversion_total+=elapsed;conversion_count++;if(elapsed>conversion_max)conversion_max=elapsed;return 0;
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
#define ACC_SAMPLES PORT_SAMPLES

static int audio_thread(SceSize argc, void *argv) {
  (void)argc; (void)argv;
  static short acc[ACC_SAMPLES * 2]; // room for stereo
  unsigned acc_frames = 0; // frames = samples per channel
  int channels = 2;

  int rate=0,port_channels=0;
  while (!stop_flag && handle >= 0) {
    if(paused){sceKernelDelayThread(5000);continue;}
    SceAvPlayerFrameInfo fr;
    memset(&fr, 0, sizeof(fr));
    if (!sceAvPlayerGetAudioData(handle, &fr) || !fr.pData) {
      sceKernelDelayThread(5000);
      continue;
    }
    channels = (int)fr.details.audio.channelCount;
    if (channels < 1 || channels > 2) { plog("unsupported audio channels=%d",channels);audio_error=-6;return -6; }

    if (audio_port < 0 || channels!=port_channels || (int)fr.details.audio.sampleRate!=rate) {
      if(audio_port>=0){sceAudioOutReleasePort(audio_port);audio_port=-1;}
      acc_frames=0;port_channels=channels;rate=(int)fr.details.audio.sampleRate;
      int mode = channels == 2 ?
        SCE_AUDIO_OUT_MODE_STEREO : SCE_AUDIO_OUT_MODE_MONO;
      // MAIN accepts only 48 kHz; BGM also accepts 44.1 kHz AAC output.
      audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
        PORT_SAMPLES, (int)fr.details.audio.sampleRate, mode);
      if (audio_port < 0) {audio_error=audio_port;return audio_port;}
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
          int rc=sceAudioOutOutput(audio_port, o);
          if(rc<0){audio_error=rc;return rc;}
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
  final_position=seek_position=0;valid_position=natural_end=0;audio_error=0;paused=0;
  int r = 0;
  if (!av_loaded) {
    r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if (r < 0) { plog("play sysmod FAIL r=0x%X", r); return r; }
    av_loaded = 1;
  }

  SceAvPlayerInitData init;
  memset(&init, 0, sizeof(init));
  init.memoryReplacement.allocate=player_alloc;
  init.memoryReplacement.deallocate=player_free;
  init.memoryReplacement.allocateTexture=frame_alloc;
  init.memoryReplacement.deallocateTexture=frame_free;
  init.eventReplacement.eventCallback=player_event;
  init.basePriority=125;
  init.numOutputVideoFrameBuffers=3;
  init.defaultLanguage="eng";
  init.autoStart = SCE_TRUE; // start when ready, rather than racing AddSource
  init.debugLevel = 0;

  handle = sceAvPlayerInit(&init);
  if (handle < 0) { plog("play init FAIL r=0x%X", handle); return handle; }

  r = sceAvPlayerAddSource(handle, hls_url);
  plog("play addsrc r=0x%X", r); // URLs contain the private Plex token
  if (r < 0) { player_stop(); return r; }

  paused=0;final_position=0;audio_error=0;conversion_total=conversion_count=conversion_max=0;
  return 0;
}

int player_active(void) {
  if (handle < 0) return 0;
  return sceAvPlayerIsActive(handle) == SCE_TRUE;
}

// Returns 0 after normal playback, <0 when nothing ever played:
// -1 no handle, -2 framebuffer alloc fail, -3 stream never went
// active within 45s (bad URL / server refused), -4 user cancelled wait.
int player_run(const char *title,unsigned duration,unsigned base_offset) {
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
  clean_frame=calloc(FB_W*FB_H,sizeof(unsigned));
  second_fb=sceKernelAllocMemBlock("plex_video_back",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,2*1024*1024,NULL);
  display_frames[0]=framebuf;display_frames[1]=NULL;
  if(second_fb>=0)sceKernelGetMemBlockBase(second_fb,(void**)&display_frames[1]);
  if(!clean_frame || !display_frames[1]){player_stop();return -2;}
  display_index=0;memset(framebuf,0,FB_W*FB_H*4);video_pool_init();

  SceCtrlData pad, old;
  memset(&old, 0, sizeof(old));
  sceCtrlPeekBufferPositive(0, &old, 1); // opening X is not a stop press
  SceAvPlayerFrameInfo vf;
  memset(&vf, 0, sizeof(vf));

  // HLS needs seconds to buffer: the old code checked IsActive once
  // and instantly bailed when the stream wasn't up yet, so X looked
  // dead. Wait up to 45s for active (O cancels), then pump.
  int waited = 0, cancelled = 0;
  while (!stop_flag && sceAvPlayerIsActive(handle) != SCE_TRUE &&
      waited < 450) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned pressed = pad.buttons & ~old.buttons;
    old = pad;
    if (pressed & (SCE_CTRL_CIRCLE|SCE_CTRL_START)) { cancelled = pressed&SCE_CTRL_START?2:1; break; }
    sceKernelDelayThread(100000);
    waited++;
  }
  plog("play active=%d waited=%dms cancelled=%d",
    sceAvPlayerIsActive(handle) == SCE_TRUE, waited * 100, cancelled);
  if (cancelled) { player_stop(); return cancelled==2?1:-4; }
  if (sceAvPlayerIsActive(handle) != SCE_TRUE) {
    player_stop();
    return -3;
  }

  // Starting before IsActive made the audio thread exit during buffering.
  audio_tid = performance_thread("plex_audio",audio_thread,0x10000100,0x4000,0x20000);
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
  int quit=0,user_stopped=0,show_controls=180,restart=0,redraw=1,last_visible=-1;
  unsigned last_second=~0u,last_progress=0;int progress_sent=0;
  unsigned no_frames=0;
  while (!stop_flag && (paused || sceAvPlayerIsActive(handle) == SCE_TRUE)) {
    if(audio_error){plog("audio output FAIL 0x%X",audio_error);break;}
    if (sceAvPlayerGetVideoData(handle, &vf) && vf.pData) {
      if (!frames)
        plog("play first frame %ux%u", vf.details.video.width,
          vf.details.video.height);
      if(blit_frame(&vf)){plog("invalid video frame %ux%u",vf.details.video.width,vf.details.video.height);break;}
      frames++;
      if(!(frames%120))plog("render conversion avg=%uus max=%uus frames=%d stamp=%llu clock=%llu",conversion_count?(unsigned)(conversion_total/conversion_count):0,conversion_max,frames,(unsigned long long)vf.timeStamp,(unsigned long long)sceAvPlayerCurrentTime(handle));
      no_frames=0;
      valid_position=1;redraw=1;
    }
    else if(!paused)no_frames++;
    if(valid_position)final_position=base_offset+(unsigned)sceAvPlayerCurrentTime(handle);
    int visible=show_controls>0;unsigned second=final_position/1000;
    if(visible!=last_visible || (visible && second!=last_second))redraw=1;
    if(redraw){display_index^=1;framebuf=display_frames[display_index];memcpy(framebuf,clean_frame,FB_W*FB_H*4);
      if(visible)gui_player_overlay(framebuf,title,final_position,duration,paused,NULL);present();redraw=0;last_visible=visible;last_second=second;}
    if(show_controls>0 && !paused)show_controls--;
    if(progress_callback && valid_position && (second>=last_progress+10 || !progress_sent)){progress_callback(final_position,paused?2:1);last_progress=second;progress_sent=1;}
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned pressed = pad.buttons & ~old.buttons;
    old = pad;
    if (pressed & SCE_CTRL_START) {quit=1;break;}
    if (pressed & SCE_CTRL_CIRCLE){user_stopped=1;break;}
    if (pressed & SCE_CTRL_CROSS) {
      int rc=paused?sceAvPlayerResume(handle):sceAvPlayerPause(handle);
      if(rc>=0)paused=!paused;
      else plog("pause/resume FAIL 0x%X",rc);
      show_controls=180;redraw=1;
      if(progress_callback && valid_position)progress_callback(final_position,paused?2:1);
    }
    if(pressed & SCE_CTRL_TRIANGLE)show_controls=show_controls?0:180;
    if(pressed & (SCE_CTRL_LEFT|SCE_CTRL_RIGHT)) {
      unsigned at=valid_position?final_position:base_offset;
      seek_position=(pressed&SCE_CTRL_LEFT)?(at>10000?at-10000:0):at+10000;
      if(duration && seek_position>=duration)seek_position=duration>1000?duration-1000:0;
      restart=1;break; // rebuild HLS at absolute time, including before the resume base
    }
    // A stream that never produces video must not be reported as success.
    if(!frames && no_frames>1800){plog("active without video");break;}
    sceDisplayWaitVblankStart();
  }
  plog("play end frames=%d", frames);

  natural_end=!restart && !quit && !user_stopped && frames && sceAvPlayerIsActive(handle)!=SCE_TRUE;
  player_stop();
  return audio_error?audio_error:restart?2:quit?1:frames?0:-5;
}
int player_run_blocking(void){return player_run("Now playing",0,0);}
unsigned player_position(void){return valid_position?final_position:0;}
unsigned player_seek_position(void){return seek_position;}
int player_completed(void){return natural_end;}
void player_progress_callback(void (*cb)(unsigned,int)){progress_callback=cb;}

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
  video_pool_shutdown();free(clean_frame);clean_frame=NULL;
  if (fb_block >= 0) {
    psvDebugScreenInit(); // restore scanout before freeing its old buffer
    sceDisplayWaitVblankStart();
    sceKernelFreeMemBlock(fb_block);
    fb_block = -1;
  }
  if(second_fb>=0){sceKernelFreeMemBlock(second_fb);second_fb=-1;}
  display_frames[0]=display_frames[1]=NULL;framebuf = NULL;
}

#else

int player_play_hls(const char *hls_url) {
  (void)hls_url;
  return -1; // host stub: no AvPlayer
}
int player_active(void) { return 0; }
int player_run_blocking(void) { return -1; }
int player_run(const char *title,unsigned duration,unsigned offset){(void)title;(void)duration;(void)offset;return -1;}
unsigned player_position(void){return 0;}
unsigned player_seek_position(void){return 0;}
int player_completed(void){return 0;}
void player_progress_callback(void (*cb)(unsigned,int)){(void)cb;}
void player_stop(void) {}

#endif
