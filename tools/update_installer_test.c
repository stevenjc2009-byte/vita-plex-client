#include "update.h"
#include "package.h"
#include <psp2/update_mock.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static char future_version[16],future_tag[20];
static int fault,preloaded,loads,unloads,init,closed,promoted,exited,restored,shutdowns;
FILE *update_test_fopen(const char *path,const char *mode){if(strstr(path,"ux0:app/")){if(fault==2)return NULL;FILE *f=tmpfile();assert(f);unsigned char header[512]={0};memcpy(header+0x37,"PLEX00001",9);assert(fwrite(header,1,sizeof(header),f)==sizeof(header));rewind(f);return f;}assert(strstr(path,"update-stage/sce_sys/package/head.bin") && !strcmp(mode,"wb"));return tmpfile();}
int package_extract(const char*a,const char*d,const char*v){assert(!strcmp(a,UPDATE_VPK_PATH) && !strcmp(d,"ux0:data/plex-client/update-stage") && !strcmp(v,future_version));return fault==1?-1:0;}
int sceIoGetstat(const char*p,SceIoStat*s){(void)p;(void)s;return -1;}int sceIoDopen(const char*p){assert(p);return -1;}int sceIoDread(int d,SceIoDirent*e){(void)d;(void)e;return 0;}int sceIoDclose(int d){(void)d;return 0;}int sceIoRmdir(const char*p){(void)p;return 0;}int sceIoRemove(const char*p){(void)p;return 0;}int sceIoMkdir(const char*p,int m){(void)p;(void)m;return 0;}
int sceSysmoduleIsLoadedInternal(int id){assert(id==8 || id==24);return preloaded?0:-1;}
int sceSysmoduleLoadModuleInternalWithArg(int id,unsigned n,void*a,const SceSysmoduleOpt*o){assert(id==8 && n==24 && a && o && o->result);loads++;return fault==3?-123:0;}
int sceSysmoduleUnloadModuleInternalWithArg(int id,unsigned n,void*a,const SceSysmoduleOpt*o){assert(id==8 && !n && !a && o);unloads++;return 0;}
int sceSysmoduleLoadModuleInternal(int id){assert(id==24);loads++;return fault==4?-124:0;}int sceSysmoduleUnloadModuleInternal(int id){assert(id==24);unloads++;return 0;}
int scePromoterUtilityInit(void){init++;return fault==5?-125:0;}int scePromoterUtilityExit(void){closed++;return 0;}
int scePromoterUtilityPromotePkgWithRif(const char *path,int sync){assert(!strcmp(path,"ux0:data/plex-client/update-stage") && sync==1);promoted++;return fault==6?-126:0;}
void sceKernelExitProcess(int code){assert(!code && restored && shutdowns==2);exited++;}void performance_restore(void){restored++;}void gui_shutdown(void){shutdowns++;}void http_shutdown(void){shutdowns++;}
int network_get(const char*u,const char*c,const char*a,char*b,unsigned n,unsigned t){(void)u;(void)c;(void)a;(void)b;(void)n;(void)t;return -1;}int network_download(const char*u,const char*p){(void)u;(void)p;return -1;}
int main(void){int major,minor;assert(sscanf(APP_VERSION,"%d.%d",&major,&minor)==2);snprintf(future_version,sizeof(future_version),"%02d.%02d",major+1,minor);snprintf(future_tag,sizeof(future_tag),"v%s",future_version);assert(update_install_version(APP_VERSION)<0);for(fault=1;fault<=6;fault++){loads=unloads=init=closed=promoted=exited=restored=shutdowns=0;assert(update_install_version(future_tag)<0 && !exited && !restored);if(fault==1 || fault==2)assert(!loads);if(fault==3)assert(loads==1 && !unloads);if(fault==4)assert(loads==2 && unloads==1);if(fault==5)assert(unloads==2 && !closed);if(fault==6)assert(unloads==2 && closed==1 && promoted==1);}
 for(preloaded=0;preloaded<=1;preloaded++){fault=0;loads=unloads=init=closed=promoted=exited=restored=shutdowns=0;assert(!update_install_version(future_tag) && exited==1 && promoted==1 && closed==1);assert(loads==(preloaded?0:2) && unloads==(preloaded?0:2));}
 assert(update_install_version("v01.40")<0);puts("Native updater staging path, title/version gate, module ownership, failure unwind, promotion and clean exit passed");return 0;}
