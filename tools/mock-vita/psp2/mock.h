#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int SceUID;
typedef unsigned SceSize;
typedef int SceAvPlayerHandle;
typedef struct {unsigned buttons;} SceCtrlData;
typedef struct {unsigned size;void *base;unsigned pitch,pixelformat,width,height;} SceDisplayFrameBuf;
typedef struct {unsigned size,attr,alignment;} SceKernelAllocMemBlockOpt;
typedef struct {unsigned width,height;} MockVideo;
typedef struct {unsigned channelCount,sampleRate,size;} MockAudio;
typedef struct {unsigned char *pData;union{MockVideo video;MockAudio audio;} details;} SceAvPlayerFrameInfo;
typedef struct {
  struct {void *objectPointer;void *(*allocate)(void*,uint32_t,uint32_t);void(*deallocate)(void*,void*);
    void *(*allocateTexture)(void*,uint32_t,uint32_t);void(*deallocateTexture)(void*,void*);} memoryReplacement;
  struct {void *objectPointer;void(*eventCallback)(void*,int32_t,int32_t,void*);} eventReplacement;
  unsigned basePriority;int numOutputVideoFrameBuffers,autoStart,debugLevel;const char *defaultLanguage;
} SceAvPlayerInitData;
#define SCE_TRUE 1
#define SCE_FALSE 0
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 1
#define SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 0
#define SCE_DISPLAY_SETBUF_NEXTFRAME 1
#define SCE_SYSMODULE_AVPLAYER 1
#define SCE_O_WRONLY 1
#define SCE_O_CREAT 2
#define SCE_O_APPEND 4
#define SCE_CTRL_CROSS 1
#define SCE_CTRL_CIRCLE 2
#define SCE_CTRL_START 4
#define SCE_CTRL_TRIANGLE 8
#define SCE_CTRL_LEFT 16
#define SCE_CTRL_RIGHT 32
#define SCE_AUDIO_OUT_MODE_STEREO 2
#define SCE_AUDIO_OUT_MODE_MONO 1
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 1
#define SCE_AUDIO_OUT_PORT_TYPE_BGM 2
void *mock_memalign(unsigned,unsigned);
int sceIoOpen(const char*,int,int);
int sceIoWrite(int,const void*,unsigned);
int sceSysmoduleLoadModule(int);
int sceKernelAllocMemBlock(const char*,int,unsigned,SceKernelAllocMemBlockOpt*);
int sceKernelGetMemBlockBase(int,void**);
int sceKernelFreeMemBlock(int);
int sceKernelFindMemBlockByAddr(void*,unsigned);
int sceKernelCreateThread(const char*,int(*)(SceSize,void*),int,int,int,int,void*);
int sceKernelStartThread(int,unsigned,void*);
int sceKernelWaitThreadEnd(int,void*,void*);
int sceKernelDeleteThread(int);
int sceKernelDelayThread(unsigned);
int sceCtrlPeekBufferPositive(int,SceCtrlData*,int);
int sceDisplaySetFrameBuf(SceDisplayFrameBuf*,int);
int sceDisplayWaitVblankStart(void);
int sceAvPlayerInit(SceAvPlayerInitData*);
int sceAvPlayerAddSource(int,const char*);
int sceAvPlayerStart(int);
int sceAvPlayerIsActive(int);
int sceAvPlayerGetVideoData(int,SceAvPlayerFrameInfo*);
int sceAvPlayerGetAudioData(int,SceAvPlayerFrameInfo*);
uint64_t sceAvPlayerCurrentTime(int);
int sceAvPlayerPause(int);
int sceAvPlayerResume(int);
int sceAvPlayerJumpToTime(int,uint64_t);
int sceAvPlayerStop(int);
int sceAvPlayerClose(int);
int sceAudioOutOpenPort(int,int,int,int);
int sceAudioOutOutput(int,const void*);
int sceAudioOutReleasePort(int);
