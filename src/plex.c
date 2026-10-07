#include "plex.h"
#include "plex_auth.h"
#include <stdio.h>
#include <string.h>

void plex_build_sections_url(
  const char *server, const char *token, char *out, unsigned out_len) {
  char encoded[384]; plex_url_encode(token,encoded,sizeof(encoded));
  snprintf(out, out_len, "%s/library/sections?X-Plex-Token=%s", server, encoded);
}

int plex_build_playback_url(const settings_t *s,const char *key,const char *session,
  unsigned offset,char *out,unsigned size) {
  if(!key || key[0]!='/')return -1;
  char path[768],token[384],id[128],sid[192],profile[1800];
  plex_url_encode(key,path,sizeof(path));plex_url_encode(s->token,token,sizeof(token));
  plex_url_encode(s->client_id,id,sizeof(id));plex_url_encode(session,sid,sizeof(sid));
  plex_url_encode("add-transcode-target(type=videoProfile&context=streaming&protocol=hls&container=mpegts&videoCodec=h264&audioCodec=aac&replace=true)+add-limitation(scope=videoCodec&scopeName=h264&type=upperBound&name=video.frameRate&value=30&replace=true)",profile,sizeof(profile));
  int n=snprintf(out,size,"%s/video/:/transcode/universal/start.m3u8?path=%s&mediaIndex=0&partIndex=0&protocol=hls&"
    "videoResolution=960x544&maxVideoBitrate=%d&videoQuality=100&videoCodec=h264&audioCodec=aac&audioChannels=2&"
    "directPlay=0&directStream=0&subtitles=%s&subtitleSize=100&location=%s&hasMDE=1&offset=%u&session=%s&"
    "X-Plex-Product=PlexVita&X-Plex-Platform=PlayStation%%20Vita&X-Plex-Client-Profile-Name=Chrome&X-Plex-Client-Identifier=%s&"
    "X-Plex-Client-Profile-Extra=%s&X-Plex-Token=%s",s->server,path,s->connection_kind==2?(s->relay_bitrate?s->relay_bitrate:1000):s->connection_kind==1?(s->remote_bitrate?s->remote_bitrate:s->bitrate):s->bitrate,s->subtitles?"burn":"none",(s->remote_mode || s->connection_kind)?"wan":"lan",offset/1000,sid,id,profile,token);
  return n<0 || (unsigned)n>=size?-1:0;
}

int plex_hls_media_url(const char *base,const char *body,const char *token,char *out,unsigned size) {
  if(!base || !body || !out || !size || strncmp(body,"#EXTM3U",7))return -1;
  if(!strstr(body,"#EXT-X-STREAM-INF:")) {
    if(!strstr(body,"#EXTINF:"))return -1;
    int n=snprintf(out,size,"%s",base);return n<0 || (unsigned)n>=size?-1:0;
  }
  const char *p=strstr(body,"#EXT-X-STREAM-INF:"),*uri=NULL,*end=NULL;
  while(p && *p) {
    p=strchr(p,'\n');if(!p)return -1;p++;
    if(*p && *p!='#' && *p!='\r' && *p!='\n'){uri=p;end=strpbrk(p,"\r\n");if(!end)end=p+strlen(p);break;}
  }
  if(!uri || end-uri>=2048)return -1;
  char relative[2048],resolved[3072],encoded[384];
  memcpy(relative,uri,(size_t)(end-uri));relative[end-uri]=0;
  const char *scheme=strstr(base,"://");if(!scheme)return -1;
  const char *origin_end=strchr(scheme+3,'/');if(!origin_end)return -1;
  size_t origin=(size_t)(origin_end-base);
  int n;
  if(strstr(relative,"://")) {
    if(strncmp(base,relative,origin) || (relative[origin]!='/' && relative[origin]!='\0'))return -1;
    n=snprintf(resolved,sizeof(resolved),"%s",relative);
  } else if(relative[0]=='/')n=snprintf(resolved,sizeof(resolved),"%.*s%s",(int)origin,base,relative);
  else {
    const char *query=strchr(base,'?'),*last=origin_end;
    for(const char *c=origin_end;*c && c!=query;c++)if(*c=='/')last=c;
    n=snprintf(resolved,sizeof(resolved),"%.*s%s",(int)(last-base+1),base,relative);
  }
  if(n<0 || (unsigned)n>=sizeof(resolved))return -1;
  plex_url_encode(token,encoded,sizeof(encoded));
  n=snprintf(out,size,"%s%s%s",resolved,strstr(resolved,"X-Plex-Token=")?"":strchr(resolved,'?')?"&X-Plex-Token=":"?X-Plex-Token=",strstr(resolved,"X-Plex-Token=")?"":encoded);
  return n<0 || (unsigned)n>=size?-1:1;
}

void plex_build_vita_transcode_url(const char *server,const char *token,const char *key,char *out,unsigned cap){
 settings_t st;settings_defaults(&st);snprintf(st.server,sizeof(st.server),"%s",server);snprintf(st.token,sizeof(st.token),"%s",token);snprintf(st.client_id,sizeof(st.client_id),"vita-legacy");
 if(plex_build_playback_url(&st,key,"vita-legacy",0,out,cap)<0 && cap)out[0]=0;
}

int plex_build_music_url(const settings_t *s,const char *key,const char *session,unsigned offset,char *out,unsigned cap){
 if(!key || strncmp(key,"/library/metadata/",18)){return -1;}
 char path[768],token[384],id[128],sid[256],profile[768];plex_url_encode(key,path,sizeof(path));plex_url_encode(s->token,token,sizeof(token));plex_url_encode(s->client_id,id,sizeof(id));plex_url_encode(session,sid,sizeof(sid));
 plex_url_encode("add-transcode-target(type=musicProfile&context=streaming&protocol=hls&container=mpegts&audioCodec=aac&replace=true)",profile,sizeof(profile));
 int n=snprintf(out,cap,"%s/music/:/transcode/universal/start.m3u8?path=%s&mediaIndex=0&partIndex=0&protocol=hls&directPlay=0&directStream=0&audioCodec=aac&audioChannels=2&maxAudioBitrate=192&audioSampleRate=48000&offset=%u&session=%s&X-Plex-Client-Identifier=%s&X-Plex-Client-Profile-Name=Chrome&X-Plex-Client-Profile-Extra=%s&X-Plex-Token=%s",s->server,path,offset/1000,sid,id,profile,token);return n<0 || (unsigned)n>=cap?-1:0;
}
int plex_build_photo_url(const settings_t *s,const char *key,char *out,unsigned cap){if(!key || key[0]!='/' || key[1]=='/' || strstr(key,"://"))return -1;char image[1024],token[384];plex_url_encode(key,image,sizeof(image));plex_url_encode(s->token,token,sizeof(token));int n=snprintf(out,cap,"%s/photo/:/transcode?width=960&height=544&minSize=0&upscale=0&format=jpeg&url=%s&X-Plex-Token=%s",s->server,image,token);return n<0 || (unsigned)n>=cap?-1:0;}
