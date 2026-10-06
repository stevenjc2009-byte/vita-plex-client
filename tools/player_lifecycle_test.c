#include <psp2/mock.h>
#include "player.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
static struct {void *ptr;unsigned size;} blocks[16];
static int init_fail;static int mock_handle=(int)0x81400280u;
static int source_fail,alloc_fail,thread_fail,never_active,initial_cross,cancel_at;
static int active_calls,frames,peeks,closed,starts,thread_started,joined;
static int audio_case,audio_outputs,scenario,overlay_seen;
static unsigned last_pixel;
static uint64_t time_us;static pthread_t audio_mock;static int resume_given;
uint64_t sceKernelGetProcessTimeWide(void){return __atomic_add_fetch(&time_us,scenario==3?1000000:100,__ATOMIC_SEQ_CST);}
void performance_poll(void){}int performance_take_resume(void){if(scenario==6 && peeks>=4 && !resume_given){resume_given=1;return 1;}return 0;}
SceUID performance_thread(const char*n,SceKernelThreadEntry fn,int p,unsigned stack,int affinity){assert(affinity==0x20000);return sceKernelCreateThread(n,fn,p,(int)stack,0,affinity,NULL);}
static int (*thread_fn)(SceSize,void*);
static void *pump(void *arg){(void)arg;thread_fn(0,NULL);return NULL;}
static void *decoder_memory,*generic_memory;
static SceAvPlayerInitData init_copy;
static int open_blocks(void){int n=0;for(int i=1;i<16;i++)n+=blocks[i].ptr!=NULL;return n;}
static void reset(void){assert(!open_blocks());init_fail=0;source_fail=alloc_fail=thread_fail=never_active=initial_cross=cancel_at=0;
 active_calls=frames=peeks=closed=starts=thread_started=joined=audio_case=audio_outputs=scenario=overlay_seen=0;last_pixel=0;resume_given=0;player_start_paused(0);decoder_memory=generic_memory=NULL;}
void *mock_memalign(unsigned a,unsigned n){assert(a>=sizeof(void*) && !(a&(a-1)));return malloc(n);}
int sceIoOpen(const char *p,int f,int m){(void)p;(void)f;(void)m;return -1;}
int sceIoWrite(int f,const void *p,unsigned n){(void)f;(void)p;return (int)n;}
int sceSysmoduleLoadModule(int m){(void)m;return 0;}
int sceKernelAllocMemBlock(const char *name,int type,unsigned size,SceKernelAllocMemBlockOpt *opt){
 (void)type;(void)opt;assert(!(size%0x40000));if(alloc_fail && !strcmp(name,"plex_video"))return -12;
 for(int i=1;i<16;i++)if(!blocks[i].ptr){blocks[i].ptr=calloc(1,size);blocks[i].size=size;return i;}return -1;}
int sceKernelGetMemBlockBase(int id,void **out){*out=blocks[id].ptr;return *out?0:-1;}
int sceKernelFreeMemBlock(int id){assert(blocks[id].ptr);free(blocks[id].ptr);blocks[id].ptr=NULL;return 0;}
int sceKernelFindMemBlockByAddr(void *p,unsigned n){(void)n;for(int i=1;i<16;i++)if(blocks[i].ptr==p)return i;return -1;}
int sceKernelCreateThread(const char *n,int(*fn)(SceSize,void*),int p,int s,int a,int c,void *o){
 (void)n;(void)p;(void)s;(void)a;(void)c;(void)o;thread_fn=fn;return thread_fail?-13:101;}
int sceKernelStartThread(int id,unsigned n,void *p){(void)n;(void)p;assert(id==101 && active_calls>=3);thread_started=1;
 if(audio_case==1)assert(thread_fn(0,NULL)==-31);if(audio_case==2)assert(!pthread_create(&audio_mock,NULL,pump,NULL));return 0;}
