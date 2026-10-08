#define main native_lifecycle_main
#define sceAudioOutOutput native_audio_output
#define sceKernelStartThread native_start_thread
#include "player_lifecycle_test.c"
#undef main
#undef sceAudioOutOutput
#undef sceKernelStartThread
#include "hls_backend.h"
static int got_audio,disconnected;
int sceNetCtlInetGetState(int *state){*state=disconnected?0:1;return 0;}
int hls_backend_start(const char *url){assert(url);got_audio=0;return 0;}
int hls_backend_start_source(const char*u,int k,unsigned o){(void)k;(void)o;return hls_backend_start(u);}
void hls_backend_stop(void){}
int hls_backend_active(void){active_calls++;return active_calls>=3 && !__atomic_load_n(&audio_outputs,__ATOMIC_SEQ_CST);}
int hls_backend_error(void){return 0;}
const char *hls_backend_stage(void){return "AAC playback";}
uint64_t hls_backend_time(void){return got_audio?16:0;}
int hls_backend_video(hls_video_t *frame){(void)frame;return 0;}
int hls_backend_audio(hls_audio_t *frame){
 static short samples[711*2];if(got_audio)return 0;
 for(unsigned i=0;i<711*2;i++)samples[i]=123;
 *frame=(hls_audio_t){(unsigned char*)samples,2,44100,sizeof(samples),0};got_audio=1;return 1;
}
int hls_backend_audio_eof(void){return got_audio;}
int hls_backend_pause(int paused){(void)paused;return 0;}
int hls_backend_buffering(void){return 0;}
void hls_backend_diagnostics(char *out,unsigned cap){snprintf(out,cap,"mock decoder ready");}
void gui_player_end_frame(const unsigned *frame){(void)frame;}
void gui_skip_overlay(unsigned *frame,int credits){(void)frame;(void)credits;}
int sceKernelStartThread(int id,unsigned n,void *arg){(void)n;(void)arg;assert(id==101);thread_started=1;return pthread_create(&audio_mock,NULL,pump,NULL);}
int sceAudioOutOutput(int id,const void *data){assert(id==1 && data);const short *pcm=data;
 for(unsigned i=0;i<1024*2;i++)assert(pcm[i]==(i<711*2?123:0));
 __atomic_add_fetch(&audio_outputs,1,__ATOMIC_SEQ_CST);return 0;
}
int main(void){
 reset();audio_case=2;assert(!player_play_hls("http://mock/short.m3u8"));assert(!player_run_media("Short ending",16,0,1));
 assert(audio_outputs==1 && joined && !open_blocks() && player_completed() && player_position()==16);
 reset();audio_case=2;disconnected=1;assert(!player_play_hls("http://mock/disconnected.m3u8"));assert(player_run_media("Disconnected",30000,0,1)==-12);
 assert(!strcmp(player_error_stage(),"Wi-Fi disconnected") && !player_completed() && joined && !open_blocks());
 puts("Real player HLS dispatch: partial PCM tail padded and emitted, clean short EOF, Wi-Fi disconnect and cleanup passed");return 0;
}
