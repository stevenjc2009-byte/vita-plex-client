#include <psp2/mock.h>
#include "video.h"
#include "performance.h"
#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static struct {pthread_mutex_t mutex;pthread_cond_t cond;int count,used;} semas[16];
static struct {pthread_t thread;SceKernelThreadEntry entry;char arg[64];unsigned size;int used,started,affinity;} threads[8];
static int deny_extra,deny_500,cpu=333,gpu=111,requested[4];
int sceKernelCreateSema(const char*n,unsigned a,int value,int max,void*opt){(void)n;(void)a;(void)opt;assert(max==1);for(int i=0;i<16;i++)if(!semas[i].used){semas[i].used=1;semas[i].count=value;pthread_mutex_init(&semas[i].mutex,NULL);pthread_cond_init(&semas[i].cond,NULL);return 200+i;}return -1;}
int sceKernelWaitSema(int id,int value,unsigned*timeout){(void)timeout;assert(value==1);int i=id-200;pthread_mutex_lock(&semas[i].mutex);while(!semas[i].count)pthread_cond_wait(&semas[i].cond,&semas[i].mutex);semas[i].count--;pthread_mutex_unlock(&semas[i].mutex);return 0;}
int sceKernelSignalSema(int id,int value){assert(value==1);int i=id-200;pthread_mutex_lock(&semas[i].mutex);if(semas[i].count<1)semas[i].count++;pthread_cond_signal(&semas[i].cond);pthread_mutex_unlock(&semas[i].mutex);return 0;}
int sceKernelDeleteSema(int id){int i=id-200;assert(semas[i].used);pthread_mutex_destroy(&semas[i].mutex);pthread_cond_destroy(&semas[i].cond);semas[i].used=0;return 0;}
int sceKernelCreateThread(const char*n,SceKernelThreadEntry fn,int p,int stack,int attr,int affinity,void*opt){(void)n;(void)p;(void)stack;(void)attr;(void)opt;
 if(affinity==0x80000 && deny_extra)return -99;
 for(int i=0;i<8;i++)if(!threads[i].used){threads[i].used=1;threads[i].started=0;threads[i].entry=fn;threads[i].affinity=affinity;for(int core=0;core<4;core++)if(affinity==(0x10000<<core))requested[core]++;return 100+i;}return -1;}
static void *thread_main(void *arg){int i=(int)(size_t)arg;threads[i].entry(threads[i].size,threads[i].arg);return NULL;}
int sceKernelStartThread(int id,unsigned size,void*arg){int i=id-100;assert(size<=sizeof(threads[i].arg));threads[i].size=size;memcpy(threads[i].arg,arg,size);threads[i].started=1;return pthread_create(&threads[i].thread,NULL,thread_main,(void*)(size_t)i);}
int sceKernelWaitThreadEnd(int id,void*a,void*b){(void)a;(void)b;return pthread_join(threads[id-100].thread,NULL);}
int sceKernelDeleteThread(int id){assert(threads[id-100].used);threads[id-100].used=0;return 0;}
int scePowerGetArmClockFrequency(void){return cpu;}int scePowerGetGpuClockFrequency(void){return gpu;}
int scePowerSetArmClockFrequency(int n){if(n==500 && deny_500)return -1;cpu=n;return 0;}
int scePowerSetGpuClockFrequency(int n){gpu=n;return 0;}
static void no_resources(void){for(int i=0;i<16;i++)assert(!semas[i].used);for(int i=0;i<8;i++)assert(!threads[i].used);}
int main(void){
 unsigned char *src=malloc(64*36*3/2);unsigned *serial=calloc(960*544,4),*parallel=calloc(960*544,4);assert(src && serial && parallel);
 for(int i=0;i<64*36*3/2;i++)src[i]=(unsigned char)(i*31+17);
 video_job_t a,b;assert(!video_prepare(&a,src,64,36,16.0f/9,serial));assert(!video_prepare(&b,src,64,36,16.0f/9,parallel));
 video_rows(&a,0,a.out_height);assert(video_pool_init()==4);assert(performance_fourth_core());assert(requested[1] && requested[2] && requested[3]);
 for(int i=0;i<40;i++){video_convert(&b);assert(!memcmp(serial,parallel,960*544*4));}
 video_pool_shutdown();no_resources();deny_extra=1;assert(video_pool_init()==4 && !performance_fourth_core());video_convert(&b);assert(!memcmp(serial,parallel,960*544*4));video_pool_shutdown();no_resources();
 assert(video_prepare(&a,src,63,36,0,serial)<0);assert(!video_prepare(&a,src,64,36,1.0f,serial));assert(a.out_width==544 && a.out_height==544 && a.left==208);
 performance_apply(2);assert(cpu==500 && gpu==222);performance_restore();assert(cpu==333 && gpu==111);
 deny_500=1;performance_apply(2);assert(cpu==444 && gpu==222);performance_restore();assert(cpu==333 && gpu==111);
 free(src);free(serial);free(parallel);puts("Parallel conversion matches serial output; core 1/2/3 affinity, fourth-core fallback and clock restore tests passed");return 0;
}
