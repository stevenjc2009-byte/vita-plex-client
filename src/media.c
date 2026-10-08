#include "media.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
static const char *end_tag(const char *p){char quote=0;for(;*p;p++){if(quote){if(*p==quote)quote=0;}else if(*p=='\'' || *p=='"')quote=*p;else if(*p=='>')return p;}return NULL;}
static int attr(const char *p,const char *name,char *out,unsigned cap){const char *e=end_tag(p);return e?plex_xml_attr(p,e,name,out,cap):-1;}
static int equal(const char *p,const char *name,const char *value){char s[96];return !attr(p,name,s,sizeof(s)) && !strcmp(s,value);}
static unsigned number(const char *p,const char *name){char s[32],*tail;errno=0;if(attr(p,name,s,sizeof(s)) || s[0]<'0' || s[0]>'9')return 0;unsigned long n=strtoul(s,&tail,10);return errno || *tail || n>0x7fffffff?0:(unsigned)n;}
int media_direct_part(const char *xml,const settings_t *s,char *key,unsigned cap){
 if(!xml || !s || !key || !cap)return 0;key[0]=0;const char *m=strstr(xml,"<Media "),*me=m?strstr(m,"</Media>"):NULL;
 if(!m || !me || !equal(m,"container","mp4") || !equal(m,"videoCodec","h264") || !equal(m,"audioCodec","aac"))return 0;
 unsigned width=number(m,"width"),height=number(m,"height"),bitrate=number(m,"bitrate");int quality=s->connection_kind==2?s->relay_bitrate:s->connection_kind==1?s->remote_bitrate:s->bitrate;
 if(!width || !height || (width&1) || (height&1) || width>1280 || height>720 || !bitrate || bitrate>(unsigned)quality)return 0;
 char fps[32];if(attr(m,"videoFrameRate",fps,sizeof(fps)))return 0;double rate=0;char *tail;if(!strcmp(fps,"PAL"))rate=25;else if(!strcmp(fps,"NTSC"))rate=29.97;else{rate=strtod(fps,&tail);if(*tail && strcmp(tail,"p"))return 0;}if(!isfinite(rate) || rate<=0 || rate>30)return 0;
 const char *part=strstr(m,"<Part ");if(!part || part>=me)return 0;const char *more=strstr(part+6,"<Part ");if(more && more<me)return 0;
 unsigned audio=0,video=0;for(const char *p=part;(p=strstr(p,"<Stream ")) && p<me;p++){
  if(equal(p,"streamType","1")){video++;char profile[64];unsigned depth=number(p,"bitDepth"),level=number(p,"level");if(!equal(p,"codec","h264") || (depth && depth!=8) || !level || level>41 || attr(p,"profile",profile,sizeof(profile)) || (strcmp(profile,"high") && strcmp(profile,"main") && strcmp(profile,"baseline") && strcmp(profile,"constrained baseline")))return 0;}
  if(equal(p,"streamType","2")){audio++;unsigned channels=number(p,"channels"),rate=number(p,"samplingRate");if(!equal(p,"codec","aac") || (channels!=1 && channels!=2) || (rate!=44100 && rate!=48000))return 0;}
  if(s->subtitles && equal(p,"streamType","3") && equal(p,"selected","1"))return 0;
 }
 // Multiple audio tracks stay on Plex's selected-track transcode path.
 if(audio!=1 || video!=1 || attr(part,"key",key,cap) || strncmp(key,"/library/parts/",15) || strpbrk(key,"?\r\n#")){key[0]=0;return 0;}return 1;
}
int media_markers(const char *xml,unsigned duration,media_marker_t *out,unsigned cap){
 if(!xml || !out)return 0;unsigned n=0;for(const char *p=xml;(p=strstr(p,"<Marker ")) && n<cap;p++){
  int credits=equal(p,"type","credits");if(!credits && !equal(p,"type","intro"))continue;char raw[32];if(attr(p,"startTimeOffset",raw,sizeof(raw)) || !raw[0] || strspn(raw,"0123456789")!=strlen(raw))continue;errno=0;unsigned long parsed=strtoul(raw,NULL,10);if(errno || parsed>0x7fffffff)continue;unsigned start=(unsigned)parsed,end=number(p,"endTimeOffset");if(end<=start || !duration || end>duration)continue;
  out[n++]=(media_marker_t){start,end,credits};}return (int)n;
}
int media_pass(const char *xml){const char *p=xml?strstr(xml,"<subscription "):NULL;if(!p && xml)p=strstr(xml,"<Subscription ");if(!p || !equal(p,"active","1"))return 0;
 char plan[96];if(!attr(p,"plan",plan,sizeof(plan)) && (strstr(plan,"remote") || strstr(plan,"Remote") || strstr(plan,"watch") || strstr(plan,"Watch")))return 0;
 const char *end=strstr(p,"</subscription>");if(!end)end=strstr(p,"</Subscription>");
 for(const char *f=p;end && (f=strstr(f,"<feature ")) && f<end;f++)if(equal(f,"id","pass") || equal(f,"id","skip-intro") || equal(f,"id","skip-credits"))return 1;
 return equal(p,"plan","lifetime") || equal(p,"plan","monthly") || equal(p,"plan","yearly") || equal(p,"plan","plexpass");}

int media_managed(const char *xml){const char *p=xml?strstr(xml,"<user "):NULL;if(!p && xml)p=strstr(xml,"<User ");return p && equal(p,"restricted","1") && equal(p,"home","1");}
