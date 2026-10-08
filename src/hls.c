#include "hls.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

int hls_segment(const char *body,uint64_t wanted,char *uri,unsigned cap,uint64_t *sequence){
 if(!body || strncmp(body,"#EXTM3U",7) || !uri || !cap || !sequence)return -1;
 uint64_t first=0;
 for(const char *line=body;*line;){
  const char *end=strpbrk(line,"\r\n");if(!end)end=line+strlen(line);size_t n=(size_t)(end-line);
  if((n>=18 && !strncmp(line,"#EXT-X-STREAM-INF:",18)) || (n>=17 && !strncmp(line,"#EXT-X-BYTERANGE:",17)) || (n>=11 && !strncmp(line,"#EXT-X-MAP:",11)) || (n==20 && !strncmp(line,"#EXT-X-DISCONTINUITY",20)))return -1;
  if(n>=11 && !strncmp(line,"#EXT-X-KEY:",11) && (n!=22 || strncmp(line+11,"METHOD=NONE",11)))return -1;
  if(n>=22 && !strncmp(line,"#EXT-X-MEDIA-SEQUENCE:",22)){
   char *tail;errno=0;if(line[22]<'0' || line[22]>'9')return -1;
   unsigned long long v=strtoull(line+22,&tail,10);if(errno || tail!=end)return -1;first=(uint64_t)v;
  }
  line=end;while(*line=='\r' || *line=='\n')line++;
 }
 const char *p;
 if(wanted==UINT64_MAX)wanted=first;
 if(wanted<first)return -2;
 uint64_t current=first;int duration=0;
 for(p=body;*p;){const char *end=strpbrk(p,"\r\n");if(!end)end=p+strlen(p);size_t n=(size_t)(end-p);
  if(n>=8 && !strncmp(p,"#EXTINF:",8))duration=1;
  else if(n && *p!='#'){
   if(!duration || current==UINT64_MAX)return -1;
   if(current==wanted){if(n>=cap)return -1;memcpy(uri,p,n);uri[n]=0;*sequence=current;return 1;}
   current++;duration=0;
  }
  p=end;while(*p=='\r' || *p=='\n')p++;
 }
 return strstr(body,"#EXT-X-ENDLIST")?2:0;
}
int hls_resolve(const char *base,const char *uri,char *out,unsigned cap){
 if(!base || !uri || !*uri || !out || !cap || strpbrk(uri,"\r\n#") || uri[0]=='?' || (uri[0]=='/' && uri[1]=='/'))return -1;
 for(const unsigned char *c=(const unsigned char*)uri;*c;c++)if(*c<=32 || *c==127 || *c==92)return -1;
 const char *scheme=strstr(base,"://");if(!scheme || (strncmp(base,"http://",7) && strncmp(base,"https://",8)))return -1;
 const char *host=scheme+3,*origin_end=strchr(host,'/');if(!origin_end || memchr(host,'@',(size_t)(origin_end-host)))return -1;
 size_t origin=(size_t)(origin_end-base);int n;
 if(strstr(uri,"://")){if(strncmp(base,uri,origin) || uri[origin]!='/')return -1;n=snprintf(out,cap,"%s",uri);}
 else if(strchr(uri,':'))return -1;
 else if(uri[0]=='/')n=snprintf(out,cap,"%.*s%s",(int)origin,base,uri);
 else{const char *last=origin_end,*query=strchr(origin_end,'?');for(const char*p=origin_end;*p && p!=query;p++)if(*p=='/')last=p;n=snprintf(out,cap,"%.*s%s",(int)(last-base+1),base,uri);}
 if(n<0 || (unsigned)n>=cap)return -1;
 const char *q=strchr(out,'?');if(q && (strstr(q,"?X-Plex-Token=") || strstr(q,"&X-Plex-Token=")))return 0;
 q=strchr(base,'?');if(q){const char *token=strstr(q,"?X-Plex-Token=");if(!token)token=strstr(q,"&X-Plex-Token=");if(token){token+=14;const char *end=strchr(token,'&');if(!end)end=token+strlen(token);if(end-token>512 || strpbrk(token,"\r\n#"))return -1;
   int added=snprintf(out+n,cap-(unsigned)n,"%cX-Plex-Token=%.*s",strchr(out,'?')?'&':'?',(int)(end-token),token);if(added<0 || (unsigned)added>=cap-(unsigned)n)return -1;}}
 return 0;
}

int hls_variant(const char *body,char *uri,unsigned cap){
 if(!body || strncmp(body,"#EXTM3U",7) || !uri || !cap)return -1;
 int variant=0;
 for(const char *p=body;*p;){const char *end=strpbrk(p,"\r\n");if(!end)end=p+strlen(p);size_t n=(size_t)(end-p);
  if(n>=18 && !strncmp(p,"#EXT-X-STREAM-INF:",18))variant=1;
  else if(variant && n && *p!='#'){if(n>=cap)return -1;memcpy(uri,p,n);uri[n]=0;return 1;}
  p=end;while(*p=='\r' || *p=='\n')p++;
 }
 return variant?-1:0;
}
int hls_duration(const char *body,uint64_t wanted,unsigned *milliseconds){
 char uri[2048];uint64_t actual=0;if(!milliseconds || hls_segment(body,wanted,uri,sizeof(uri),&actual)!=1)return -1;
 uint64_t seq=0;const char *start=strstr(body,"#EXT-X-MEDIA-SEQUENCE:");if(start)seq=strtoull(start+22,NULL,10);
 double duration=0;for(const char *p=body;*p;){const char *end=strpbrk(p,"\r\n");if(!end)end=p+strlen(p);
  if(end-p>=8 && !strncmp(p,"#EXTINF:",8)){char *tail;duration=strtod(p+8,&tail);if(tail==p+8 || tail>end || (*tail!=',' && tail!=end) || !isfinite(duration) || duration<=0 || duration>120)return -1;}
  else if(p<end && *p!='#'){if(seq==actual){if(duration<=0)return -1;*milliseconds=(unsigned)(duration*1000+0.5);return *milliseconds?0:-1;}seq++;duration=0;}
  p=end;while(*p=='\r' || *p=='\n')p++;
 }return -1;
}
