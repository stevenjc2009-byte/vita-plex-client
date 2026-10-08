#include "media.h"
#include "http.h"
#include "update.h"
#include "progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *metadata="<Video><Media container='mp4' videoCodec='h264' audioCodec='aac' width='960' height='544' bitrate='1500' videoFrameRate='24p'><Part key='/library/parts/1/file.mp4'><Stream streamType='1' codec='h264' bitDepth='8' level='41' profile='high'/><Stream streamType='2' codec='aac' channels='2' samplingRate='48000'/></Part></Media></Video>";
static void reject(const char *old,const char *replacement){char data[2048],key[256];const char *p=strstr(metadata,old);assert(p);snprintf(data,sizeof(data),"%.*s%s%s",(int)(p-metadata),metadata,replacement,p+strlen(old));settings_t s;settings_defaults(&s);assert(!media_direct_part(data,&s,key,sizeof(key)));}
void gui_message(const char*a,const char*b,const char*c){(void)a;(void)b;(void)c;}
int network_get(const char*u,const char*c,const char*a,char*b,unsigned n,unsigned t){(void)u;(void)c;(void)a;(void)b;(void)n;(void)t;return -1;}
int main(void){settings_t s;settings_defaults(&s);char key[256];assert(media_direct_part(metadata,&s,key,sizeof(key)) && !strcmp(key,"/library/parts/1/file.mp4"));s.remote_bitrate=1000;s.connection_kind=1;assert(!media_direct_part(metadata,&s,key,sizeof(key)));
 reject("h264","hevc");reject("mp4","mkv");reject("bitDepth='8'","bitDepth='10'");reject("level='41'","level='51'");reject("height='544'","height='1080'");reject("24p","60p");reject("channels='2'","channels='6'");reject("</Part>","<Stream streamType='3' selected='1'/></Part>");reject("</Part>","</Part><Part key='/library/parts/2/file.mp4'/>");reject("file.mp4","file.mp4?leak=1");
 media_marker_t m[4];assert(media_markers("<Marker type='intro' startTimeOffset='0' endTimeOffset='1500'/><Marker type='credits' startTimeOffset='2500' endTimeOffset='3000'/><Marker type='intro' startTimeOffset='5' endTimeOffset='4'/><Marker type='credits' startTimeOffset='1' endTimeOffset='999999999999999999999'/>",3000,m,4)==2 && !m[0].credits && m[1].credits && m[0].end==1500);
 assert(media_pass("<subscription active='1' plan='lifetime'/>") && !media_pass("<subscription active='0' plan='lifetime'/>") && !media_pass("<subscription active='1' plan='remote-watch-pass-monthly'/>") && !media_pass("<subscription active='1'/>") && media_pass("<subscription active='1'><feature id='skip-intro'/></subscription>"));assert(media_managed("<User restricted='1' home='1'/>") && !media_managed("<User restricted='0' home='1'/>"));
 uint64_t total=0;unsigned bytes=0;const char *range="bytes 4294967296-4294967395/9000000000";assert(!http_range_header(range,(unsigned)strlen(range),4294967296ULL,256,&total,&bytes) && bytes==100 && total==9000000000ULL);
 const char *bad[]={"bytes 0-100/100","bytes 1-2/3","bytes 0-2/*","bytes 0-256/300","bytes 0-1/18446744073709551616","bytes -1-2/3","bytes 0-1/3junk"};for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(http_range_header(bad[i],(unsigned)strlen(bad[i]),0,256,&total,&bytes));
 assert(!update_version_newer("v9999999999999999999999999.42","01.41") && update_version_newer("v01.42","01.41") && !update_version_newer("v01.41oops","01.40"));
 char url[1024],tag[32];const char *release="{\"tag_name\":\"v01.42\",\"assets\":[{\"browser_download_url\":\"https://github.com/stevenjc2009-byte/vita-plex-client/releases/download/v01.42/vita-plex-client-01.42.vpk\"}]}";
 assert(update_release(release,"01.41",url,sizeof(url),tag,sizeof(tag))==1 && !strcmp(tag,"v01.42") && strstr(url,"v01.42/vita-plex-client-01.42.vpk"));assert(!update_release(release,"01.42",url,sizeof(url),tag,sizeof(tag)) && !url[0]);assert(update_release("{\"tag_name\":\"v01.42\",\"browser_download_url\":\"https://evil.example/update.vpk\"}","01.41",url,sizeof(url),tag,sizeof(tag))<0 && !url[0]);
 strcpy(s.client_id,"client");strcpy(s.server_id,"server");assert(!progress_record_local(&s,"42",3000,1500) && progress_position(&s,"42")==1500);strcpy(s.server,"https://other-network:32400");assert(progress_position(&s,"42")==1500);strcpy(s.client_id,"other");assert(!progress_position(&s,"42"));
 puts("Direct Play compatibility, marker bounds, Plex Pass eligibility, 64-bit range validation and version overflow tests passed");return 0;}
