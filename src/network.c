#include "network.h"
#include "http.h"
#include "gui.h"
#ifdef __vita__
#include "performance.h"
#include <psp2/kernel/threadmgr.h>
static struct {const char *url,*client,*accept,*path;char *body;unsigned size;int mode,result;volatile int done;} job;
static int wants_exit;
static int worker(SceSize size,void *arg){(void)size;(void)arg;
 job.result=job.mode==3?http_put(job.url,job.client,job.body,job.size):job.mode==1?http_post_pins(job.url,job.client,job.body,job.size):job.mode==2?http_download(job.url,job.path,NULL):http_get(job.url,job.client,job.accept,job.body,job.size);
 __atomic_store_n(&job.done,1,__ATOMIC_RELEASE);return 0;}
static int perform(unsigned deadline){http_prepare(deadline);__atomic_store_n(&job.done,0,__ATOMIC_RELEASE);
 SceUID tid=performance_thread("plex_network",worker,0x10000130,0x10000,0x40000);if(tid<0)return tid;
 int r=sceKernelStartThread(tid,0,NULL);if(r<0){sceKernelDeleteThread(tid);return r;}
 int action=gui_wait(&job.done,http_cancel);if(action==GUI_QUIT)wants_exit=1;
 sceKernelWaitThreadEnd(tid,NULL,NULL);sceKernelDeleteThread(tid);
 if(action==GUI_BACK || action==GUI_QUIT){if(job.body && job.size)job.body[0]=0;return -2;}return job.result;}
int network_get(const char *url,const char *client,const char *accept,char *body,unsigned size,unsigned deadline){job.url=url;job.client=client;job.accept=accept;job.body=body;job.size=size;job.mode=0;return perform(deadline);}
int network_pins(const char *url,const char *client,char *body,unsigned size){job.url=url;job.client=client;job.body=body;job.size=size;job.mode=1;return perform(15);}
int network_download(const char *url,const char *path){job.url=url;job.path=path;job.body=NULL;job.size=0;job.mode=2;return perform(180);}
int network_put(const char *url,const char *client,char *body,unsigned size){job.url=url;job.client=client;job.body=body;job.size=size;job.mode=3;return perform(10);}
int network_exit_requested(void){return wants_exit;}
#else
int network_get(const char*u,const char*c,const char*a,char*b,unsigned s,unsigned d){(void)d;return http_get(u,c,a,b,s);}
int network_pins(const char*u,const char*c,char*b,unsigned s){return http_post_pins(u,c,b,s);}
int network_download(const char*u,const char*p){return http_download(u,p,NULL);}int network_exit_requested(void){return 0;}
int network_put(const char*u,const char*c,char*b,unsigned s){return http_put(u,c,b,s);}
#endif
