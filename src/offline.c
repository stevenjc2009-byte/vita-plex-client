#include "offline.h"
#include "http.h"
#include "hls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(__vita__) || defined(PLEX_TEST_OFFLINE)
#include "gui.h"
#include "player.h"
#include "progress.h"
#include "network.h"
#include "session.h"
#include <pthread.h>
#ifdef __vita__
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#define ROOT "ux0:data/plex-client/offline"
#else
void offline_test_delay(unsigned delay);
int offline_test_mkdir(const char *path);
int offline_test_truncate(const char *path,uint64_t bytes);
typedef struct {uint64_t st_size;} SceIoStat;
#define sceKernelDelayThread offline_test_delay
#define sceIoMkdir(path,mode) offline_test_mkdir(path)
#define sceIoChstat(path,st,bits) offline_test_truncate(path,(st)->st_size)
#define ROOT "offline"
#endif
#else
#define ROOT "offline"
#endif
static uint32_t hash(const void *data,size_t bytes){uint32_t n=2166136261u;const unsigned char *p=data;while(bytes--)n=(n^*p++)*16777619u;return n;}
void offline_path(int slot,int complete,char out[160]){snprintf(out,160,ROOT "/slot%d.%s",slot,complete?"media":"part");}
static void index_path(int slot,char out[180]){char media[160];offline_path(slot,1,media);snprintf(out,180,"%s.idx",media);}
static void meta_path(int slot,char out[160]){snprintf(out,160,ROOT "/slot%d.meta",slot);}
static int read_meta(const char *path,offline_entry_t *e){FILE *f=fopen(path,"rb");if(!f)return -1;int ok=fread(e,sizeof(*e),1,f)==1 && fgetc(f)==EOF && !ferror(f);fclose(f);
 if(!ok || e->magic!=0x31445650 || e->checksum!=hash(e,sizeof(*e)-sizeof(e->checksum)) || e->kind<1 || e->kind>3 || e->bytes>1024u*1024*1024 || e->complete>1 || !memchr(e->server,0,sizeof(e->server)) || !memchr(e->client,0,sizeof(e->client)) || !memchr(e->item.title,0,sizeof(e->item.title)) || !memchr(e->item.rating_key,0,sizeof(e->item.rating_key)))return -1;return 0;}
int offline_load(int slot,offline_entry_t *e){if(slot<0 || slot>=OFFLINE_SLOTS || !e)return -1;char path[160],backup[180],data[160];meta_path(slot,path);snprintf(backup,sizeof(backup),"%s.bak",path);if(read_meta(path,e) && read_meta(backup,e))return -1;
 offline_path(slot,e->complete,data);struct stat st;if(stat(data,&st) || st.st_size<0 || (uint64_t)st.st_size<e->bytes){offline_path(slot,!e->complete,data);if(stat(data,&st) || st.st_size<0 || (uint64_t)st.st_size!=e->bytes || !e->bytes)return -1;e->complete=!e->complete;}
 if(e->complete && (uint64_t)st.st_size!=e->bytes)return -1;
 if(e->kind==3 && e->next){char index[180];index_path(slot,index);FILE *f=fopen(index,"rb");if(!f)return -1;uint32_t previous[2]={0,0},point[2];int valid=e->next<=8192;
  for(unsigned i=0;valid && i<e->next;i++){valid=fread(point,sizeof(point),1,f)==1 && (i?(point[0]>previous[0] && point[1]>previous[1]):(!point[0] && !point[1])) && point[0]<e->total && point[1]<e->bytes;previous[0]=point[0];previous[1]=point[1];}fclose(f);if(!valid)return -1;}
 return 0;}
static int present(int slot){char path[160],other[180];struct stat st;for(int i=0;i<2;i++){offline_path(slot,i,path);if(!stat(path,&st))return 1;}index_path(slot,other);if(!stat(other,&st))return 1;meta_path(slot,path);if(!stat(path,&st))return 1;snprintf(other,sizeof(other),"%s.bak",path);return !stat(other,&st);}

