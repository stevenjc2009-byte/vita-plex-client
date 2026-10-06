#include <psp2/mock.h>
#include "player.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static struct {void *ptr;unsigned size;} blocks[16];
static int source_fail,alloc_fail,thread_fail,never_active,initial_cross,cancel_at;
static int active_calls,frames,peeks,closed,starts,thread_started,joined;
static int audio_case,audio_outputs;
static int (*thread_fn)(SceSize,void*);
static void *decoder_memory,*generic_memory;
static SceAvPlayerInitData init_copy;
static int open_blocks(void){int n=0;for(int i=1;i<16;i++)n+=blocks[i].ptr!=NULL;return n;}
static void reset(void){assert(!open_blocks());source_fail=alloc_fail=thread_fail=never_active=initial_cross=cancel_at=0;
 active_calls=frames=peeks=closed=starts=thread_started=joined=audio_case=audio_outputs=0;decoder_memory=generic_memory=NULL;}
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
 if(audio_case)assert(thread_fn(0,NULL)==-31);return 0;}
int sceKernelWaitThreadEnd(int id,void *a,void *b){(void)a;(void)b;assert(id==101 && !closed);joined=1;return 0;}
int sceKernelDeleteThread(int id){assert(id==101);return 0;}
int sceKernelDelayThread(unsigned n){(void)n;return 0;}
int sceCtrlPeekBufferPositive(int port,SceCtrlData *pad,int count){(void)port;(void)count;peeks++;
 pad->buttons=cancel_at && peeks>=cancel_at?SCE_CTRL_CIRCLE:initial_cross?SCE_CTRL_CROSS:0;return 1;}
int sceDisplaySetFrameBuf(SceDisplayFrameBuf *f,int mode){(void)mode;assert(f->base);return 0;}
int sceDisplayWaitVblankStart(void){return 0;}
void psvDebugScreenInit(void){}
void gui_player_overlay(unsigned *b,const char *t,unsigned p,unsigned d,int paused,const char *m){(void)b;(void)t;(void)p;(void)d;(void)paused;(void)m;}
int sceAvPlayerInit(SceAvPlayerInitData *data){assert(data->autoStart==SCE_TRUE && data->numOutputVideoFrameBuffers>=2 && data->basePriority==125);
 assert(data->memoryReplacement.allocate && data->memoryReplacement.deallocate && data->memoryReplacement.allocateTexture && data->memoryReplacement.deallocateTexture);
 init_copy=*data;generic_memory=data->memoryReplacement.allocate(NULL,64,1024);decoder_memory=data->memoryReplacement.allocateTexture(NULL,16,800000);
 assert(generic_memory && decoder_memory);return 5;}
int sceAvPlayerAddSource(int handle,const char *url){assert(handle==5 && url[0]);return source_fail?-20:0;}
int sceAvPlayerStart(int h){(void)h;starts++;return -21;}
int sceAvPlayerIsActive(int h){assert(h==5);active_calls++;return !never_active && active_calls>=3 && frames<2;}
int sceAvPlayerGetVideoData(int h,SceAvPlayerFrameInfo *frame){assert(h==5);static unsigned char pixels[6]={128,128,128,128,128,128};
 frame->pData=pixels;frame->details.video.width=2;frame->details.video.height=2;frames++;return 1;}
int sceAvPlayerGetAudioData(int h,SceAvPlayerFrameInfo *f){(void)h;static short pcm[2048];if(!audio_case)return 0;
 f->pData=(unsigned char*)pcm;f->details.audio.channelCount=2;f->details.audio.sampleRate=44100;f->details.audio.size=sizeof(pcm);return 1;}
uint64_t sceAvPlayerCurrentTime(int h){assert(h==5);return frames*40;}
int sceAvPlayerPause(int h){(void)h;assert(!initial_cross);return 0;}
int sceAvPlayerResume(int h){(void)h;return 0;}
int sceAvPlayerJumpToTime(int h,uint64_t t){(void)h;(void)t;return 0;}
int sceAvPlayerStop(int h){assert(h==5);return 0;}
int sceAvPlayerClose(int h){assert(h==5);if(thread_started)assert(joined);closed++;
 init_copy.memoryReplacement.deallocate(NULL,generic_memory);init_copy.memoryReplacement.deallocateTexture(NULL,decoder_memory);return 0;}
int sceAudioOutOpenPort(int t,int n,int r,int m){assert(t==SCE_AUDIO_OUT_PORT_TYPE_BGM && n==1024 && r==44100 && m==SCE_AUDIO_OUT_MODE_STEREO);return 1;}
int sceAudioOutOutput(int id,const void *p){assert(id==1 && p);audio_outputs++;return -31;}
int sceAudioOutReleasePort(int id){(void)id;assert(joined);return 0;}
int main(void){
 reset();source_fail=1;assert(player_play_hls("http://mock/video.m3u8")<0);assert(closed==1 && !open_blocks() && starts==0);
 reset();assert(!player_play_hls("http://mock/video.m3u8"));initial_cross=1;assert(!player_run("Movie",100000,12000));
 assert(frames==2 && player_position()==12080 && closed==1 && joined && !open_blocks() && starts==0);
 reset();assert(!player_play_hls("http://mock/video.m3u8"));never_active=1;assert(player_run("Movie",100000,0)<0);assert(closed==1 && !thread_started && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));never_active=1;cancel_at=3;assert(player_run("Movie",100000,0)==-4);assert(closed==1 && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));alloc_fail=1;assert(player_run("Movie",100000,0)==-2);assert(closed==1 && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));thread_fail=1;assert(player_run("Movie",100000,0)<0);assert(closed==1 && !open_blocks());
 reset();assert(!player_play_hls("http://mock/video.m3u8"));audio_case=1;assert(player_run("Movie",100000,0)==-31);
 assert(audio_outputs==1 && closed==1 && joined && !open_blocks());
 player_stop();assert(!open_blocks());puts("Player lifecycle tests passed (mock Vita APIs)");return 0;
}
