#define _POSIX_C_SOURCE 200809L
#include "offline.h"
#include "gui.h"
#include "player.h"
#include "session.h"
#include "http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif
static unsigned size=700000,fail_at=UINT32_MAX,range_calls,segment_calls[3];
void offline_test_delay(unsigned us){struct timespec t={us/1000000,(long)(us%1000000)*1000};nanosleep(&t,NULL);}
int offline_test_mkdir(const char *path){
#ifdef _WIN32
 return _mkdir(path);
#else
 return mkdir(path,0777);
#endif
}
int offline_test_truncate(const char *path,uint64_t n){FILE *f=fopen(path,"r+b");if(!f)return -1;
#ifdef _WIN32
 int result=_chsize_s(_fileno(f),n);
#else
 int result=ftruncate(fileno(f),(off_t)n);
#endif
 fclose(f);return result;
}
void http_media_abort(void){}
int http_media_range(const char *url,void *data,unsigned cap,unsigned *used,uint64_t at,uint64_t *total,volatile int *cancel){assert(strstr(url,"fixture"));range_calls++;if(*cancel)return -2;if(at>=fail_at)return -403;assert(at<size);*used=size-at<cap?(unsigned)(size-at):cap;for(unsigned i=0;i<*used;i++)((unsigned char*)data)[i]=(unsigned char)((at+i)%251);*total=size;return 0;}
int http_media_fetch(const char *url,void *data,unsigned cap,unsigned *used,volatile int *cancel){if(*cancel)return -2;
 if(strstr(url,".m3u8")){const char *list="#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:1,\n0.ts\n#EXTINF:1,\n1.ts\n#EXTINF:1,\n2.ts\n#EXT-X-ENDLIST\n";assert(cap>strlen(list));*used=(unsigned)strlen(list);memcpy(data,list,*used);return 0;}
 const char *name=strrchr(url,'/');unsigned index=(unsigned)(name[1]-'0');assert(index<3);segment_calls[index]++;if(index>=fail_at)return -403;assert(cap>=188);memset(data,(int)index,188);((unsigned char*)data)[0]=0x47;*used=188;return 0;
}
void gui_message(const char*a,const char*b,const char*c){(void)a;(void)b;(void)c;}
int gui_wait(volatile int *done,void(*cancel)(void)){(void)cancel;while(!__atomic_load_n(done,__ATOMIC_ACQUIRE))offline_test_delay(1000);return 0;}
int gui_choice(const char*t,const char*s,const char**r,int n){(void)t;(void)s;(void)r;(void)n;return -1;}
int network_exit_requested(void){return 0;}void network_request_exit(void){}
void session_download(const settings_t*s,const browse_item_t*i,char*n,unsigned c){(void)s;(void)i;(void)n;(void)c;}
int player_play_file(const char*u,int k,unsigned o){(void)u;(void)k;(void)o;return -1;}void player_adaptive(int e){(void)e;}void player_set_markers(const media_marker_t*m,unsigned n){(void)m;(void)n;}void player_progress_callback(void(*cb)(unsigned,int)){(void)cb;}int player_run_media(const char*t,unsigned d,unsigned o,int a){(void)t;(void)d;(void)o;(void)a;return -1;}unsigned player_position(void){return 0;}unsigned player_seek_position(void){return 0;}int player_completed(void){return 0;}const char *player_error_stage(void){return "mock";}
int progress_record_local(const settings_t*s,const char*k,unsigned d,unsigned p){(void)s;(void)k;(void)d;(void)p;return 0;}
static void content(int slot,unsigned bytes){char path[160];offline_path(slot,1,path);FILE *f=fopen(path,"rb");assert(f);for(unsigned i=0;i<bytes;i++)assert(fgetc(f)==(int)(i%251));assert(fgetc(f)==EOF);fclose(f);}
int main(void){settings_t s;memset(&s,0,sizeof(s));strcpy(s.server,"http://fixture");strcpy(s.server_id,"server");strcpy(s.client_id,"client");s.offline=1;s.offline_quota=128;browse_item_t item;memset(&item,0,sizeof(item));strcpy(item.title,"Fixture");strcpy(item.rating_key,"42");strcpy(item.type,"movie");item.duration=3000;
 s.offline=0;assert(offline_download(&s,&item,"http://fixture/test.mp4",1,7)<0 && !range_calls);s.offline=1;
 fail_at=524288;assert(offline_download(&s,&item,"http://fixture/test.mp4",1,7)==-403);int slot=offline_find(&s,"42");assert(slot>=0);offline_entry_t e;assert(!offline_load(slot,&e) && !e.complete && e.bytes==524288);
 char partial[160];offline_path(slot,0,partial);FILE *tail=fopen(partial,"ab");assert(tail);for(int i=0;i<1000;i++)fputc(255,tail);fclose(tail);
 assert(offline_download(&s,&item,"http://fixture/test.mp4",1,8)==-33);fail_at=UINT32_MAX;assert(!offline_download(&s,&item,"http://fixture/test.mp4",1,7));assert(!offline_load(slot,&e) && e.complete && e.bytes==size);content(slot,size);assert(offline_usage()==size);
 char meta[180],backup[200];snprintf(meta,sizeof(meta),"offline/slot%d.meta",slot);snprintf(backup,sizeof(backup),"%s.bak",meta);assert(!remove(meta) && !rename(backup,meta));assert(!offline_load(slot,&e) && e.complete); // Crash between media rename and completion checkpoint.
 assert(!offline_position(slot,1500));assert(!offline_load(slot,&e) && e.position==1500);
 settings_t other=s;strcpy(other.client_id,"other");assert(offline_find(&other,"42")==-1);strcpy(other.client_id,s.client_id);strcpy(other.server_id,"other");assert(!offline_owned(&e,&other));assert(!offline_delete(slot) && !offline_usage());
 size=129*1024*1024;assert(offline_download(&s,&item,"http://fixture/large.mp4",1,7)==-30 && !offline_usage());assert(!offline_delete(offline_find(&s,"42")));
 fail_at=1;assert(offline_download(&s,&item,"http://fixture/index.m3u8",3,7)==-403);slot=offline_find(&s,"42");assert(!offline_load(slot,&e) && e.bytes==188 && e.next==1);offline_path(slot,0,partial);tail=fopen(partial,"ab");assert(tail);for(int i=0;i<1000;i++)fputc(255,tail);fclose(tail);fail_at=UINT32_MAX;assert(!offline_download(&s,&item,"http://fixture/index.m3u8",3,7));assert(!offline_load(slot,&e) && e.complete && e.bytes==564);assert(segment_calls[0]==1 && segment_calls[1]==2 && segment_calls[2]==1);
 char path[160];offline_path(slot,1,path);FILE *f=fopen(path,"rb");assert(f);for(unsigned i=0;i<3;i++){assert(fgetc(f)==0x47);for(unsigned n=1;n<188;n++)assert(fgetc(f)==(int)i);}assert(fgetc(f)==EOF);fclose(f);assert(!offline_delete(slot));
 puts("Offline opt-in, MP4 and HLS interrupted recovery, contents, quota, identity isolation and saved positions passed");return 0;
}
