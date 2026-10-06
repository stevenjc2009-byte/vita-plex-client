#include "network.h"
#include "http.h"
#include "gui.h"
#include <stdio.h>
#include <string.h>
#ifdef __vita__
#include "performance.h"
#include <psp2/kernel/threadmgr.h>
static struct {const char *url,*client,*accept,*path;char *body;unsigned size;int mode,result;volatile int done;} job;
static int wants_exit,cancelled,last_result;static const char *stage="not started";
static int record_result(int result){last_result=result;if(result){FILE *log=fopen("ux0:data/plex-client/debug.log","a");if(log){fprintf(log,"network failure stage=%s result=%X http=%d detail=%X\n",stage,result,network_last_status(),network_last_error());fclose(log);}}return result;}
static int worker(SceSize size,void *arg){(void)size;(void)arg;
 job.result=http_init();stage=job.result<0?http_init_stage():"HTTP request";if(job.result>=0)job.result=job.mode==4?http_download_art(job.url,job.path,NULL):job.mode==3?http_put(job.url,job.client,job.body,job.size):job.mode==1?http_post_pins(job.url,job.client,job.body,job.size):job.mode==2?http_download(job.url,job.path,NULL):http_get(job.url,job.client,job.accept,job.body,job.size);
 __atomic_store_n(&job.done,1,__ATOMIC_RELEASE);return 0;}
static int perform(unsigned deadline){cancelled=0;if(job.body && job.size)job.body[0]=0;http_prepare(deadline);__atomic_store_n(&job.done,0,__ATOMIC_RELEASE);
 stage="network worker allocation";SceUID tid=performance_thread("plex_network",worker,0x10000130,0x10000,0x40000);if(tid<0){return record_result(tid);}
 stage="network worker start";int r=sceKernelStartThread(tid,0,NULL);if(r<0){sceKernelDeleteThread(tid);return record_result(r);}
 int action=gui_wait(&job.done,http_cancel);if(action==GUI_QUIT)wants_exit=1;
 sceKernelWaitThreadEnd(tid,NULL,NULL);sceKernelDeleteThread(tid);
 if(action==GUI_BACK || action==GUI_QUIT){cancelled=1;if(job.body && job.size)job.body[0]=0;last_result=-2;return -2;}return record_result(job.result);}

int network_get(const char *url,const char *client,const char *accept,char *body,unsigned size,unsigned deadline){job.url=url;job.client=client;job.accept=accept;job.body=body;job.size=size;job.mode=0;return perform(deadline);}
int network_pins(const char *url,const char *client,char *body,unsigned size){job.url=url;job.client=client;job.body=body;job.size=size;job.mode=1;return perform(15);}
int network_download(const char *url,const char *path){job.url=url;job.path=path;job.body=NULL;job.size=0;job.mode=2;return perform(180);}
int network_download_image(const char *url,const char *path){job.url=url;job.path=path;job.body=NULL;job.size=0;job.mode=4;return perform(20);}
int network_put(const char *url,const char *client,char *body,unsigned size){job.url=url;job.client=client;job.body=body;job.size=size;job.mode=3;return perform(10);}
void network_request_exit(void){wants_exit=1;}
int network_exit_requested(void){return wants_exit;}
int network_cancelled(void){return cancelled;}
int network_last_error(void){return !strcmp(stage,"HTTP request") && http_last_error()?http_last_error():last_result;}
int network_last_status(void){return !strcmp(stage,"HTTP request")?http_last_status():0;}
const char *network_last_stage(void){return stage;}
#else
int network_get(const char*u,const char*c,const char*a,char*b,unsigned s,unsigned d){(void)d;return http_get(u,c,a,b,s);}
int network_pins(const char*u,const char*c,char*b,unsigned s){return http_post_pins(u,c,b,s);}
int network_download(const char*u,const char*p){return http_download(u,p,NULL);}int network_exit_requested(void){return 0;}
void network_request_exit(void){}
int network_cancelled(void){return 0;}
int network_last_error(void){return http_last_error();}int network_last_status(void){return http_last_status();}const char *network_last_stage(void){return "HTTP request";}
int network_download_image(const char*u,const char*p){return http_download_art(u,p,NULL);}
int network_put(const char*u,const char*c,char*b,unsigned s){return http_put(u,c,b,s);}
#endif