int sceKernelWaitThreadEnd(int id,void *a,void *b){(void)a;(void)b;assert(id==101 && !closed);if(audio_case==2)assert(!pthread_join(audio_mock,NULL));joined=1;return 0;}
int sceKernelDeleteThread(int id){assert(id==101);return 0;}
int sceKernelDelayThread(unsigned n){if(audio_case==2){struct timespec delay={0,(long)n*1000};nanosleep(&delay,NULL);}return 0;}
int sceCtrlPeekBufferPositive(int port,SceCtrlData *pad,int count){(void)port;(void)count;peeks++;
 if(scenario==5){pad->buttons=peeks>=30?SCE_CTRL_CIRCLE:0;return 1;}
 if(scenario==1){pad->buttons=peeks==4?SCE_CTRL_LEFT:0;return 1;}
 if(scenario==2){pad->buttons=peeks==4?SCE_CTRL_CROSS:peeks==6?SCE_CTRL_TRIANGLE:peeks>=8?SCE_CTRL_CIRCLE:0;return 1;}
 pad->buttons=cancel_at && peeks>=cancel_at?SCE_CTRL_CIRCLE:initial_cross?SCE_CTRL_CROSS:0;return 1;}
int sceDisplaySetFrameBuf(SceDisplayFrameBuf *f,int mode){(void)mode;assert(f->base);last_pixel=*(unsigned*)f->base;if(last_pixel==0xDEADBEEF)overlay_seen=1;return 0;}
int sceDisplayWaitVblankStart(void){if(audio_case==2){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}return 0;}
void psvDebugScreenInit(void){}
void gui_player_overlay(unsigned *b,const char *t,unsigned p,unsigned d,int paused,const char *m){b[0]=0xDEADBEEF;(void)t;(void)p;(void)d;(void)paused;(void)m;}
int sceAvPlayerInit(SceAvPlayerInitData *data){assert(data->autoStart==SCE_TRUE && data->numOutputVideoFrameBuffers>=2 && data->basePriority==125);
 assert(data->memoryReplacement.allocate && data->memoryReplacement.deallocate && data->memoryReplacement.allocateTexture && data->memoryReplacement.deallocateTexture);
 if(init_fail)return init_fail==1?0:(int)0x806A0003u;
 init_copy=*data;generic_memory=data->memoryReplacement.allocate(NULL,64,1024);decoder_memory=data->memoryReplacement.allocateTexture(NULL,16,800000);
 assert(generic_memory && decoder_memory);return mock_handle;}
int sceAvPlayerAddSource(int handle,const char *url){assert(handle==mock_handle && url[0]);return source_fail?-20:0;}
int sceAvPlayerStart(int h){(void)h;starts++;return -21;}
int sceAvPlayerIsActive(int h){assert(h==mock_handle);active_calls++;return !never_active && active_calls>=3 && (scenario==2 || scenario==3 || scenario==4 || scenario==6 || frames<2);}
int sceAvPlayerGetVideoData(int h,SceAvPlayerFrameInfo *frame){assert(h==mock_handle);static unsigned char pixels[6]={128,128,128,128,128,128};
 if((scenario==2 || scenario==3 || scenario==6) && frames)return 0;
 frame->pData=pixels;frame->details.video.width=scenario==4 && frames?3:2;frame->details.video.height=2;frames++;return 1;}
int sceAvPlayerGetAudioData(int h,SceAvPlayerFrameInfo *f){(void)h;static short pcm[2048];if(!audio_case || (audio_case==2 && __atomic_load_n(&audio_outputs,__ATOMIC_SEQ_CST)))return 0;
 f->pData=(unsigned char*)pcm;f->details.audio.channelCount=2;f->details.audio.sampleRate=44100;f->details.audio.size=sizeof(pcm);return 1;}
