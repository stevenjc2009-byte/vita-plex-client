#include "progress.h"
#include "http.h"
#include "network.h"
#include "plex_auth.h"
#include "gui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef __vita__
#include "performance.h"
#include <psp2/kernel/threadmgr.h>
#ifdef PLEX_TEST_JOURNAL
#define JOURNAL "progress.bin"
#else
#define JOURNAL "ux0:data/plex-client/progress.bin"
#endif
#else
#define JOURNAL "progress.bin"
#endif
#define RECORDS 32
typedef struct {char server[256],client[40],rating[32];unsigned position,duration;} record_t;
static record_t records[RECORDS];static int loaded;
static int read_journal(const char *path){FILE *f=fopen(path,"rb");if(!f)return -1;char magic[4];unsigned count=0;int valid=fread(magic,1,4,f)==4;
 if(valid && !memcmp(magic,"PVP1",4))count=8;else if(valid && !memcmp(magic,"PVP2",4))valid=fread(&count,sizeof(count),1,f)==1 && count>0 && count<=RECORDS;else valid=0;
 memset(records,0,sizeof(records));if(valid)valid=fread(records,sizeof(record_t),count,f)==count;fclose(f);return valid?0:-1;}
static void load(void){if(loaded)return;loaded=1;if(read_journal(JOURNAL) && read_journal(JOURNAL ".bak"))memset(records,0,sizeof(records));
 for(int i=0;i<RECORDS;i++){record_t *r=records+i;r->server[255]=r->client[39]=r->rating[31]=0;if(strspn(r->rating,"0123456789")!=strlen(r->rating) || (r->duration && r->position>r->duration))memset(r,0,sizeof(*r));}}
static int persist(void){FILE *f=fopen(JOURNAL ".tmp","wb");if(!f)return -1;int ok=fwrite("PVP2",1,4,f)==4;unsigned count=RECORDS;ok=ok && fwrite(&count,sizeof(count),1,f)==1 && fwrite(records,sizeof(records),1,f)==1;int closed=fclose(f);if(!ok || closed){remove(JOURNAL ".tmp");return -1;}
 remove(JOURNAL ".bak");int old=rename(JOURNAL,JOURNAL ".bak")==0;if(rename(JOURNAL ".tmp",JOURNAL)){if(old)rename(JOURNAL ".bak",JOURNAL);return -1;}remove(JOURNAL ".bak");return 0;}
static int slot(const char *server,const char *client,const char *rating){load();for(int i=0;i<RECORDS;i++)if(!strcmp(records[i].server,server) && !strcmp(records[i].client,client) && !strcmp(records[i].rating,rating))return i;
 for(int i=0;i<RECORDS;i++)if(!records[i].rating[0])return i;return -1;}
static int timeline_url(const settings_t *s,const record_t *r,int state,char *url,unsigned cap){char tok[384],id[128];plex_url_encode(s->token,tok,sizeof(tok));plex_url_encode(s->client_id,id,sizeof(id));
 int n=snprintf(url,cap,"%s/:/timeline?ratingKey=%s&key=%%2Flibrary%%2Fmetadata%%2F%s&state=%s&time=%u&duration=%u&X-Plex-Client-Identifier=%s&X-Plex-Token=%s",s->server,r->rating,r->rating,state==1?"playing":state==2?"paused":"stopped",r->position,r->duration,id,tok);return n<0 || (unsigned)n>=cap?-1:0;}
int progress_pending(void){load();int n=0;for(int i=0;i<RECORDS;i++)n+=records[i].rating[0]!=0;return n;}
int progress_pending_for(const settings_t *s){load();int count=0;for(int i=0;i<RECORDS;i++)if(records[i].rating[0] && !strcmp(records[i].server,s->server) && !strcmp(records[i].client,s->client_id))count++;return count;}
int progress_discard(const settings_t *s,int others){load();record_t backup[RECORDS];memcpy(backup,records,sizeof(records));for(int i=0;i<RECORDS;i++)if(!others || strcmp(records[i].server,s->server) || strcmp(records[i].client,s->client_id))memset(records+i,0,sizeof(*records));if(persist()){memcpy(records,backup,sizeof(records));return -1;}return 0;}
int progress_retry(const settings_t *s){load();int result=0;char url[1400],body[2048];
 for(int i=0;i<RECORDS;i++){record_t *r=records+i;if(!r->rating[0] || strcmp(r->server,s->server) || strcmp(r->client,s->client_id))continue;
  gui_message("Resume recovery","Saving pending playback progress","O cancels. Progress remains on this Vita until Plex accepts it.");
  if(timeline_url(s,r,0,url,sizeof(url)) || network_get(url,s->client_id,"text/xml",body,sizeof(body),5)){result=-1;break;}memset(r,0,sizeof(*r));if(persist())result=-1;
 }return result;}
