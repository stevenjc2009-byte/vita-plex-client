#include "session.h"
#include "plex.h"
#include "plex_auth.h"
#include "network.h"
#include "progress.h"
#include "player.h"
#include "gui.h"
#include "http.h"
#include <stdio.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
static char body[512*1024];
static unsigned serial;
static int get(const settings_t *s,const char *path,const char *sort,int offset,int count){char url[4096];if(plex_build_page_url(s->server,s->token,path,"",sort,offset,count,url,sizeof(url)))return -1;return network_get(url,s->client_id,"text/xml",body,sizeof(body),15);}
static void error(char *notice,unsigned cap,const char *what){snprintf(notice,cap,"%s (HTTP %d / error 0x%X).",what,http_last_status(),http_last_error());}
static int next_episode(const settings_t *s,const browse_item_t *current,browse_item_t *next){
 const char *parent=current->grandparent_key[0]?current->grandparent_key:current->parent_key;
 if(strcmp(current->type,"episode") || !parent[0])return 0;char path[160];snprintf(path,sizeof(path),"/library/metadata/%s/%s",parent,current->grandparent_key[0]?"allLeaves":"children");
 int found=0;static browse_item_t page_items[BROWSE_MAX_ITEMS];
 for(int offset=0;;offset+=BROWSE_MAX_ITEMS){gui_message("Next episode","Finding the next episode","O cancels.");if(get(s,path,"parentIndex:asc,index:asc",offset,BROWSE_MAX_ITEMS))return -1;
  browse_page_t page;if(plex_parse_page(body,&page))return -1;int n=plex_parse_items(body,"Video",page_items,BROWSE_MAX_ITEMS);
  if(offset && page.offset!=offset)return -1;
  for(int i=0;i<n;i++){if(found){*next=page_items[i];return 1;}if(!strcmp(current->rating_key,page_items[i].rating_key))found=1;}
  if(!n || offset+n>=page.total)return 0;
 }
}
static int track(settings_t *s,const char *key,int type){
 gui_message("Playback tracks","Loading available tracks","O cancels.");if(get(s,key,"",0,1))return -1;static plex_stream_t streams[64];char part[32];int n=plex_parse_streams(body,streams,64,part,sizeof(part));
 const char *rows[66];int ids[66],count=0;
 if(type==3){rows[count]="Off";ids[count++]=-1;rows[count]="Use Plex selection";ids[count++]=-2;}
 for(int i=0;i<n;i++)if(streams[i].type==type){rows[count]=streams[i].label;ids[count++]=i;}
 if(!count){const char *back[]={"Back"};return gui_choice("Audio tracks","No selectable audio tracks reported",back,1)==GUI_QUIT?-2:0;}
 int selected=gui_choice(type==2?"Audio track":"Subtitles","Applies to this video's Plex playback preferences",rows,count);if(selected==GUI_QUIT)return -2;if(selected<0)return 0;
 if(type==3 && ids[selected]==-2){s->subtitles=1;return settings_save(s)?-1:0;}
 const char *id=ids[selected]<0?"0":streams[ids[selected]].id;if(!part[0])return -1;
 char url[1024],tok[384];plex_url_encode(s->token,tok,sizeof(tok));snprintf(url,sizeof(url),"%s/library/parts/%s?allParts=1&%sStreamID=%s&X-Plex-Token=%s",s->server,part,type==2?"audio":"subtitle",id,tok);
 gui_message("Playback tracks","Saving track selection","O cancels.");if(network_put(url,s->client_id,body,sizeof(body)))return -1;
 if(type==3){s->subtitles=strcmp(id,"0")!=0;if(settings_save(s))return -1;}return 0;
}
int session_play(settings_t *s,browse_item_t *original,char *notice,unsigned cap){
 browse_item_t current=*original;int advanced=0,direct=0;
 for(;;){
  if(strcmp(current.type,"movie") && strcmp(current.type,"episode") && strcmp(current.type,"clip")){snprintf(notice,cap,"This player supports video; music and photos remain browsable.");return 0;}
  char key[384];if(current.rating_key[0])snprintf(key,sizeof(key),"/library/metadata/%s",current.rating_key);else snprintf(key,sizeof(key),"%s",current.key);
  gui_message("Video details","Loading description","O cancels.");if(!get(s,key,"",0,1)){static browse_item_t details;if(plex_parse_items(body,"Video",&details,1)==1)current=details;}
  int choice=0;
  for(;;){if(direct){direct=0;choice=0;break;}browse_item_t shown=current;if(!s->resume)shown.view_offset=0;choice=gui_details(&shown,s->server,s->token,notice);
   if(choice==GUI_QUIT || network_exit_requested())return 1;if(choice<0)return 0;
   if(choice!=3)break;
   const char *rows[]={"Audio track","Subtitles",current.view_count?"Mark unwatched":"Mark watched","Play next episode","Back"};int action=gui_choice("Video options",current.title,rows,5);
   if(action==GUI_QUIT)return 1;
   if(action==0 || action==1){int result=track(s,key,action==0?2:3);if(result==-2)return 1;if(result<0)error(notice,cap,"Track selection failed");else snprintf(notice,cap,"Playback preference saved.");}
   else if(action==2){char url[1024],tok[384];plex_url_encode(s->token,tok,sizeof(tok));snprintf(url,sizeof(url),"%s/:/%s?key=%s&identifier=com.plexapp.plugins.library&X-Plex-Token=%s",s->server,current.view_count?"unscrobble":"scrobble",current.rating_key,tok);
    gui_message("Watch status","Saving watch status","O cancels.");if(network_get(url,s->client_id,"text/xml",body,sizeof(body),10))error(notice,cap,"Watch status failed");else {current.view_count=!current.view_count;if(!advanced)original->view_count=current.view_count;snprintf(notice,cap,"Watch status saved.");}}
   else if(action==3){browse_item_t next;int n=next_episode(s,&current,&next);if(n>0){current=next;advanced=1;break;}snprintf(notice,cap,"%s",n<0?"Could not load the next episode.":"No next episode in this series.");}
  }
  if(choice==3)continue;
  unsigned offset=choice==1?current.view_offset:0;int result=0,completed=0,save_failed=0,recovery_unavailable=0;
  for(;;){
   char session[80],source[4096],next[4096],stop[1400],tok[384],sid[256];snprintf(session,sizeof(session),"%s-%u-%u",s->client_id,sceKernelGetProcessTimeLow(),++serial);
   if(plex_build_playback_url(s,key,session,offset,source,sizeof(source))){snprintf(notice,cap,"Could not build playback request.");return 0;}
   gui_message(current.title,"Preparing video","Vita-compatible H.264 / AAC. O cancels.");int valid=0;
   for(int depth=0;depth<4;depth++){if(network_get(source,s->client_id,"application/vnd.apple.mpegurl",body,sizeof(body),20)){error(notice,cap,"Stream preparation failed");break;}
    int r=plex_hls_media_url(source,body,s->token,next,sizeof(next));if(r<0){snprintf(notice,cap,"Plex did not return a valid HLS stream.");break;}if(!r){valid=1;break;}snprintf(source,sizeof(source),"%s",next);}
   int progress_started=0;
   if(valid){int r=player_play_hls(source);if(r>=0){progress_started=progress_begin(s,current.rating_key,current.duration)==0;if(!progress_started)recovery_unavailable=1;player_progress_callback(progress_started?progress_update:NULL);r=player_run(current.title,current.duration,offset);}
    player_progress_callback(NULL);unsigned position=player_position();completed=player_completed();
    if(progress_started){gui_message(current.title,"Saving playback position","O cancels; unsaved progress remains on this Vita.");if(progress_finish(position))save_failed=1;}
    else if(position)save_failed=1;
    if(position){current.view_offset=position;if(!advanced)original->view_offset=position;}
    if(r==2){offset=player_seek_position();result=2;}else result=r;
    if(r<0)snprintf(notice,cap,"Playback failed (0x%X). See debug.log.",r);
   }else result=-1;
   plex_url_encode(s->token,tok,sizeof(tok));plex_url_encode(session,sid,sizeof(sid));snprintf(stop,sizeof(stop),"%s/video/:/transcode/universal/stop?session=%s&X-Plex-Token=%s",s->server,sid,tok);
   gui_message(current.title,"Closing playback session","O cancels.");if(network_get(stop,s->client_id,"text/xml",body,sizeof(body),5)){if(result>=0 && result!=2)snprintf(notice,cap,"Stream cleanup failed. Plex may keep the session briefly.");}
   if(network_exit_requested() || result==1)return 1;
   if(result==2)continue;
   if(save_failed)snprintf(notice,cap,"%s",recovery_unavailable?"Could not record progress. Check free space and retry pending saves in Settings.":"Progress not confirmed by Plex. Retry pending saves in Settings.");
   break;
  }
  if(completed && !strcmp(current.type,"episode")){
   browse_item_t next;int n=next_episode(s,&current,&next);if(n>0){int play_next=s->autoplay;
    if(!play_next){const char *rows[]={"Play next episode","Back to library"};int action=gui_choice("Episode finished",next.title,rows,2);if(action==GUI_QUIT)return 1;play_next=action==0;}
    if(play_next){current=next;advanced=1;direct=1;continue;}
   }
  }return 0;
 }
}
#else
int session_play(settings_t*s,browse_item_t*i,char*n,unsigned c){(void)s;(void)i;(void)n;(void)c;return 0;}
#endif