uint64_t sceAvPlayerCurrentTime(int h){assert(h==mock_handle);return (audio_case==2?__atomic_load_n(&audio_outputs,__ATOMIC_SEQ_CST):frames)*40;}
int sceAvPlayerPause(int h){(void)h;assert(!initial_cross);return 0;}
int sceAvPlayerResume(int h){(void)h;return 0;}
int sceAvPlayerJumpToTime(int h,uint64_t t){(void)h;(void)t;return 0;}
int sceAvPlayerStop(int h){assert(h==mock_handle);return 0;}
int sceAvPlayerClose(int h){assert(h==mock_handle);if(thread_started)assert(joined);closed++;
 init_copy.memoryReplacement.deallocate(NULL,generic_memory);init_copy.memoryReplacement.deallocateTexture(NULL,decoder_memory);return 0;}
int sceAudioOutOpenPort(int t,int n,int r,int m){assert(t==SCE_AUDIO_OUT_PORT_TYPE_BGM && n==1024 && r==44100 && m==SCE_AUDIO_OUT_MODE_STEREO);return 1;}
int sceAudioOutOutput(int id,const void *p){assert(id==1 && p);__atomic_add_fetch(&audio_outputs,1,__ATOMIC_SEQ_CST);return audio_case==2?0:-31;}
int sceAudioOutReleasePort(int id){(void)id;assert(joined);return 0;}
int main(void){
 reset();mock_handle=(int)0x814001E0u;assert(!player_play_hls("http://mock/video.m3u8"));player_stop();assert(closed==1 && !open_blocks());mock_handle=(int)0x81400280u;
 reset();init_fail=1;assert(player_play_hls("http://mock/video.m3u8")== (int)0x806A0003u);assert(!closed && !player_active() && !open_blocks());player_stop();
 reset();init_fail=2;assert(player_play_hls("http://mock/video.m3u8")== (int)0x806A0003u);assert(!closed && !open_blocks());player_stop();

 reset();source_fail=1;assert(player_play_hls("http://mock/video.m3u8")<0);assert(closed==1 && !open_blocks() && starts==0);
 reset();assert(!player_play_hls("http://mock/video.m3u8"));initial_cross=1;assert(!player_run("Movie",12100,12000));
 assert(frames==2 && player_position()==12080 && closed==1 && joined && !open_blocks() && starts==0);
 reset();source_fail=1;assert(player_play_hls("http://mock/video.m3u8")<0);assert(player_position()==0);
 reset();assert(!player_play_hls("http://mock/video.m3u8"));never_active=1;assert(player_run("Movie",100000,0)<0);assert(closed==1 && !thread_started && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));never_active=1;cancel_at=3;assert(player_run("Movie",100000,0)==-4);assert(closed==1 && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));alloc_fail=1;assert(player_run("Movie",100000,0)==-2);assert(closed==1 && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));thread_fail=1;assert(player_run("Movie",100000,0)<0);assert(closed==1 && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));audio_case=1;assert(player_run("Movie",100000,0)==-31);
 assert(audio_outputs==1 && closed==1 && joined && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));scenario=1;assert(player_run("Movie",12100,12000)==2);
 assert(player_seek_position()==2040 && player_position()==12040 && !player_completed() && joined && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));scenario=2;assert(!player_run("Movie",100000,0));
 assert(overlay_seen && last_pixel==0 && !player_completed() && joined && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));scenario=3;assert(player_run("Stall",100000,0)==-5);assert(frames==1 && !player_completed() && joined && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));scenario=4;assert(player_run("Invalid frame",100000,0)==-7);assert(!player_completed() && joined && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));audio_case=2;scenario=5;assert(!player_run_media("Music",100000,12000,1));assert(!frames && audio_outputs==1 && player_position()==12040 && !player_completed() && joined && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));assert(player_run("Truncated",100000,0)==-8 && !player_completed() && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));scenario=6;player_start_paused(1);assert(player_run("Suspend / resume",100000,12000)==2);assert(player_seek_position()==12040 && player_was_paused() && !player_completed() && !open_blocks());
 player_stop();assert(!open_blocks());reset();mock_handle=5;assert(!player_play_hls("http://mock/video.m3u8"));player_stop();assert(closed==1 && !open_blocks());puts("Player lifecycle tests passed (native high-bit and emulator handles, mock Vita APIs)");return 0;
}
