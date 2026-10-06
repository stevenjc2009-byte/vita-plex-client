#define main conversion_mock_main
#include "parallel_video_test.c"
#undef main
#include "progress.h"
#include "gui.h"
#include <unistd.h>
static int offline;
void http_prepare(unsigned deadline){assert(deadline==5);}void http_cancel(void){}
int http_get(const char *url,const char *client,const char *accept,char *body,unsigned size){assert(strstr(url,"ratingKey=42") && strstr(url,"time=12080") && !strcmp(client,"test-vita"));(void)accept;(void)body;(void)size;return offline?-1:0;}
int network_get(const char *url,const char *client,const char *accept,char *body,unsigned size,unsigned deadline){assert(deadline==5);return http_get(url,client,accept,body,size);}
void gui_message(const char*t,const char*m,const char*d){(void)t;(void)m;(void)d;}
int gui_wait(volatile int *done,void(*cancel)(void)){(void)cancel;while(!__atomic_load_n(done,__ATOMIC_ACQUIRE))usleep(1000);return 0;}
int main(int argc,char **argv){(void)argv;settings_t s={0};snprintf(s.server,sizeof(s.server),"http://test:32400");snprintf(s.client_id,sizeof(s.client_id),"test-vita");snprintf(s.token,sizeof(s.token),"SECRET-TEST-TOKEN");
 if(argc>1){assert(progress_pending()==1);assert(!progress_retry(&s));assert(progress_pending()==0);puts("Pending playback position recovered after restart");return 0;}
 remove("progress.bin");remove("progress.bin.bak");offline=1;assert(!progress_begin(&s,"42",30000));progress_update(12080,1);assert(progress_finish(12080)<0);assert(progress_pending()==1);no_resources();
 FILE *f=fopen("progress.bin","rb");assert(f);char bytes[4000];size_t n=fread(bytes,1,sizeof(bytes),f);fclose(f);assert(n>4);for(size_t i=0;i+17<n;i++)assert(memcmp(bytes+i,"SECRET-TEST-TOKEN",17));
 puts("Failed timeline retained in durable journal without token; background worker joined");return 0;}
