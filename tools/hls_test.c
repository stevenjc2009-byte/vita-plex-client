#include "hls.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){char out[4096];uint64_t seq;
const char *p="#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:12\n#EXTINF:2,\n12.ts\n#EXTINF:2,\n13.ts\n#EXT-X-ENDLIST\n";
assert(hls_segment(p,UINT64_MAX,out,sizeof(out),&seq)==1 && seq==12 && !strcmp(out,"12.ts"));
assert(hls_segment(p,13,out,sizeof(out),&seq)==1 && seq==13);
assert(hls_segment(p,14,out,sizeof(out),&seq)==2);
assert(hls_segment(p,11,out,sizeof(out),&seq)==-2);
assert(hls_segment("#EXTM3U\n#EXTINF:2,\na.ts\n",1,out,sizeof(out),&seq)==0);
assert(hls_segment("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n",0,out,sizeof(out),&seq)<0);
assert(hls_segment("#EXTM3U\n#EXT-X-MAP:URI=init\n",0,out,sizeof(out),&seq)<0);
assert(hls_segment("#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:-1\n",0,out,sizeof(out),&seq)<0);
assert(hls_segment("#EXTM3U\n#EXTINF:2,\na.ts\n#EXT-X-KEY:METHOD=AES-128\n",0,out,sizeof(out),&seq)<0);
assert(hls_segment("#EXTM3U\nfoo.ts\n",0,out,sizeof(out),&seq)<0);
assert(hls_segment(p,12,out,3,&seq)<0);
assert(hls_variant("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=100\nstream.m3u8?session=x\n",out,sizeof(out))==1 && !strcmp(out,"stream.m3u8?session=x"));
assert(hls_variant(p,out,sizeof(out))==0);
assert(hls_variant("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=100\n",out,sizeof(out))<0);
const char *base="http://server:32400/video/start.m3u8?session=x&X-Plex-Token=fake%2Btoken&other=1";
assert(!hls_resolve(base,"1.ts",out,sizeof(out)) && !strcmp(out,"http://server:32400/video/1.ts?X-Plex-Token=fake%2Btoken"));
assert(!hls_resolve(base,"/part/1.ts?session=x",out,sizeof(out)) && strstr(out,"session=x&X-Plex-Token=fake%2Btoken"));
assert(!hls_resolve(base,"http://server:32400/1.ts?X-Plex-Token=own",out,sizeof(out)) && strstr(out,"Token=own"));
assert(hls_resolve(base,"http://other:32400/1.ts",out,sizeof(out))<0);
assert(hls_resolve(base,"http://server:32400.evil/1.ts",out,sizeof(out))<0);
assert(hls_resolve(base,"//evil/1.ts",out,sizeof(out))<0);
assert(hls_resolve(base,"file:/1.ts",out,sizeof(out))<0);
assert(hls_resolve(base,"1.ts\n",out,sizeof(out))<0);
assert(hls_resolve(base,"1.ts",out,16)<0);
puts("HLS sequence, EOF, unsupported media, URL scope and token inheritance tests passed");return 0;}
