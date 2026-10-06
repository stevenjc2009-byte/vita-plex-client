#include "browse.h"
#include "plex.h"
#include "plex_auth.h"
#include "settings.h"
#include "http.h"
#include "player.h"
#include "update.h"
#include "gui.h"
#include <stdio.h>
#include <string.h>
#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include "debugScreen.h"

// Decoded posters, font atlases, XML pages and AvPlayer's generic allocations
// share this heap. The default newlib heap is too small for large libraries.
unsigned int sceLibcHeapSize=32*1024*1024;
typedef struct {
  char title[160],path[384],search[128],section[32];
  int offset,cursor;
} location_t;
static location_t locations[32];
static browse_item_t items[BROWSE_MAX_ITEMS];
static char body[512*1024],notice[256],url[4096];
static const char *sorts[]={"titleSort:asc","addedAt:desc","year:desc"};
static const char *sort_names[]={"Title A-Z","Recently added","Newest year"};

static void save(settings_t *st) {
  if(settings_save(st)<0)snprintf(notice,sizeof(notice),"Could not save settings. Check free space in ux0:data.");
}
static int request(settings_t *st,const char *target,const char *accept) {
  if(http_get(target,st->client_id,accept,body,sizeof(body))==0)return 0;
  int status=http_last_status();
  if(status==401 || status==403)snprintf(notice,sizeof(notice),"Server denied access (%d). Check the token and server in Settings.",status);
  else snprintf(notice,sizeof(notice),"Server request failed (HTTP %d, error 0x%X). Square retries; SELECT opens Settings.",status,http_last_error());
  return -1;
}
static int discover(settings_t *st) {
  static plex_server_t servers[16];char tok[384];const char *rows[16];
  plex_url_encode(st->account_token[0]?st->account_token:st->token,tok,sizeof(tok));
  snprintf(url,sizeof(url),"https://plex.tv/api/resources?includeHttps=1&includeRelay=1&X-Plex-Token=%s",tok);
  gui_message("Find servers","Looking for your Plex servers","This reads the servers registered to your Plex account.");
  if(request(st,url,"text/xml"))return 0;
  int n=plex_parse_servers(body,servers,16);
  if(!n){snprintf(notice,sizeof(notice),"No Plex servers found. Enter the LAN server address manually.");return 0;}
  for(int i=0;i<n;i++)rows[i]=servers[i].name[0]?servers[i].name:servers[i].url;
  int selected=gui_choice("Choose server","LAN connections are preferred when available",rows,n);
  if(selected==GUI_QUIT)return -1;
  if(selected>=0) {
    if(settings_server_url(servers[selected].url)<0){snprintf(notice,sizeof(notice),"The selected server address is invalid.");return 0;}
    snprintf(st->server,sizeof(st->server),"%s",servers[selected].url);
    if(servers[selected].token[0])snprintf(st->token,sizeof(st->token),"%s",servers[selected].token);
    else snprintf(st->token,sizeof(st->token),"%s",st->account_token);
    save(st);snprintf(notice,sizeof(notice),"Selected %s",servers[selected].name);return 1;
  }
  return 0;
}
static int settings_screen(settings_t *st,const char *section) {
  for(;;) {
    char server[320],quality[100],resume[80],sorting[80];
    snprintf(server,sizeof(server),"Server: %s",st->server);
    snprintf(quality,sizeof(quality),"Video quality: %d Mbps (H.264 / AAC)",st->bitrate/1000);
    snprintf(resume,sizeof(resume),"Resume playback: %s",st->resume?"On":"Off");
    snprintf(sorting,sizeof(sorting),"Sort: %s",sort_names[st->sort]);
    const char *rows[]={server,"Find and select Plex server","Enter / replace Plex token",quality,resume,sorting,
      "Refresh libraries",section && *section?"Scan this library for new media":"Scan all libraries for new media",
      "Check for app updates","About / controls","Back"};
    int choice=gui_choice("Settings",notice[0]?notice:"Connection, playback and library preferences",rows,11);
    if(choice==GUI_QUIT)return GUI_QUIT;
    if(choice<0 || choice==10)return GUI_BACK;
    if(choice==0) {
      char candidate[256];snprintf(candidate,sizeof(candidate),"%s",st->server);
      int r=gui_keyboard("Server address",candidate,sizeof(candidate),0);if(r==GUI_QUIT)return r;
      if(r==0) {
        if(settings_server_url(candidate)<0){snprintf(notice,sizeof(notice),"Use http://IP:32400 or https://hostname:port.");continue;}
        snprintf(st->server,sizeof(st->server),"%s",candidate);save(st);return GUI_HOME;
      }
    } else if(choice==1) {int r=discover(st);if(r<0)return GUI_QUIT;if(r>0)return GUI_HOME;}
    else if(choice==2) {
      char token[128];snprintf(token,sizeof(token),"%s",st->account_token);
      int r=gui_keyboard("Plex token",token,sizeof(token),1);if(r==GUI_QUIT)return r;
      if(r==0){snprintf(st->token,sizeof(st->token),"%s",token);snprintf(st->account_token,sizeof(st->account_token),"%s",token);save(st);return GUI_HOME;}
    } else if(choice==3) {st->bitrate=st->bitrate==1000?2000:st->bitrate==2000?4000:1000;save(st);}
    else if(choice==4) {st->resume=!st->resume;save(st);}
    else if(choice==5) {st->sort=(st->sort+1)%3;save(st);}
    else if(choice==6)return GUI_HOME;
    else if(choice==7) {
      const char *confirm[]={"Start server scan","Cancel"};
      int r=gui_choice("Scan media files","Plex will scan for new media in the background.",confirm,2);
      if(r==GUI_QUIT)return r;
      if(r==0) {
        char path[128];snprintf(path,sizeof(path),"/library/sections/%s/refresh",section && *section?section:"all");
        plex_build_page_url(st->server,st->token,path,"","",0,1,url,sizeof(url));
        gui_message("Library scan","Asking Plex to scan","You can refresh the library after Plex finishes scanning.");
        if(!request(st,url,"text/xml"))snprintf(notice,sizeof(notice),"Server scan requested. Refresh after Plex finishes.");
      }
    } else if(choice==8) {
      char download[1024],tag[64];gui_message("App update","Checking GitHub releases","This can take a few seconds.");
      int r=update_check(download,sizeof(download),tag,sizeof(tag));
      if(r<0)snprintf(notice,sizeof(notice),"Update check failed. Try again when connected.");
      else if(!r)snprintf(notice,sizeof(notice),"This build is current.");
      else {
        const char *rows2[]={"Download update VPK","Cancel"};
        int r2=gui_choice("Update available",tag,rows2,2);if(r2==GUI_QUIT)return r2;
        if(r2==0){gui_message("App update","Downloading VPK","Install ux0:data/plex-client/update.vpk with VitaShell after exiting.");
          snprintf(notice,sizeof(notice),"%s",update_download(download,NULL)==0?"Downloaded. Install update.vpk using VitaShell.":"Download failed; no incomplete VPK was retained.");}
      }
    } else if(choice==9) {
      const char *rows2[]={"Back"};
      if(gui_choice("Plex for Vita " APP_VERSION,"X opens / pauses; O returns / stops. Triangle searches; Square refreshes; L/R changes pages.",rows2,1)==GUI_QUIT)return GUI_QUIT;
    }
  }
}
static int login(settings_t *st) {
  plex_pin_t pin={0};
  for(;;) {
    const char *rows[]={pin.pin_id?"Check link approval":"Get Plex link code","Enter Plex token manually","Settings"};
    char subtitle[256];
    if(pin.pin_id)snprintf(subtitle,sizeof(subtitle),"Visit plex.tv/link and enter %s, then check approval.",pin.code);
    else snprintf(subtitle,sizeof(subtitle),"%s",notice[0]?notice:"Link your account once to browse your Plex libraries.");
    int r=gui_choice("Sign in to Plex",subtitle,rows,3);
    if(r==GUI_QUIT || r==GUI_BACK)return GUI_QUIT;
    if(r==2){if(settings_screen(st,NULL)==GUI_QUIT)return GUI_QUIT;if(st->token[0])return 0;continue;}
    if(r==1) {
      char token[128]={0};int k=gui_keyboard("Enter Plex token",token,sizeof(token),1);
      if(k==GUI_QUIT)return k;
      if(k==0 && token[0]){snprintf(st->token,sizeof(st->token),"%s",token);snprintf(st->account_token,sizeof(st->account_token),"%s",token);save(st);return 0;}
    } else if(!pin.pin_id) {
      plex_pin_create_url(url,sizeof(url));gui_message("Plex sign in","Requesting a link code","If plex.tv is unavailable, you can enter a token manually.");
      if(http_post_pins(url,st->client_id,body,sizeof(body)) || plex_parse_pin_create(body,&pin))
        snprintf(notice,sizeof(notice),"Link request failed (HTTP %d). Try manual token entry.",http_last_status());
    } else {
      plex_pin_poll_url(pin.pin_id,url,sizeof(url));gui_message("Plex sign in","Checking approval","Your account token will be saved on this Vita.");
      if(!request(st,url,"application/json") && !plex_parse_auth_token(body,st->token,sizeof(st->token))) {
        snprintf(st->account_token,sizeof(st->account_token),"%s",st->token);save(st);notice[0]=0;return 0;
      }
      if(http_last_status()==404 || http_last_status()==410)memset(&pin,0,sizeof(pin));
      snprintf(notice,sizeof(notice),"Not approved yet. Enter the code at plex.tv/link.");
    }
  }
}
static int play(settings_t *st,browse_item_t *it) {
  char key[384];snprintf(key,sizeof(key),"%s",it->key);
  if(it->rating_key[0])snprintf(key,sizeof(key),"/library/metadata/%s",it->rating_key);
  if(strcmp(it->type,"movie") && strcmp(it->type,"episode") && strcmp(it->type,"clip")) {
    const char *rows[]={"Back"};
    return gui_choice("Unsupported media","This player supports video. Music and photo libraries remain browsable.",rows,1)==GUI_QUIT?1:0;
  }
  gui_message("Video details","Loading description","Preparing movie or episode information.");
  if(!plex_build_page_url(st->server,st->token,key,"","",0,1,url,sizeof(url)) && !request(st,url,"text/xml")) {
    static browse_item_t details;
    if(plex_parse_items(body,"Video",&details,1)==1)*it=details;
  }
  browse_item_t shown=*it;
  if(!st->resume)shown.view_offset=0;
  int choice=gui_details(&shown,st->server,st->token,notice);
  if(choice==GUI_QUIT)return 1;if(choice<0)return 0;
  unsigned offset=choice==1?it->view_offset:0;
  char session[80],source[4096],next[4096];
  snprintf(session,sizeof(session),"%s-%u",st->client_id,sceKernelGetProcessTimeLow());
  if(plex_build_playback_url(st,key,session,offset,source,sizeof(source))<0) {
    snprintf(notice,sizeof(notice),"Playback URL could not be built.");return 0;
  }
  gui_message(it->title,"Starting video","Preparing a Vita-compatible H.264 / AAC stream. O cancels during buffering.");
  // Validate the playlist before invoking the decoder; resolve a master playlist
  // to its media playlist and retain authentication on relative variant URLs.
  int valid=0;
  for(int depth=0;depth<4;depth++) {
    if(request(st,source,"application/vnd.apple.mpegurl"))break;
    int r=plex_hls_media_url(source,body,st->token,next,sizeof(next));
    if(r<0){snprintf(notice,sizeof(notice),"Plex did not return a playable HLS stream. Check server transcoding.");break;}
    if(r==0){valid=1;break;}
    snprintf(source,sizeof(source),"%s",next);
  }
  int result=0;
  if(valid) {
    int r=player_play_hls(source);if(r>=0)r=player_run(it->title,it->duration,offset);
    unsigned position=player_position();
    if(position && it->rating_key[0]) {
      char tok[384];plex_url_encode(st->token,tok,sizeof(tok));
      snprintf(url,sizeof(url),"%s/:/timeline?ratingKey=%s&key=%s&state=stopped&time=%u&duration=%u&X-Plex-Token=%s",
        st->server,it->rating_key,key,position,it->duration,tok);
      gui_message(it->title,"Returning to your library","Saving playback position.");
      http_get(url,st->client_id,"text/xml",body,sizeof(body));it->view_offset=position;
    }
    if(r==1)result=1;
    if(r<0)snprintf(notice,sizeof(notice),"Video could not start (0x%X). Check server transcoding and debug.log.",r);
    else notice[0]=0;
  }
  char tok[384],sid[240];plex_url_encode(st->token,tok,sizeof(tok));plex_url_encode(session,sid,sizeof(sid));
  snprintf(url,sizeof(url),"%s/video/:/transcode/universal/stop?session=%s&X-Plex-Token=%s",st->server,sid,tok);
  http_get(url,st->client_id,"text/xml",body,sizeof(body));
  return result;
}
int main(void) {
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  psvDebugScreenInit();sceIoMkdir("ux0:data/plex-client",0777);
  SceUID log=sceIoOpen("ux0:data/plex-client/debug.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
  if(log>=0){sceIoWrite(log,"Plex Vita " APP_VERSION "\n",sizeof("Plex Vita " APP_VERSION "\n")-1);sceIoClose(log);}
  if(gui_init()<0){psvDebugScreenPrintf("Cannot load Plex interface. Reinstall the complete VPK.\n");sceKernelDelayThread(4000000);sceKernelExitProcess(1);return 1;}
  gui_message("Plex for Vita","Connecting","Loading your account and saved server settings.");
  settings_t st;settings_load(&st);save(&st);
  int net=http_init();if(net<0)snprintf(notice,sizeof(notice),"Network setup failed (0x%X). Reconnect Wi-Fi and restart.",net);
  int depth=0,count=0,total=0,fetch=1;
  snprintf(locations[0].title,sizeof(locations[0].title),"Your libraries");
  snprintf(locations[0].path,sizeof(locations[0].path),"/library/sections");
  for(;;) {
    if(!st.token[0]){if(login(&st)==GUI_QUIT)break;depth=0;fetch=1;}
    location_t *loc=locations+depth;
    if(fetch) {
      gui_message(loc->title,loc->search[0]?"Searching Plex":"Loading library",loc->search[0]?loc->search:st.server);
      count=total=0;
      if(plex_build_page_url(st.server,st.token,loc->path,loc->search,
        depth && !strstr(loc->path,"/children")?sorts[st.sort]:"",loc->offset,BROWSE_MAX_ITEMS,url,sizeof(url))==0 && !request(&st,url,"text/xml")) {
        browse_page_t page={0};
        if(plex_parse_page(body,&page)<0)snprintf(notice,sizeof(notice),"Unexpected server response. Check server address in Settings.");
        else {
          count=plex_parse_items(body,depth?NULL:"Directory",items,BROWSE_MAX_ITEMS);total=page.total;
          if(loc->offset && page.offset!=loc->offset){count=0;snprintf(notice,sizeof(notice),"Server did not return the requested page. Square retries.");}
          else if(total<loc->offset+count)total=loc->offset+count;
          else notice[0]=0;
        }
      }
      if(!count && !notice[0])snprintf(notice,sizeof(notice),"%s",loc->search[0]?"No matches. Triangle changes your search.":"No items. Square refreshes; SELECT opens Settings.");
      fetch=0;
    }
    char subtitle[240];snprintf(subtitle,sizeof(subtitle),"%s%s%s",depth?sort_names[st.sort]:"Choose a library to explore",loc->search[0]?"  |  Search: ":"",loc->search);
    gui_view_t view={.title=loc->title,.subtitle=subtitle,.notice=notice,.server=st.server,.token=st.token,
      .items=items,.n=count,.libraries=depth==0,.offset=loc->offset,.total=total,.cursor=loc->cursor};
    int action=gui_browse_view(&view);loc->cursor=view.cursor;
    if(action==GUI_QUIT)break;
    if(action==GUI_HOME){depth=0;locations[0].offset=locations[0].cursor=0;fetch=1;}
    else if(action==GUI_BACK){if(depth){depth--;fetch=1;}else {int r=settings_screen(&st,NULL);if(r==GUI_QUIT)break;fetch=1;}}
    else if(action==GUI_SETTINGS){int r=settings_screen(&st,loc->section);if(r==GUI_QUIT)break;if(r==GUI_HOME)depth=0;fetch=1;}
    else if(action==GUI_REFRESH){notice[0]=0;fetch=1;}
    else if(action==GUI_NEXT){loc->offset+=BROWSE_MAX_ITEMS;loc->cursor=0;fetch=1;}
    else if(action==GUI_PREVIOUS){loc->offset=loc->offset>BROWSE_MAX_ITEMS?loc->offset-BROWSE_MAX_ITEMS:0;loc->cursor=0;fetch=1;}
    else if(action==GUI_SEARCH) {
      char query[128];snprintf(query,sizeof(query),"%s",loc->search);
      int k=gui_keyboard("Search movies and series",query,sizeof(query),0);if(k==GUI_QUIT)break;
      if(k==0 && depth<31) {
        location_t *next=locations+depth+1;memset(next,0,sizeof(*next));
        snprintf(next->title,sizeof(next->title),"Search results");snprintf(next->search,sizeof(next->search),"%s",query);
        snprintf(next->section,sizeof(next->section),"%s",loc->section);
        if(loc->section[0])snprintf(next->path,sizeof(next->path),"/library/sections/%s/all",loc->section);
        else snprintf(next->path,sizeof(next->path),"/library/all");
        depth++;fetch=1;
      }
    } else if(action>=0 && action<count) {
      browse_item_t *it=items+action;
      if(depth==0 || it->is_directory) {
        if(depth>=31){snprintf(notice,sizeof(notice),"Maximum folder depth reached.");continue;}
        location_t *next=locations+depth+1;memset(next,0,sizeof(*next));snprintf(next->title,sizeof(next->title),"%s",it->title);
        snprintf(next->section,sizeof(next->section),"%s",depth?loc->section:it->key);
        if(depth==0)snprintf(next->path,sizeof(next->path),"/library/sections/%s/all",it->key);
        else {
          snprintf(next->path,sizeof(next->path),"%s",it->key);
          if(!strstr(next->path,"/children") && !strstr(next->path,"/allLeaves") && !strncmp(it->key,"/library/metadata/",18))
            snprintf(next->path,sizeof(next->path),"%s/children",it->key);
        }
        depth++;fetch=1;
      } else if(play(&st,it))break;
    }
  }
  player_stop();gui_shutdown();sceKernelExitProcess(0);return 0;
}
#else
int main(void){puts("Use tools/run-regression-tests.ps1 for desktop tests.");return 0;}
#endif