static int save_meta(int slot,offline_entry_t *e){char path[160],temp[180],backup[180];meta_path(slot,path);snprintf(temp,sizeof(temp),"%s.tmp",path);snprintf(backup,sizeof(backup),"%s.bak",path);e->checksum=hash(e,sizeof(*e)-sizeof(e->checksum));FILE *f=fopen(temp,"wb");if(!f)return -1;int ok=fwrite(e,sizeof(*e),1,f)==1;if(fclose(f))ok=0;if(!ok){remove(temp);return -1;}remove(backup);int old=!rename(path,backup);if(rename(temp,path)){if(old)rename(backup,path);return -1;}return 0;}
int offline_owned(const offline_entry_t *e,const settings_t *s){return e && s && !strcmp(e->client,s->client_id) && !strcmp(e->server,s->server_id[0]?s->server_id:s->server);}
int offline_find(const settings_t *s,const char *rating){offline_entry_t e;for(int i=0;i<OFFLINE_SLOTS;i++)if(!offline_load(i,&e) && offline_owned(&e,s) && !strcmp(e.item.rating_key,rating))return i;return -1;}
uint64_t offline_usage(void){uint64_t total=0;for(int i=0;i<OFFLINE_SLOTS;i++){for(int j=0;j<2;j++){char path[160];struct stat st;offline_path(i,j,path);if(!stat(path,&st) && st.st_size>0)total+=(uint64_t)st.st_size;}char index[180];struct stat st;index_path(i,index);if(!stat(index,&st) && st.st_size>0)total+=(uint64_t)st.st_size;}return total;}

