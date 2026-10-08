// Self-updater implementation. Release metadata comes from the GitHub
// API; the asset URL 302-redirects, so the download enables auto-redirect.

#include "update.h"
#include "plex_auth.h"
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int update_version_newer(const char *candidate, const char *current) {
  if(!candidate || !current)return 0;
  unsigned long v[4];const char *p[2]={candidate,current};
  for(int i=0;i<2;i++){if(*p[i]=='v')p[i]++;for(int j=0;j<2;j++){if(!isdigit((unsigned char)*p[i]))return 0;char *tail;errno=0;v[i*2+j]=strtoul(p[i],&tail,10);if(errno || v[i*2+j]>9999 || (j==0?*tail!='.':*tail!=0))return 0;p[i]=tail+(!j);}}
  return v[0]>v[2] || (v[0]==v[2] && v[1]>v[3]);
}

int update_release(const char *body,const char *current,char *dl_url_out,unsigned url_len,char *tag_out,unsigned tag_len){
 if(!body || !current || !dl_url_out || !url_len || !tag_out || !tag_len)return -1;
 dl_url_out[0]=tag_out[0]=0;char tag[32];
  if (plex_json_string(body, "tag_name", tag, sizeof(tag)) != 0) return -1;
  if(strlen(tag)>=tag_len)return -1;
  strcpy(tag_out,tag);
  if (!update_version_newer(tag, current)) return 0;
  const char *version=tag[0]=='v'?tag+1:tag;char expected[1024];
  snprintf(expected,sizeof(expected),"https://github.com/%s/releases/download/%s/vita-plex-client-%s.vpk",UPDATE_REPO,tag,version);
  const char *p=body;char candidate[1024];
  while((p=strstr(p,"\"browser_download_url\""))){if(!plex_json_string(p,"browser_download_url",candidate,sizeof(candidate)) && !strcmp(candidate,expected)){
    if(strlen(candidate)>=url_len)return -1;
    strcpy(dl_url_out,candidate);return 1;}p++;}
  return -1;
}

#ifdef __vita__

#include "http.h"
#include "network.h"
#include "package.h"
#include "gui.h"
#include "performance.h"
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/sysmodule.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
#include <stdio.h>
#include <string.h>

int update_check(char *dl_url_out, unsigned url_len,
    char *tag_out, unsigned tag_len) {
  char url[256];
  static char body[65536];
  snprintf(url, sizeof(url),
    "https://api.github.com/repos/%s/releases/latest", UPDATE_REPO);
  if (network_get(url, "PlexVita", "application/vnd.github+json",
        body, sizeof(body),15) != 0)
    return -1;
  return update_release(body,APP_VERSION,dl_url_out,url_len,tag_out,tag_len);
}

int update_download(const char *dl_url,
    void (*progress_cb)(unsigned received, unsigned total)) {
  (void)progress_cb;return network_download(dl_url, UPDATE_VPK_PATH);
}

#define UPDATE_STAGE "ux0:data/plex-client/update-stage"
static int clean_stage(const char *path,int depth){
 if(depth>8 || strncmp(path,UPDATE_STAGE,sizeof(UPDATE_STAGE)-1) || (path[sizeof(UPDATE_STAGE)-1] && path[sizeof(UPDATE_STAGE)-1]!='/'))return -1;
 SceUID d=sceIoDopen(path);if(d<0)return sceIoRemove(path);SceIoDirent e;int r;
 while(memset(&e,0,sizeof(e)),(r=sceIoDread(d,&e))>0){if(!strcmp(e.d_name,".") || !strcmp(e.d_name,".."))continue;if(strchr(e.d_name,'/') || strchr(e.d_name,':') || strchr(e.d_name,'\\')){r=-1;break;}char child[512];if(snprintf(child,sizeof(child),"%s/%s",path,e.d_name)>=(int)sizeof(child)){r=-1;break;}int removed=clean_stage(child,depth+1);if(removed<0){r=removed;break;}}
 sceIoDclose(d);return r<0?r:sceIoRmdir(path);
}
static int copy_header(void){
 // VitaShell generated this header when installing this same title. Its
 // package identity is independent of APP_VER; no license is replaced.
 unsigned char header[16384];FILE *f=fopen("ux0:app/PLEX00001/sce_sys/package/head.bin","rb");if(!f)return -1;
 size_t n=fread(header,1,sizeof(header),f);int extra=fgetc(f),bad=ferror(f);fclose(f);
 if(bad || extra!=EOF || n<0x100 || memcmp(header+0x37,"PLEX00001",9))return -1;
 sceIoMkdir(UPDATE_STAGE "/sce_sys/package",0777);f=fopen(UPDATE_STAGE "/sce_sys/package/head.bin","wb");if(!f)return -1;
 int ok=fwrite(header,1,n,f)==n;if(fclose(f))ok=0;return ok?0:-1;
}
int update_install_version(const char *tag){
 const char *version=tag && tag[0]=='v'?tag+1:tag;if(!version || !update_version_newer(version,APP_VERSION))return -1;
 SceIoStat previous;if(!sceIoGetstat(UPDATE_STAGE,&previous) && clean_stage(UPDATE_STAGE,0)<0)return -21;
 if(package_extract(UPDATE_VPK_PATH,UPDATE_STAGE,version) || copy_header()){clean_stage(UPDATE_STAGE,0);return -20;}
 static uint32_t args[]={0x180000,0xffffffff,0xffffffff,1,0xffffffff,0xffffffff};
 int module_result=-1,paf_owned=sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF)!=0;
 SceSysmoduleOpt option={sizeof(option),&module_result,{-1,-1}};
 int r=paf_owned?sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,sizeof(args),args,&option):0;
 if(r<0){clean_stage(UPDATE_STAGE,0);return r;}
 int promoter_owned=sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL)!=0;
 r=promoter_owned?sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL):0;
 int loaded=r>=0,initialized=0;if(loaded){r=scePromoterUtilityInit();initialized=r>=0;}if(initialized)r=scePromoterUtilityPromotePkgWithRif(UPDATE_STAGE,1);
 if(initialized)scePromoterUtilityExit();if(loaded && promoter_owned)sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
 if(paf_owned){option.result=&module_result;sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,0,NULL,&option);}
 clean_stage(UPDATE_STAGE,0);if(r<0)return r;remove(UPDATE_VPK_PATH);gui_shutdown();http_shutdown();performance_restore();sceKernelExitProcess(0);return 0;
}


#else

int update_check(char *u, unsigned ul, char *t, unsigned tl) {
  (void)u; (void)ul; (void)t; (void)tl;
  return 0;
}
int update_download(const char *u, void (*p)(unsigned, unsigned)) {
  (void)u; (void)p;
  return -1;
}
int update_install_version(const char *tag){(void)tag;return -1;}

#endif
