#define _DEFAULT_SOURCE
#define main conversion_mock_main
#include "parallel_video_test.c"
#undef main
#include "network.h"
#include "gui.h"
#include <unistd.h>
static int init_error,request_error,wait_action;
int http_init(void){return init_error;}const char *http_init_stage(void){return "network stack";}void http_prepare(unsigned d){assert(d);}void http_cancel(void){}
int http_last_error(void){return request_error?-140:0;}int http_last_status(void){return 0;}
int http_get(const char*u,const char*c,const char*a,char*b,unsigned n){(void)u;(void)c;(void)a;snprintf(b,n,"response");return request_error?-1:0;}
int http_post_pins(const char*u,const char*c,char*b,unsigned n){return http_get(u,c,"",b,n);}int http_put(const char*u,const char*c,char*b,unsigned n){return http_get(u,c,"",b,n);}
int http_download(const char*u,const char*p,void(*cb)(unsigned,unsigned)){(void)u;(void)p;(void)cb;return 0;}int http_download_art(const char*u,const char*p,volatile int*c){(void)u;(void)p;(void)c;return 0;}
int gui_wait(volatile int *done,void(*cancel)(void)){if(wait_action)cancel();while(!__atomic_load_n(done,__ATOMIC_ACQUIRE))usleep(1000);return wait_action;}
int main(void){char body[128]="stale";deny_all=1;assert(network_get("http://test","id","xml",body,sizeof(body),5)==-91 && !body[0]);assert(network_last_error()==-91 && !strcmp(network_last_stage(),"network worker allocation"));deny_all=0;no_resources();
 start_error=-92;assert(network_get("http://test","id","xml",body,sizeof(body),5)==-92 && network_last_error()==-92);start_error=0;no_resources();
 init_error=-121;assert(network_get("http://test","id","xml",body,sizeof(body),5)==-121);assert(network_last_error()==-121 && !strcmp(network_last_stage(),"network stack") && !network_last_status());init_error=0;no_resources();
 request_error=1;assert(network_get("http://test","id","xml",body,sizeof(body),5)<0 && network_last_error()==-140);request_error=0;no_resources();
 wait_action=GUI_BACK;assert(network_get("http://test","id","xml",body,sizeof(body),5)==-2 && network_cancelled() && !body[0]);wait_action=0;no_resources();
 deny_custom_priority=1;assert(!network_get("http://test","id","xml",body,sizeof(body),5) && !strcmp(body,"response"));deny_custom_priority=0;no_resources();
 assert(!network_get("http://test","id","xml",body,sizeof(body),5) && !network_cancelled() && !strcmp(body,"response"));no_resources();puts("Network worker creation/start, initialization errors, diagnostic codes and cancellation tests passed");return 0;}
