#define _DEFAULT_SOURCE
#define main conversion_mock_main
#include "parallel_video_test.c"
#undef main
#include "progress.h"
#include "gui.h"
#include <unistd.h>
static int offline,stress,cancel_test,release_prepare;static unsigned stopped_position;
void http_prepare(unsigned deadline){assert(deadline==5);if(cancel_test)while(!__atomic_load_n(&release_prepare,__ATOMIC_ACQUIRE))usleep(1000);}
void http_cancel(void){__atomic_store_n(&release_prepare,1,__ATOMIC_RELEASE);}
int http_get(const char *url,const char *client,const char *accept,char *body,unsigned size){assert(strstr(url,"ratingKey=42") && !strcmp(client,"test-vita"));assert(!cancel_test);if(!stress)assert(strstr(url,"time=12080"));if(strstr(url,"state=stopped")){const char *p=strstr(url,"&time=");assert(p);stopped_position=(unsigned)strtoul(p+6,NULL,10);}(void)accept;(void)body;(void)size;return offline?-1:0;}
int network_get(const char *url,const char *client,const char *accept,char *body,unsigned size,unsigned deadline){assert(deadline==5);return http_get(url,client,accept,body,size);}
void network_request_exit(void){}
void gui_message(const char*t,const char*m,const char*d){(void)t;(void)m;(void)d;}
int gui_wait(volatile int *done,void(*cancel)(void)){if(cancel_test)cancel();while(!__atomic_load_n(done,__ATOMIC_ACQUIRE))usleep(1000);return 0;}
int main(int argc,char **argv){settings_t s={0};snprintf(s.server,sizeof(s.server),"http://test:32400");snprintf(s.client_id,sizeof(s.client_id),"test-vita");snprintf(s.token,sizeof(s.token),"SECRET-TEST-TOKEN");
 if(argc>1){
  if(!strcmp(argv[1],"legacy")){struct {char server[256],client[40],rating[32];unsigned position,duration;} old[8]={0};snprintf(old[0].server,sizeof(old[0].server),"%s",s.server);snprintf(old[0].client,sizeof(old[0].client),"%s",s.client_id);snprintf(old[0].rating,sizeof(old[0].rating),"42");old[0].position=12080;old[0].duration=30000;FILE *legacy=fopen("progress.bin","wb");assert(legacy);assert(fwrite("PVP1",1,4,legacy)==4 && fwrite(old,sizeof(old),1,legacy)==1);fclose(legacy);}
  else {assert(!rename("progress.bin","progress.bin.bak"));FILE *broken=fopen("progress.bin","wb");assert(broken);fputs("PVP1",broken);fclose(broken);}
  assert(progress_pending()==1);assert(!progress_retry(&s));assert(progress_pending()==0);puts("Pending playback position recovered after restart");return 0;}
 remove("progress.bin");remove("progress.bin.bak");stress=1;
 for(int i=0;i<30;i++){stopped_position=0;assert(!progress_begin(&s,"42",30000));for(unsigned p=1000;p<12080;p+=1000)progress_update(p,1);assert(!progress_finish(12080));assert(stopped_position==12080 && !progress_pending());no_resources();}
 stress=0;cancel_test=1;assert(!progress_begin(&s,"42",30000));progress_update(12080,1);assert(progress_finish(12080)<0);assert(progress_pending()==1);no_resources();cancel_test=0;
 offline=1;
 for(int i=0;i<32;i++){snprintf(s.server,sizeof(s.server),"http://test:%d",32400+i);assert(!progress_begin(&s,"42",30000));assert(progress_durable());progress_update(12080,1);assert(progress_finish(12080)<0);}
 assert(progress_pending()==32);snprintf(s.server,sizeof(s.server),"http://overflow:32400");offline=0;assert(!progress_begin(&s,"42",30000) && !progress_durable());assert(!progress_finish(12080) && progress_pending()==32);
 snprintf(s.server,sizeof(s.server),"http://test:32400");assert(progress_pending_for(&s)==1);assert(!progress_discard(&s,1) && progress_pending()==1);assert(!progress_discard(&s,0) && !progress_pending());offline=1;
 strcpy(s.server_id,"server-one");assert(!progress_begin(&s,"42",30000));progress_update(12080,1);assert(progress_finish(12080)<0);
 settings_t remote=s;strcpy(remote.server,"https://remote.example:32400");remote.connection_kind=1;remote.remote_mode=1;
 assert(progress_pending_for(&remote)==1 && progress_position(&remote,"42")==12080);
 settings_t other=remote;strcpy(other.server_id,"server-two");assert(!progress_pending_for(&other));strcpy(other.server_id,"server-one");strcpy(other.client_id,"other-account-client");assert(!progress_pending_for(&other));
 offline=0;assert(!progress_retry(&remote) && !progress_pending());offline=1;s.server_id[0]=0;
 assert(!progress_begin(&s,"42",30000));progress_update(12080,1);assert(progress_finish(0)<0);assert(progress_pending()==1);no_resources();
 FILE *f=fopen("progress.bin","rb");assert(f);char bytes[16000];size_t n=fread(bytes,1,sizeof(bytes),f);fclose(f);assert(n>4);for(size_t i=0;i+17<n;i++)assert(memcmp(bytes+i,"SECRET-TEST-TOKEN",17));
 puts("Concurrent checkpoint coalescing and cancellation passed; failed timeline retained in durable journal without token; background worker joined");return 0;}
