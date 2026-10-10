#include <psp2/http_mock.h>
#include "http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int step,fail_step,modules,blocks,mutexes,net,ctl,http,ssl,templates,connections,requests,files;
static int external_modules;
int sceSysmoduleIsLoaded(int module){(void)module;return external_modules?0:-1;}
static int length_delta,media_abort_read;static int declared,status=200,cancel_read,aborted,removed,close_fail,read_at;static unsigned payload_size,written;static uint64_t ticks;
const unsigned char plex_ca_root[]={0},plex_ca_int[]={0},plex_ca_isrg[]={0};const unsigned plex_ca_root_len=1,plex_ca_int_len=1,plex_ca_isrg_len=1;
const unsigned char plex_ca_github_r46[]={0},plex_ca_github_r36[]={0},plex_ca_github_ecc[]={0};const unsigned plex_ca_github_r46_len=1,plex_ca_github_r36_len=1,plex_ca_github_ecc_len=1;
static int fail(void){return ++step==fail_step;}
static void empty(void){assert(!modules && !blocks && !mutexes && !net && !ctl && !http && !ssl && !templates && !connections && !requests && !files);}
int sceKernelCreateMutex(const char*n,unsigned a,int c,void*o){(void)n;(void)a;(void)c;(void)o;if(fail())return -1;mutexes++;return 1;}
int sceKernelLockMutex(int i,int n,void*p){(void)i;(void)n;(void)p;return 0;}int sceKernelUnlockMutex(int i,int n){(void)i;(void)n;return 0;}int sceKernelDeleteMutex(int i){(void)i;mutexes--;return 0;}
int sceSysmoduleLoadModule(int i){(void)i;if(fail())return -1;modules++;return 0;}int sceSysmoduleUnloadModule(int i){(void)i;modules--;return 0;}
int sceKernelAllocMemBlock(const char*n,int t,unsigned s,SceKernelAllocMemBlockOpt*o){(void)n;(void)t;(void)s;(void)o;if(fail())return -1;blocks++;return 1;}
int sceKernelGetMemBlockBase(int i,void**p){(void)i;if(fail())return -1;static char memory;*p=&memory;return 0;}int sceKernelFreeMemBlock(int i){(void)i;blocks--;return 0;}
int sceNetInit(SceNetInitParam*p){assert(p->memory);if(fail())return -1;net++;return 0;}int sceNetTerm(void){net--;return 0;}
int sceNetCtlInit(void){if(fail())return -1;ctl++;return 0;}int sceNetCtlTerm(void){ctl--;return 0;}
int sceHttpInit(unsigned n){(void)n;if(fail())return -1;http++;return 0;}int sceHttpTerm(void){http--;return 0;}
int sceSslInit(unsigned n){(void)n;if(fail())return -1;ssl++;return 0;}int sceSslTerm(void){ssl--;return 0;}
int sceHttpsLoadCert(int n,const SceHttpsData**c,void*a,void*b){(void)c;(void)a;(void)b;assert(n==6);return fail()?-1:0;}
uint64_t sceKernelGetProcessTimeWide(void){ticks+=100;return ticks;}
int sceHttpCreateTemplate(const char*n,int v,int a){(void)n;(void)v;(void)a;templates++;return 10;}
int sceHttpCreateConnectionWithURL(int i,const char*u,int a){(void)i;(void)u;(void)a;connections++;return 11;}
int sceHttpCreateRequestWithURL(int i,int m,const char*u,unsigned n){(void)i;(void)m;(void)u;(void)n;requests++;read_at=0;return 12;}
int sceHttpAddRequestHeader(int i,const char*n,const char*v,int a){(void)i;(void)n;(void)v;(void)a;return 0;}
#define TIMEOUT(fn) int fn(int i,unsigned n){(void)i;assert(n>0);return 0;}
TIMEOUT(sceHttpSetResolveTimeOut) TIMEOUT(sceHttpSetConnectTimeOut) TIMEOUT(sceHttpSetSendTimeOut) TIMEOUT(sceHttpSetRecvTimeOut)
int sceHttpSendRequest(int i,const void*p,unsigned n){(void)i;(void)p;(void)n;return 0;}
int sceHttpsGetSslError(int i,int*e,unsigned*d){(void)i;*e=0;*d=0;return 0;}
int sceHttpGetStatusCode(int i,int*s){(void)i;*s=status;return 0;}
int sceHttpReadData(int i,void*p,unsigned n){(void)i;if(media_abort_read){http_media_abort();return -4;}if(cancel_read){http_cancel();return -4;}unsigned remaining=payload_size-(unsigned)read_at;if(n>remaining)n=remaining;memset(p,'A',n);read_at+=(int)n;return (int)n;}
int sceHttpAbortRequest(int i){assert(i==12);aborted++;return 0;}int sceHttpDeleteRequest(int i){(void)i;requests--;return 0;}int sceHttpDeleteConnection(int i){(void)i;connections--;return 0;}int sceHttpDeleteTemplate(int i){(void)i;templates--;return 0;}
int sceHttpSetAutoRedirect(int i,int v){(void)i;(void)v;return 0;}int sceHttpGetResponseContentLength(int i,unsigned long long*n){(void)i;*n=payload_size+length_delta;return declared?0:-1;}
int sceIoOpen(const char*p,int f,int m){(void)p;(void)f;(void)m;files++;return 20;}
int sceIoWrite(int i,const void*p,unsigned n){assert(i==20 && p);written+=n;return (int)n;}
int sceIoClose(int i){assert(i==20);files--;return close_fail?-1:0;}int sceIoRemove(const char*p){assert(p);removed++;return 0;}
static const char *range_header="bytes 0-99/200";
int sceHttpGetAllResponseHeaders(int id,char **out,unsigned *len){(void)id;*out=(char*)range_header;*len=(unsigned)strlen(range_header);return 0;}
int sceHttpParseResponseHeader(const char *headers,unsigned len,const char *name,const char **out,unsigned *size){assert(!strcmp(name,"Content-Range"));*out=headers;*size=len;return 0;}
int main(void){
 for(int fault=1;fault<=12;fault++){step=0;fail_step=fault;int result=http_init();if(fault<=8){assert(result<0);empty();}else {assert(!result);char body[16];payload_size=10;http_prepare(5);assert(!http_get("http://test","id","xml",body,sizeof(body)));assert(http_get("https://test","id","xml",body,sizeof(body))<0 && http_last_error()==-1);http_shutdown();empty();}}
 step=fail_step=0;assert(!http_init());char body[16];http_prepare(5);payload_size=10;assert(!http_get("http://test","id","text/xml",body,sizeof(body)) && strlen(body)==10);
 http_prepare(5);payload_size=30;assert(http_get("http://test","id","text/xml",body,sizeof(body))<0 && !body[0]);
 http_prepare(5);http_cancel();assert(http_get("http://test","id","text/xml",body,sizeof(body))<0 && !body[0]);
 http_prepare(5);cancel_read=1;assert(http_get("http://test","id","text/xml",body,sizeof(body))<0 && aborted && !body[0]);cancel_read=0;
 volatile int cancel=0;payload_size=600*1024;declared=1;http_prepare(5);assert(http_download_art("http://test","poster.jpg",&cancel)<0 && !files && !written);
 declared=0;http_prepare(5);assert(http_download_art("http://test","poster.jpg",&cancel)<0 && written<=512*1024 && removed==1 && !files);
 written=0;payload_size=100;close_fail=1;http_prepare(5);assert(http_download_art("http://test","poster.jpg",&cancel)<0 && removed==2 && !files);

 unsigned used=99;unsigned char media[256];declared=1;payload_size=100;close_fail=0;
 http_cancel();assert(!http_media_fetch("http://test",media,sizeof(media),&used,&cancel) && used==100); // Independent of metadata cancellation.
 declared=1;length_delta=10;http_prepare(5);assert(http_get("http://test","id","xml",(char*)media,sizeof(media))<0 && !media[0] && http_last_error()==-10);length_delta=0;
 length_delta=10;assert(http_media_fetch("http://test",media,sizeof(media),&used,&cancel)==-10 && !used);length_delta=0;
 payload_size=300;assert(http_media_fetch("http://test",media,sizeof(media),&used,&cancel)==-9 && !used);
 declared=0;assert(http_media_fetch("http://test",media,sizeof(media),&used,&cancel)==-9 && !used);
 payload_size=100;status=403;assert(http_media_fetch("http://test",media,sizeof(media),&used,&cancel)==-403 && !used);status=200;
 cancel=1;assert(http_media_fetch("http://test",media,sizeof(media),&used,&cancel)==-2 && !used);cancel=0;
 media_abort_read=1;int before=aborted;assert(http_media_fetch("http://test",media,sizeof(media),&used,&cancel)<0 && aborted>before);media_abort_read=0;
 uint64_t total=0;status=206;assert(!http_media_range("http://test",media,sizeof(media),&used,0,&total,&cancel) && used==100 && total==200);range_header="bytes 1-100/200";assert(http_media_range("http://test",media,sizeof(media),&used,0,&total,&cancel)==-13 && !used);status=200;
 http_shutdown();empty();external_modules=1;step=fail_step=0;assert(!http_init() && !modules);http_shutdown();empty();puts("HTTP initialization unwind, cancellation, truncation, download bounds and close failure tests passed");return 0;
}