int offline_delete(int slot){if(slot<0 || slot>=OFFLINE_SLOTS)return -1;char path[160],extra[180];for(int i=0;i<2;i++){offline_path(slot,i,path);remove(path);struct stat st;if(!stat(path,&st))return -1;}index_path(slot,extra);remove(extra);meta_path(slot,path);remove(path);snprintf(extra,sizeof(extra),"%s.bak",path);remove(extra);snprintf(extra,sizeof(extra),"%s.tmp",path);remove(extra);return 0;}
int offline_position(int slot,unsigned pos){offline_entry_t e;if(offline_load(slot,&e))return -1;e.position=pos>e.item.duration?e.item.duration:pos;return save_meta(slot,&e);}
unsigned offline_signature(const char *xml,const settings_t *s){const char *p=strstr(xml,"<Media "),*end=p?strstr(p,"</Media>"):NULL;unsigned n=p && end?hash(p,(size_t)(end+8-p)):0;int quality=s->connection_kind==2?s->relay_bitrate:s->connection_kind==1?s->remote_bitrate:s->bitrate;return n^((unsigned)quality<<1)^(unsigned)s->subtitles;}
#if defined(__vita__) || defined(PLEX_TEST_OFFLINE)
static struct {offline_entry_t e;int slot,result;uint64_t quota;char source[4096];volatile int cancel,done;} job;
static void cancel_job(void){__atomic_store_n(&job.cancel,1,__ATOMIC_RELEASE);http_media_abort();}
static int fetch(const char *url,void *data,unsigned cap,unsigned *used,int range,uint64_t offset,uint64_t *total){int r=-1;for(int i=0;i<3;i++){r=range?http_media_range(url,data,cap,used,offset,total,&job.cancel):http_media_fetch(url,data,cap,used,&job.cancel);if(!r || r==-2 || r==-401 || r==-403 || r==-404 || r==-9 || r==-13 || r==HTTP_MEDIA_DISCONNECTED)break;sceKernelDelayThread(200000);}return r;}
static void *download_worker(void *arg){(void)arg;unsigned char *chunk=malloc(8*1024*1024);char *playlist=calloc(1,512*1024);char partial[160],finished[160];FILE *file=NULL,*index=NULL;int result=-1;uint64_t waited=0;
 offline_path(job.slot,0,partial);offline_path(job.slot,1,finished);if(!chunk || !playlist)goto out;
 file=fopen(partial,job.e.bytes?"r+b":"wb");if(!file || fseek(file,job.e.bytes,SEEK_SET))goto out;
 if(job.e.kind==3){char index_name[180];index_path(job.slot,index_name);index=fopen(index_name,job.e.next?"r+b":"wb");if(!index || job.e.next>8192 || fseek(index,(long)job.e.next*8,SEEK_SET))goto out;}
 if(save_meta(job.slot,&job.e))goto out;
 for(;;){if(job.e.kind==1 && job.e.total && job.e.bytes==job.e.total){result=0;break;}if(__atomic_load_n(&job.cancel,__ATOMIC_ACQUIRE)){result=-2;goto out;}unsigned used=0,segment_ms=0;int r;
  if(job.e.kind==1){uint64_t total=0;r=fetch(job.source,chunk,512*1024,&used,1,job.e.bytes,&total);if(r){result=r;goto out;}if(total>job.quota || total>1024u*1024*1024 || (job.e.total && total!=job.e.total)){result=-30;goto out;}job.e.total=(uint32_t)total;}
  else{char uri[2048],url[4096];uint64_t seq=0;r=playlist[0]?hls_segment(playlist,job.e.next,uri,sizeof(uri),&seq):0;
   if(r==2){result=0;break;}if(r<0){result=-31;goto out;}if(!r){r=fetch(job.source,playlist,512*1024-1,&used,0,0,NULL);if(r){result=r;goto out;}playlist[used]=0;if(++waited>600){result=HTTP_MEDIA_TIMEOUT;goto out;}sceKernelDelayThread(100000);continue;}
   if(seq!=job.e.next || hls_resolve(job.source,uri,url,sizeof(url))){result=-31;goto out;}r=fetch(url,chunk,8*1024*1024,&used,0,0,NULL);if(r){result=r;goto out;}if(!used || used%188 || chunk[0]!=0x47){result=-31;goto out;}if(job.e.next>=8192 || hls_duration(playlist,seq,&segment_ms) || job.e.total>0x7fffffff-segment_ms){result=-31;goto out;}waited=0;
  }
  if(!used || offline_usage()+used+(job.e.kind==3?8:0)>job.quota){result=-30;goto out;}
  if(fwrite(chunk,1,used,file)!=used || fflush(file)){result=-32;goto out;}if(index){uint32_t point[2]={job.e.total,job.e.bytes};if(fwrite(point,sizeof(point),1,index)!=1 || fflush(index)){result=-32;goto out;}job.e.total+=segment_ms;job.e.next++;}job.e.bytes+=used;if(save_meta(job.slot,&job.e)){result=-32;goto out;}
  if(job.e.kind==1 && job.e.bytes==job.e.total){result=0;break;}
 }
 if(index){if(fclose(index)){index=NULL;result=-32;goto out;}index=NULL;char index_name[180];index_path(job.slot,index_name);SceIoStat index_stat;memset(&index_stat,0,sizeof(index_stat));index_stat.st_size=job.e.next*8;if(sceIoChstat(index_name,&index_stat,8)<0){result=-32;goto out;}}
 if(fclose(file)){file=NULL;result=-32;goto out;}file=NULL;
 // A cancelled transfer can leave an uncommitted tail; truncate it before
 // exposing the finished file. The quota always includes such tails.
 SceIoStat st;memset(&st,0,sizeof(st));st.st_size=job.e.bytes;if(sceIoChstat(partial,&st,8)<0){result=-32;goto out;}
 if(rename(partial,finished)){result=-32;goto out;}job.e.complete=1;if(save_meta(job.slot,&job.e)){rename(finished,partial);result=-32;}
out:if(index)fclose(index);if(file)fclose(file);free(chunk);free(playlist);job.result=result;__atomic_store_n(&job.done,1,__ATOMIC_RELEASE);return NULL;
}
int offline_download(const settings_t *s,const browse_item_t *item,const char *source,int kind,unsigned signature){
 if(!s->offline || !settings_connection_allowed(s) || !item->rating_key[0] || strlen(source)>=sizeof(job.source))return -1;
 memset(&job,0,sizeof(job));job.slot=offline_find(s,item->rating_key);job.quota=(uint64_t)s->offline_quota*1024*1024;
 if(job.slot>=0){if(offline_load(job.slot,&job.e))return -1;if(job.e.complete)return 0;if(job.e.signature!=signature || job.e.kind!=(unsigned)kind)return -33;}
 else{for(int i=0;i<OFFLINE_SLOTS;i++)if(!present(i)){job.slot=i;break;}if(job.slot<0)return -34;
  job.e.magic=0x31445650;job.e.signature=signature;job.e.kind=kind;job.e.item=*item;snprintf(job.e.server,sizeof(job.e.server),"%s",s->server_id[0]?s->server_id:s->server);snprintf(job.e.client,sizeof(job.e.client),"%s",s->client_id);}
 if(offline_usage()>=job.quota)return -30;sceIoMkdir(ROOT,0777);strcpy(job.source,source);gui_message(item->title,"Downloading for offline playback","O stops safely. You can resume this download from the same item's options.");
 pthread_t thread;if(pthread_create(&thread,NULL,download_worker,NULL))return -1;int action=gui_wait(&job.done,cancel_job);pthread_join(thread,NULL);if(action==GUI_QUIT)network_request_exit();return job.result;
}
static settings_t playing_settings;static offline_entry_t playing_entry;static int playing_slot,save_error;
static void offline_progress(unsigned position,int state){(void)state;if(offline_position(playing_slot,position) || progress_record_local(&playing_settings,playing_entry.item.rating_key,playing_entry.item.duration,position))save_error=1;}
int offline_menu(settings_t *s,char *notice,unsigned cap){
 if(!s->offline){snprintf(notice,cap,"Enable offline downloads in Settings first.");return 0;}
 for(;;){offline_entry_t entries[OFFLINE_SLOTS];char labels[OFFLINE_SLOTS][220];const char *rows[OFFLINE_SLOTS+1];int slots[OFFLINE_SLOTS],damaged[OFFLINE_SLOTS]={0},count=0;
  for(int i=0;i<OFFLINE_SLOTS;i++){int valid=!offline_load(i,&entries[count]);if(valid && !offline_owned(&entries[count],s))continue;if(!valid && !present(i))continue;
   if(!valid){damaged[count]=1;memset(entries+count,0,sizeof(entries[count]));strcpy(entries[count].item.title,"Damaged download data");snprintf(labels[count],sizeof(labels[count]),"Damaged download slot %d (delete to recover space)",i+1);}
   else snprintf(labels[count],sizeof(labels[count]),"%s [%s, %u MB]",entries[count].item.title,entries[count].complete?"Ready":"Interrupted",entries[count].bytes/(1024*1024));slots[count]=i;rows[count]=labels[count];count++;}

  rows[count]="Back";char subtitle[120];snprintf(subtitle,sizeof(subtitle),"Storage %llu / %d MB; this account and server",(unsigned long long)(offline_usage()/(1024*1024)),s->offline_quota);int selected=gui_choice("Downloads",subtitle,rows,count+1);if(selected==GUI_QUIT)return GUI_QUIT;if(selected<0 || selected==count)return 0;
  offline_entry_t *e=entries+selected;int slot=slots[selected];if(damaged[selected]){const char *confirm[]={"Delete damaged download data","Back"};int d=gui_choice("Recover download storage","Only this app's damaged download slot is removed.",confirm,2);if(d==GUI_QUIT)return d;if(d==0 && offline_delete(slot))snprintf(notice,cap,"Could not remove damaged files.");continue;}const char *actions[]={e->complete?(e->position && e->position+1000<e->item.duration?"Resume":"Play"):"Resume download","Play from beginning","Delete download","Back"};int a=gui_choice(e->item.title,e->complete?"Available without Wi-Fi":"Partial download retained for recovery",actions,4);if(a==GUI_QUIT)return a;
  if(a==2){const char *confirm[]={"Delete this download","Cancel"};int d=gui_choice("Delete download",e->item.title,confirm,2);if(d==GUI_QUIT)return d;if(!d && offline_delete(slot))snprintf(notice,cap,"Could not delete download.");continue;}
  if(a==0 && !e->complete){session_download(s,&e->item,notice,cap);if(network_exit_requested())return GUI_QUIT;continue;}
  if((a!=0 && a!=1) || !e->complete)continue;unsigned offset=a==0 && e->position+1000<e->item.duration?e->position:0;char path[160];offline_path(slot,1,path);
  playing_settings=*s;playing_entry=*e;playing_slot=slot;save_error=0;player_set_markers(NULL,0);
  for(;;){int result=player_play_file(path,e->kind==1?2:3,offset);player_adaptive(0);player_progress_callback(offline_progress);if(result>=0)result=player_run_media(e->item.title,e->item.duration,offset,!strcmp(e->item.type,"track"));player_progress_callback(NULL);unsigned position=player_completed()?0:player_position()?player_position():e->position;if(player_position())offline_progress(player_position(),0);if(offline_position(slot,position))snprintf(notice,cap,"Could not save offline position.");else e->position=position;if(result==1)return GUI_QUIT;if(result==2 || result==4){offset=player_seek_position();if(result==4){const char *info[]={"Continue playback"};if(gui_choice("Offline tracks","Tracks are fixed when downloaded. Change online preferences, then download again.",info,1)==GUI_QUIT)return GUI_QUIT;}continue;}if(result<0)snprintf(notice,cap,"Offline playback failed at %s (0x%X).",player_error_stage(),result);if(save_error && result>=0)snprintf(notice,cap,"Offline position could not be saved. Check free storage.");break;}
 }
}
#else
int offline_download(const settings_t*s,const browse_item_t*i,const char*u,int k,unsigned g){(void)s;(void)i;(void)u;(void)k;(void)g;return -1;}
int offline_menu(settings_t*s,char*n,unsigned c){(void)s;(void)n;(void)c;return 0;}
#endif