#ifdef __vita__
static settings_t current_settings;static record_t current;static SceUID thread=-1,ready=-1;
static unsigned pending_position,pending_sequence;static int cancelled;static int pending_state,finishing,result,record_slot;
static volatile int done;
static void cancel_finish(void){__atomic_store_n(&cancelled,1,__ATOMIC_RELEASE);http_cancel();}
// One producer (the UI thread). Publish position/state/end as one snapshot.
static void publish(unsigned position,int state,int end){__atomic_add_fetch(&pending_sequence,1,__ATOMIC_SEQ_CST);
 if(position)__atomic_store_n(&pending_position,position,__ATOMIC_SEQ_CST);
 __atomic_store_n(&pending_state,state,__ATOMIC_SEQ_CST);__atomic_store_n(&finishing,end,__ATOMIC_SEQ_CST);
 __atomic_add_fetch(&pending_sequence,1,__ATOMIC_SEQ_CST);sceKernelSignalSema(ready,1);}
static int worker(SceSize size,void *arg){(void)size;(void)arg;char url[1400],body[2048];
 for(;;){sceKernelWaitSema(ready,1,NULL);unsigned position,before,after;int state,end;
  do{before=__atomic_load_n(&pending_sequence,__ATOMIC_SEQ_CST);position=__atomic_load_n(&pending_position,__ATOMIC_SEQ_CST);state=__atomic_load_n(&pending_state,__ATOMIC_SEQ_CST);end=__atomic_load_n(&finishing,__ATOMIC_SEQ_CST);after=__atomic_load_n(&pending_sequence,__ATOMIC_SEQ_CST);}while((before&1) || before!=after);
  if(position){current.position=position;if(current.duration && current.position>current.duration)current.position=current.duration;
   if(record_slot>=0){records[record_slot]=current;if(persist()){result=-1;}}
   http_prepare(5);if(__atomic_load_n(&cancelled,__ATOMIC_ACQUIRE))http_cancel();int sent=__atomic_load_n(&cancelled,__ATOMIC_ACQUIRE) || timeline_url(&current_settings,&current,end?0:state,url,sizeof(url)) || http_get(url,current_settings.client_id,"text/xml",body,sizeof(body));
   if(end && !sent && record_slot>=0){memset(records+record_slot,0,sizeof(record_t));if(persist())result=-1;}else if(end && sent)result=-1;
  }if(end)break;
 }__atomic_store_n(&done,1,__ATOMIC_RELEASE);return 0;}
int progress_begin(const settings_t *s,const char *rating,unsigned duration){load();if(thread>=0 || !*rating || strspn(rating,"0123456789")!=strlen(rating))return -1;
 record_slot=slot(s->server,s->client_id,rating);
 current_settings=*s;memset(&current,0,sizeof(current));snprintf(current.server,sizeof(current.server),"%s",s->server);snprintf(current.client,sizeof(current.client),"%s",s->client_id);snprintf(current.rating,sizeof(current.rating),"%s",rating);current.duration=duration;
 pending_position=pending_sequence=0;pending_state=1;finishing=result=done=cancelled=0;ready=sceKernelCreateSema("plex_progress",0,0,1,NULL);if(ready<0)return ready;
 thread=performance_thread("plex_progress",worker,0x10000140,0x10000,0x40000);if(thread<0 || sceKernelStartThread(thread,0,NULL)<0){if(thread>=0)sceKernelDeleteThread(thread);thread=-1;sceKernelDeleteSema(ready);ready=-1;return -1;}return 0;}
int progress_durable(void){return record_slot>=0;}
void progress_update(unsigned position,int state){if(thread<0 || !position)return;publish(position,state,0);}
int progress_finish(unsigned position){if(thread<0)return -1;publish(position,0,1);
 int action=gui_wait(&done,cancel_finish);if(action==GUI_QUIT)network_request_exit();sceKernelWaitThreadEnd(thread,NULL,NULL);sceKernelDeleteThread(thread);sceKernelDeleteSema(ready);thread=ready=-1;return result;}
#else
int progress_begin(const settings_t*s,const char*r,unsigned d){(void)s;(void)r;(void)d;return slot(s->server,s->client_id,r)<0?-1:0;}
int progress_durable(void){return 0;}
void progress_update(unsigned p,int s){(void)p;(void)s;}int progress_finish(unsigned p){(void)p;return 0;}
#endif
