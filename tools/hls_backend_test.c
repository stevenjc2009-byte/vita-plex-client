#define _POSIX_C_SOURCE 200809L
#include "hls_backend.h"
#include "http.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static const char *folder;static int fault,audio_only,slow,retry_fault;static unsigned downloads;
static void sleep_ms(unsigned n){struct timespec t={n/1000,(long)(n%1000)*1000000};nanosleep(&t,0);}
void http_media_abort(void){}
int http_media_fetch(const char *url,void *data,unsigned cap,unsigned *used,volatile int *cancel){
 *used=0;if(__atomic_load_n(cancel,__ATOMIC_ACQUIRE))return -2;
 const char *name=strrchr(url,'/');assert(name);name++;char path[4096];snprintf(path,sizeof(path),"%s/%s",folder,name);
 if(!strcmp(name,"master.m3u8")){const char *master="#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000000\nindex.m3u8\n";assert(strlen(master)<cap);memcpy(data,master,strlen(master));*used=(unsigned)strlen(master);return 0;}
 if(fault && strstr(name,".ts"))return -403;
 if(retry_fault && strstr(name,".ts")){retry_fault=0;return HTTP_MEDIA_TIMEOUT;}
 if(slow && strstr(name,"3.ts")){for(int i=0;i<500;i++){if(__atomic_load_n(cancel,__ATOMIC_ACQUIRE))return -2;sleep_ms(10);}}
 FILE *file=fopen(path,"rb");if(!file)return -404;*used=(unsigned)fread(data,1,cap,file);int extra=fgetc(file);fclose(file);assert(extra==EOF);downloads++;return 0;
}
int http_media_range(const char *url,void *data,unsigned cap,unsigned *used,uint64_t offset,uint64_t *total,volatile int *cancel){
 *used=0;if(*cancel)return -2;const char *name=strrchr(url,'/');assert(name);char path[4096];snprintf(path,sizeof(path),"%s/%s",folder,name+1);FILE *f=fopen(path,"rb");if(!f)return -404;fseek(f,0,SEEK_END);*total=(uint64_t)ftell(f);if(offset>=*total){fclose(f);return -416;}fseek(f,(long)offset,SEEK_SET);*used=(unsigned)fread(data,1,cap,f);fclose(f);downloads++;return 0;
}
static int file_kind;static unsigned file_offset;
static void play(void){
 unsigned video=0,audio=0;uint64_t last=0,last_audio=0;int started=0,paused=0,frozen=0;
 char source[4096];snprintf(source,sizeof(source),"%s/test.%s",file_kind>=2?folder:"http://fixture",file_kind==3?"ts":"mp4");
 if(file_kind==1)strcpy(source,"http://fixture/test.mp4");assert(!(file_kind?hls_backend_start_source(source,file_kind,file_offset):hls_backend_start("http://fixture/master.m3u8")));
 for(unsigned tick=0;tick<30000;tick+=2){
  if(tick && !(tick%2000))fprintf(stderr,"Streaming test: frames=%u audio=%u time=%llu stage=%s\n",video,audio,(unsigned long long)hls_backend_time(),hls_backend_stage());
  int error=hls_backend_error();if(error){fprintf(stderr,"failure stage=%s error=%d\n",hls_backend_stage(),error);assert(!error);}
  if(hls_backend_active())started=1;
  hls_video_t v;hls_audio_t a;
  if(hls_backend_video(&v)){assert(v.data && v.width && v.height && v.stamp>=last);last=v.stamp;video++;}
  if(hls_backend_audio(&a)){assert(a.data && a.size && a.channels<=2 && a.rate>=8000 && (!audio || a.stamp>last_audio));last_audio=a.stamp;audio++;}
  if(slow && hls_backend_buffering() && !frozen && audio>5){uint64_t before=hls_backend_time();sleep_ms(100);assert(hls_backend_time()==before);frozen=1;}
  if(started && !paused && (audio_only?audio>=2:video>=2)){assert(!hls_backend_pause(1));uint64_t before=hls_backend_time();sleep_ms(60);assert(hls_backend_time()==before && !hls_backend_video(&v) && !hls_backend_audio(&a));assert(!hls_backend_pause(0));paused=1;}
  if(started && !hls_backend_active())break;
  sleep_ms(2);
 }
 assert(started && (audio_only?video==0:video>5) && audio>5 && !hls_backend_active());if(slow)assert(frozen);hls_backend_stop();assert(!hls_backend_active());
 printf("Decoded %u video frames / %u audio frames; pause and clean EOF passed\n",video,audio);
}
int main(int argc,char **argv){setbuf(stdout,NULL);assert(argc==2 || argc==3);audio_only=argc==3 && !strcmp(argv[2],"audio");folder=argv[1];if(argc==3){if(!strcmp(argv[2],"mp4"))file_kind=1;if(!strcmp(argv[2],"local"))file_kind=2;if(!strcmp(argv[2],"seek")){file_kind=1;file_offset=1100;}if(!strcmp(argv[2],"tsseek")){file_kind=3;file_offset=1100;}}play();play();if(file_kind)return 0;if(argc==3 && !strcmp(argv[2],"slow")){slow=1;retry_fault=1;play();slow=0;puts("Slow segment, transient timeout retry and frozen buffering clock passed");}
 fault=1;assert(!hls_backend_start("http://fixture/master.m3u8"));for(int i=0;i<1000 && !hls_backend_error();i++)sleep_ms(2);fprintf(stderr,"HTTP-failure stage=%s error=%d active=%d\n",hls_backend_stage(),hls_backend_error(),hls_backend_active());assert(hls_backend_error()==-403 && !hls_backend_active());hls_backend_stop();fault=0;
 for(int n=0;n<3;n++){assert(!hls_backend_start("http://fixture/master.m3u8"));sleep_ms(40);hls_backend_stop();assert(!hls_backend_active());}
 printf("Restart, HTTP failure and cancellation passed (%u fixture downloads)\n",downloads);return 0;}
