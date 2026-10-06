#include "video.h"
#include <string.h>
#include <limits.h>
#if defined(__vita__) && !defined(PLEX_MOCK_VITA)
#include <psp2/kernel/cpu.h>
#endif
static unsigned observed_cores;
unsigned video_observed_cores(void){return __atomic_load_n(&observed_cores,__ATOMIC_ACQUIRE);}
static int yy[256],rv[256],gu[256],gv[256],bu[256],tables;
static unsigned clip(int v){return v<0?0:v>255?255:(unsigned)v;}
int video_prepare(video_job_t *j,const unsigned char *src,unsigned w,unsigned h,float aspect,unsigned *dst) {
 if(!j || !src || !dst || !w || !h || (w&1) || (h&1) || w>2048 || h>2048)return -1;
 if(!tables){for(int i=0;i<256;i++){yy[i]=298*(i-16);rv[i]=409*(i-128);gu[i]=-100*(i-128);gv[i]=-208*(i-128);bu[i]=516*(i-128);}tables=1;}
 j->source=src;j->dest=dst;j->width=w;j->height=h;
 if(!(aspect>0.1f && aspect<10.0f))aspect=(float)w/h;
 j->out_width=960;j->out_height=(unsigned)(960/aspect);
 if(j->out_height>544){j->out_height=544;j->out_width=(unsigned)(544*aspect);}
 j->out_width&=~1u;j->out_height&=~1u;if(!j->out_width || !j->out_height)return -1;
 j->left=(960-j->out_width)/2;j->top=(544-j->out_height)/2;
 for(unsigned x=0;x<j->out_width;x++)j->x[x]=(unsigned short)(x*w/j->out_width);
 for(unsigned y=0;y<j->out_height;y++)j->y[y]=(unsigned short)(y*h/j->out_height);
 // Only clear letterbox regions, not the image that conversion overwrites.
 for(unsigned y=0;y<544;y++){
   unsigned *row=dst+y*960;
   if(y<j->top || y>=j->top+j->out_height)memset(row,0,960*4);
   else {memset(row,0,j->left*4);memset(row+j->left+j->out_width,0,(960-j->left-j->out_width)*4);}
 }
 return 0;
}
void video_rows(const video_job_t *j,unsigned begin,unsigned end) {
#if defined(__vita__) && !defined(PLEX_MOCK_VITA)
 int core=sceKernelGetCpuId();if(core>=0 && core<4)__atomic_fetch_or(&observed_cores,1u<<core,__ATOMIC_RELEASE);
#endif
 if(end>j->out_height)end=j->out_height;
 for(unsigned y=begin;y<end;y++){
  unsigned sy=j->y[y];const unsigned char *luma=j->source+sy*j->width;
  const unsigned char *vu=j->source+j->width*j->height+(sy/2)*j->width;
  unsigned *dst=j->dest+(j->top+y)*960+j->left;
  for(unsigned x=0;x<j->out_width;x++){
   unsigned sx=j->x[x],u=vu[(sx&~1u)+1],v=vu[sx&~1u];int c=yy[luma[sx]];
   dst[x]=clip((c+rv[v]+128)>>8)|(clip((c+gu[u]+gv[v]+128)>>8)<<8)|(clip((c+bu[u]+128)>>8)<<16)|0xFF000000u;
  }
 }
}
#if defined(__vita__) && !defined(PLEX_MOCK_VITA)
#include "performance.h"
#include <psp2/kernel/threadmgr.h>
typedef struct {SceUID thread,ready,done;const video_job_t *job;unsigned first,last;int stop;} worker_t;
static worker_t workers[3];static int worker_count;
static int convert_thread(SceSize size,void *arg){(void)size;worker_t *w=*(worker_t**)arg;
 for(;;){sceKernelWaitSema(w->ready,1,NULL);if(w->stop)break;video_rows(w->job,w->first,w->last);sceKernelSignalSema(w->done,1);}return 0;}
int video_pool_init(void){
 if(worker_count)return worker_count+1;__atomic_store_n(&observed_cores,0,__ATOMIC_RELEASE);
 for(int i=0;i<3;i++){
  worker_t *w=workers+i;memset(w,0,sizeof(*w));w->thread=w->ready=w->done=-1;
  w->ready=sceKernelCreateSema("plex_convert_ready",0,0,1,NULL);w->done=sceKernelCreateSema("plex_convert_done",0,0,1,NULL);
  if(w->ready>=0 && w->done>=0)w->thread=performance_thread("plex_convert",convert_thread,0x10000110,0x4000,0x10000<<(i+1));
  if(w->thread<0 || (i==2 && !performance_fourth_core()) || sceKernelStartThread(w->thread,sizeof(w),&w)<0){
   if(w->thread>=0)sceKernelDeleteThread(w->thread);if(w->ready>=0)sceKernelDeleteSema(w->ready);if(w->done>=0)sceKernelDeleteSema(w->done);break;
  }worker_count++;
 }return worker_count+1;
}
void video_convert(const video_job_t *j){unsigned bands=(unsigned)worker_count+1;
 for(int i=0;i<worker_count;i++){worker_t *w=workers+i;w->job=j;w->first=j->out_height*(i+1)/bands;w->last=j->out_height*(i+2)/bands;sceKernelSignalSema(w->ready,1);}
 video_rows(j,0,j->out_height/bands);
 for(int i=0;i<worker_count;i++)sceKernelWaitSema(workers[i].done,1,NULL);
}
void video_pool_shutdown(void){for(int i=0;i<worker_count;i++){worker_t *w=workers+i;w->stop=1;sceKernelSignalSema(w->ready,1);sceKernelWaitThreadEnd(w->thread,NULL,NULL);sceKernelDeleteThread(w->thread);sceKernelDeleteSema(w->ready);sceKernelDeleteSema(w->done);}worker_count=0;}
#else
int video_pool_init(void){return 1;}void video_convert(const video_job_t *j){video_rows(j,0,j->out_height);}void video_pool_shutdown(void){}
#endif
