#include "session.h"
#include "plex.h"
#include "plex_auth.h"
#include "network.h"
#include "progress.h"
#include "player.h"
#include "gui.h"
#include "http.h"
#include "media.h"
#include "offline.h"
#include <stdio.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
static char body[512*1024];
static unsigned serial;
static int get(const settings_t *s,const char *path,const char *sort,int offset,int count){char url[4096];if(plex_build_page_url(s->server,s->token,path,"",sort,offset,count,url,sizeof(url)))return -1;return network_get(url,s->client_id,"text/xml",body,sizeof(body),15);}
static void error(char *notice,unsigned cap,const char *what){snprintf(notice,cap,"%s (HTTP %d / error 0x%X).",what,network_last_status(),network_last_error());}
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
 gui_message("Playback tracks","Loading available tracks","O cancels.");if(get(s,key,"",0,1))return network_exit_requested()?-2:network_cancelled()?1:-1;static plex_stream_t streams[64];char part[32];int n=plex_parse_streams(body,streams,64,part,sizeof(part));
 const char *rows[66];int ids[66],count=0;
 if(type==3){rows[count]="Off";ids[count++]=-1;rows[count]="Use Plex selection";ids[count++]=-2;}
 for(int i=0;i<n;i++)if(streams[i].type==type){rows[count]=streams[i].label;ids[count++]=i;}
 if(!count){const char *back[]={"Back"};return gui_choice("Audio tracks","No selectable audio tracks reported",back,1)==GUI_QUIT?-2:0;}
 int selected=gui_choice(type==2?"Audio track":"Subtitles","Applies to this video's Plex playback preferences",rows,count);if(selected==GUI_QUIT)return -2;if(selected<0)return 1;
 if(type==3 && ids[selected]==-2){s->subtitles=1;return settings_save(s)?-1:0;}
 const char *id=ids[selected]<0?"0":streams[ids[selected]].id;if(!part[0])return -1;
 char url[1024],tok[384];plex_url_encode(s->token,tok,sizeof(tok));snprintf(url,sizeof(url),"%s/library/parts/%s?allParts=1&%sStreamID=%s&X-Plex-Token=%s",s->server,part,type==2?"audio":"subtitle",id,tok);
 gui_message("Playback tracks","Saving track selection","O cancels.");if(network_put(url,s->client_id,body,sizeof(body)))return -1;
 if(type==3){s->subtitles=strcmp(id,"0")!=0;if(settings_save(s))return -1;}return 0;
}
static int skip_access(const settings_t *s){
 static char last_token[128],last_server[256];static unsigned checked;static int allowed;
 const char *identity=s->server_id[0]?s->server_id:s->server;unsigned now=sceKernelGetProcessTimeLow();if(!strcmp(last_token,s->account_token) && !strcmp(last_server,identity) && checked && now-checked<300000000u)return allowed;
 allowed=0;checked=now;snprintf(last_token,sizeof(last_token),"%s",s->account_token);snprintf(last_server,sizeof(last_server),"%s",identity);
 char owner[1024],token[384],features[2048];int server_pass=0,owned=0;gui_message("Playback features","Checking skip intro / credits access","O cancels.");
 if(!get(s,"/","",0,1)){const char *p=strstr(body,"<MediaContainer"),*e=p?strchr(p,'>'):NULL;if(e){if(!plex_xml_attr(p,e,"ownerFeatures",features,sizeof(features))){const char *f=features;while(*f){const char *end=strchr(f,',');if(!end)end=f+strlen(f);if(end-f==4 && !memcmp(f,"pass",4))server_pass=1;f=*end?end+1:end;}}char value[8];owned=!plex_xml_attr(p,e,"owned",value,sizeof(value)) && !strcmp(value,"1");}}
 if(!server_pass || network_cancelled() || network_exit_requested())return 0;
 plex_url_encode(s->account_token,token,sizeof(token));snprintf(owner,sizeof(owner),"https://plex.tv/users/account.xml?X-Plex-Token=%s",token);
 if(!network_get(owner,s->client_id,"text/xml",body,sizeof(body),5))allowed=media_pass(body) || (owned && media_managed(body));return allowed;
}
static int prepare_stream(const settings_t *s,const char *key,int music,const char *session,unsigned offset,char source[4096]){
 if(music?plex_build_music_url(s,key,session,offset,source,4096):plex_build_playback_url(s,key,session,offset,source,4096))return -1;
 char next[4096];for(int i=0;i<4;i++){if(network_get(source,s->client_id,"application/vnd.apple.mpegurl",body,sizeof(body),20))return -1;int result=plex_hls_media_url(source,body,s->token,next,sizeof(next));if(result<0)return -1;if(!result)return 0;strcpy(source,next);}return -1;
}
static void close_stream(const settings_t *s,const char *session,int music){char url[1400],token[384],sid[256];plex_url_encode(s->token,token,sizeof(token));plex_url_encode(session,sid,sizeof(sid));snprintf(url,sizeof(url),"%s/%s/:/transcode/universal/stop?session=%s&X-Plex-Token=%s",s->server,music?"music":"video",sid,token);network_get(url,s->client_id,"text/xml",body,sizeof(body),5);}
static void download_item(const settings_t *s,const browse_item_t *item,const char *key,int music,char *notice,unsigned cap){
 if(!s->offline){snprintf(notice,cap,"Enable offline downloads in Settings first.");return;}
 gui_message(item->title,"Preparing offline download","Only explicit downloads use storage. O cancels.");if(get(s,key,"",0,1)){snprintf(notice,cap,"Could not prepare download.");return;}
 unsigned signature=offline_signature(body,s);char part[512],source[4096],session[80];int kind=!music && media_direct_part(body,s,part,sizeof(part))?1:3;snprintf(session,sizeof(session),"%s-dl-%u-%u",s->client_id,sceKernelGetProcessTimeLow(),++serial);
 if(kind==1){if(plex_build_page_url(s->server,s->token,part,"","",0,1,source,sizeof(source))){snprintf(notice,cap,"Could not build download source.");return;}}
 else if(prepare_stream(s,key,music,session,0,source)){snprintf(notice,cap,"Could not prepare compatible download.");close_stream(s,session,music);return;}
 int result=offline_download(s,item,source,kind,signature);if(kind==3){gui_message(item->title,"Closing download session","Downloaded media stays on this Vita.");close_stream(s,session,music);}
 snprintf(notice,cap,"%s",result==0?"Download ready. Settings > Manage / play downloads.":result==-2?"Download interrupted. Choose Download / resume again to continue.":result==-30?"Download storage limit reached. Delete downloads or raise the limit.":result==-33?"Media or track preferences changed. Delete the partial download before restarting.":result==-34?"All download slots are occupied. Delete one in Settings.":"Download failed. Partial data retained; retry from this item's options.");
}
void session_download(const settings_t *s,const browse_item_t *item,char *notice,unsigned cap){char key[384];if(!item->rating_key[0]){snprintf(notice,cap,"Missing media identifier.");return;}snprintf(key,sizeof(key),"/library/metadata/%s",item->rating_key);download_item(s,item,key,!strcmp(item->type,"track"),notice,cap);}
int session_play(settings_t *s,browse_item_t *original,char *notice,unsigned cap){
 if(!settings_connection_allowed(s)){snprintf(notice,cap,"Away mode needs a secure remote connection. Reconnect in Settings.");return 0;}
 notice[0]=0;browse_item_t current=*original;int advanced=0,direct=0;
 for(;;){
  if(!strcmp(current.type,"photo")){char part[256],image_url[2048];const char *image=current.thumb;
   gui_message(current.title,"Loading photo","O cancels.");if(!get(s,current.key,"",0,1) && !plex_first_part_key(body,part,sizeof(part)))image=part;
   if(network_exit_requested())return 1;if(network_cancelled())return 0;
   if(plex_build_photo_url(s,image,image_url,sizeof(image_url)) || network_download_image(image_url,"ux0:data/plex-client/photo.jpg")){error(notice,cap,"Photo could not be loaded");return 0;}
   int shown=gui_photo(current.title,"ux0:data/plex-client/photo.jpg");remove("ux0:data/plex-client/photo.jpg");if(shown==GUI_QUIT)return 1;if(shown==-3)snprintf(notice,cap,"Plex did not return a supported JPEG photo.");return 0;
  }
  int music=!strcmp(current.type,"track");
  if(!music && strcmp(current.type,"movie") && strcmp(current.type,"episode") && strcmp(current.type,"clip")){snprintf(notice,cap,"This media type cannot be played.");return 0;}
  media_marker_t markers[16];int marker_count=0,can_skip=!music && s->skip_markers?skip_access(s):0;
  char key[384];if(current.rating_key[0])snprintf(key,sizeof(key),"/library/metadata/%s",current.rating_key);else snprintf(key,sizeof(key),"%s",current.key);
  char marker_key[432];snprintf(marker_key,sizeof(marker_key),"%s?includeMarkers=1",key);
  gui_message(music?"Track details":"Video details","Loading description","O cancels.");if(!get(s,marker_key,"",0,1)){static browse_item_t details;if(plex_parse_items(body,music?"Track":"Video",&details,1)==1)current=details;if(can_skip)marker_count=media_markers(body,current.duration,markers,16);}
  if(network_exit_requested())return 1;if(network_cancelled())return 0;
  unsigned pending=progress_position(s,current.rating_key);if(pending)current.view_offset=pending;
  int choice=0;
  for(;;){if(direct){direct=0;choice=0;break;}browse_item_t shown=current;if(!s->resume)shown.view_offset=0;choice=gui_details(&shown,s->server,s->token,notice);
   if(choice==GUI_QUIT || network_exit_requested())return 1;if(choice<0)return 0;
   if(choice!=3)break;
   const char *rows[]={"Audio track","Subtitles",current.view_count?"Mark unwatched":"Mark watched","Play next episode","Download / resume download","Back"};const char *music_rows[]={"Audio track","Download / resume download","Back"};int action=gui_choice("Playback options",current.title,music?music_rows:rows,music?3:6);if((music && action==2) || (!music && action==5))continue;
   if(action==GUI_QUIT)return 1;
   if((music && action==1) || (!music && action==4)){download_item(s,&current,key,music,notice,cap);if(network_exit_requested())return 1;}
   else if(action==0 || action==1){int result=track(s,key,action==0?2:3);if(result==-2)return 1;if(result<0)error(notice,cap,"Track selection failed");else if(result==0)snprintf(notice,cap,"Playback preference saved.");}
   else if(action==2){char url[1024],tok[384];plex_url_encode(s->token,tok,sizeof(tok));snprintf(url,sizeof(url),"%s/:/%s?key=%s&identifier=com.plexapp.plugins.library&X-Plex-Token=%s",s->server,current.view_count?"unscrobble":"scrobble",current.rating_key,tok);
    gui_message("Watch status","Saving watch status","O cancels.");if(network_get(url,s->client_id,"text/xml",body,sizeof(body),10))error(notice,cap,"Watch status failed");else {current.view_count=!current.view_count;if(!advanced)original->view_count=current.view_count;snprintf(notice,cap,"Watch status saved.");}}
   else if(action==3){browse_item_t next;int n=next_episode(s,&current,&next);if(n>0){current=next;advanced=1;break;}snprintf(notice,cap,"%s",n<0?"Could not load the next episode.":"No next episode in this series.");}
  }
  if(choice==3)continue;
  unsigned offset=choice==1?current.view_offset:0;int result=0,completed=0,save_failed=0,recovery_unavailable=0,restart_paused=0;
  settings_t playback=*s;int direct_failed=0;
  for(;;){
   char session[80],source[4096],next[4096],stop[1400],tok[384],sid[256];snprintf(session,sizeof(session),"%s-%u-%u",s->client_id,sceKernelGetProcessTimeLow(),++serial);
   (void)next;int direct_play=0,valid=0;
   gui_message(current.title,"Preparing playback","Checking compatible Direct Play. O cancels.");
   if(!music && s->direct_play && !direct_failed && !get(s,key,"",0,1)){char part[512];if(media_direct_part(body,&playback,part,sizeof(part)) && !plex_build_page_url(s->server,s->token,part,"","",0,1,source,sizeof(source)))direct_play=valid=1;}
   if(!valid && !network_exit_requested() && !network_cancelled()){gui_message(current.title,music?"Preparing music":"Preparing video","Vita-compatible H.264 / AAC. O cancels.");offset-=offset%1000;valid=prepare_stream(&playback,key,music,session,offset,source)==0;if(!valid)error(notice,cap,"Stream preparation failed");}
   int progress_started=0;
   if(valid){player_set_markers(markers,(unsigned)marker_count);int r=direct_play?player_play_file(source,1,offset):player_play_hls(source);if(r>=0){progress_started=progress_begin(s,current.rating_key,current.duration)==0;if(!progress_started || !progress_durable())recovery_unavailable=1;player_progress_callback(progress_started?progress_update:NULL);player_start_paused(restart_paused);int quality=playback.connection_kind==2?playback.relay_bitrate:playback.connection_kind==1?playback.remote_bitrate:playback.bitrate;player_adaptive(s->adaptive && quality>(playback.connection_kind==2?500:1000));r=player_run_media(current.title,current.duration,offset,music);}
    player_progress_callback(NULL);unsigned position=player_position();completed=r==0 && player_completed();
    if(progress_started){gui_message(current.title,"Saving playback position","O cancels; unsaved progress remains on this Vita.");if(progress_finish(position))save_failed=1;}
    else if(position)save_failed=1;
    if(position){current.view_offset=position;if(!advanced)original->view_offset=position;}
    if(r==3){direct_failed=1;int *quality=playback.connection_kind==2?&playback.relay_bitrate:playback.connection_kind==1?&playback.remote_bitrate:&playback.bitrate;*quality=*quality>2000?2000:*quality>1000?1000:500;r=2;}
    if(r==-4)r=0; // cancelling preparation is not a decoder failure
    if(r==2 || r==4){restart_paused=player_was_paused();offset=player_seek_position();result=r;}else result=r;
    if(r<0 && direct_play && r!=-12 && !network_exit_requested() && position<=offset){direct_failed=1;result=2;restart_paused=0;}
    if(r<0 && result!=2)snprintf(notice,cap,"Playback failed at %s (0x%X). See debug.log.",player_error_stage(),r);
   }else result=-1;
   if(!direct_play){plex_url_encode(s->token,tok,sizeof(tok));plex_url_encode(session,sid,sizeof(sid));snprintf(stop,sizeof(stop),"%s/%s/:/transcode/universal/stop?session=%s&X-Plex-Token=%s",s->server,music?"music":"video",sid,tok);
   gui_message(current.title,"Closing playback session","O cancels.");int cleanup=network_get(stop,s->client_id,"text/xml",body,sizeof(body),5);if(cleanup && !network_cancelled() && !network_exit_requested() && network_last_status()!=401 && network_last_status()!=403 && network_last_status()!=404 && network_last_status()!=410)cleanup=network_get(stop,s->client_id,"text/xml",body,sizeof(body),5);if(cleanup){if(result>=0 && result!=2 && network_last_status()!=404 && network_last_status()!=410)snprintf(notice,cap,"Stream cleanup failed. Plex may keep the session briefly.");}
   }
   if(network_cancelled())completed=0;
   if(network_exit_requested() || result==1)return 1;
   if(result==4){const char *rows[]={"Audio track","Subtitles","Back to video"};int selected=gui_choice("Playback tracks",current.title,rows,3);if(selected==GUI_QUIT)return 1;if(selected==0 || selected==1){int t=track(s,key,selected==0?2:3);if(t==-2)return 1;if(t<0)error(notice,cap,"Track selection failed");}playback.subtitles=s->subtitles;continue;}
   if(result==2)continue;
   if(save_failed && result>=0)snprintf(notice,cap,"%s",recovery_unavailable?"Could not record progress. Check free space and retry pending saves in Settings.":"Progress not confirmed by Plex. Retry pending saves in Settings.");
   break;
  }
  if(result==0 && completed && !strcmp(current.type,"episode")){
   browse_item_t next;int n=next_episode(s,&current,&next);if(n>0){int play_next=gui_up_next(next.title,s->autoplay);if(play_next==GUI_QUIT)return 1;
    if(play_next){current=next;advanced=1;direct=1;continue;}
   }
  }return 0;
 }
}
#else
void session_download(const settings_t*s,const browse_item_t*i,char*n,unsigned c){(void)s;(void)i;(void)n;(void)c;}
int session_play(settings_t*s,browse_item_t*i,char*n,unsigned c){(void)s;(void)i;(void)n;(void)c;return 0;}
#endif
