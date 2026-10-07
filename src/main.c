#include "browse.h"
#include "plex.h"
#include "plex_auth.h"
#include "settings.h"
#include "http.h"
#include "player.h"
#include "update.h"
#include "gui.h"
#include "session.h"
#include "network.h"
#include "progress.h"
#include "performance.h"
#include "library.h"
#include <stdio.h>
#include <string.h>
#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/net/netctl.h>
#include "debugScreen.h"

// Decoded posters, font atlases, XML pages, bounded stream queues and codec probing
// share this heap. The default newlib heap is too small for large libraries.
unsigned int sceLibcHeapSize=64*1024*1024;
typedef struct {
  char title[160],path[384],search[128],section[32];
  int offset,cursor;
} location_t;
static location_t locations[32];
static browse_item_t items[BROWSE_MAX_ITEMS];
static char body[512*1024],notice[256],url[4096];
static int settings_unsaved;
static const char *sorts[]={"titleSort:asc","addedAt:desc","year:desc"};
static const char *sort_names[]={"Title A-Z","Recently added","Newest year"};

static int save(settings_t *st) {
  if(settings_save(st)<0){settings_unsaved=1;snprintf(notice,sizeof(notice),"Could not save settings. Check free space in ux0:data.");return -1;}settings_unsaved=0;return 0;
}
static int request(settings_t *st,const char *target,const char *accept) {
  if(network_get(target,st->client_id,accept,body,sizeof(body),15)==0)return 0;
  if(network_cancelled()){snprintf(notice,sizeof(notice),"Request cancelled.");return -1;}
  int status=network_last_status();
  if(status==401 || status==403)snprintf(notice,sizeof(notice),"Server denied access (%d). Check the token and server in Settings.",status);
  else snprintf(notice,sizeof(notice),"Connection failed at %s (HTTP %d / 0x%X). Settings: check server and Wi-Fi.",network_last_stage(),status,network_last_error());
  return -1;
}
static int discover(settings_t *st,int automatic) {
  static plex_server_t servers[16];char tok[384];const char *rows[16];
  plex_url_encode(st->account_token[0]?st->account_token:st->token,tok,sizeof(tok));
  snprintf(url,sizeof(url),"https://plex.tv/api/resources?includeHttps=1&includeRelay=1&X-Plex-Token=%s",tok);
  gui_message("Find servers","Looking for your Plex servers","This reads the servers registered to your Plex account.");
  if(request(st,url,"text/xml"))return 0;
  int n=plex_parse_servers(body,servers,16);
  if(!n){snprintf(notice,sizeof(notice),"No Plex servers found. Link your account or enter a server address.");return 0;}
  for(int i=0;i<n;i++)rows[i]=servers[i].name[0]?servers[i].name:servers[i].url;
  int selected=-1;
  if(automatic){for(int i=0;i<n;i++)if(st->server_id[0] && !strcmp(st->server_id,servers[i].id)){selected=i;break;}
    if(selected<0){snprintf(notice,sizeof(notice),"Saved server was not found. Use Find and select Plex server.");return 0;}}
  else selected=gui_choice("Choose server",st->remote_mode?"Away from home: secure remote connections only":"Automatic: home, secure remote, then Relay",rows,n);
  if(selected==GUI_QUIT)return -1;
  if(selected>=0) {
    plex_server_t *chosen=servers+selected;char probe[1024],probe_body[2048],tok[384];plex_url_encode(chosen->token[0]?chosen->token:st->account_token,tok,sizeof(tok));
    int order[8],number=plex_connection_order(chosen,st->remote_mode,order,8),kind=0;
    int reachable=0;for(int attempt=0;attempt<number;attempt++){
      int index=order[attempt];const char *candidate=chosen->connections[index];
      snprintf(probe,sizeof(probe),"%s/identity?X-Plex-Token=%s",candidate,tok);gui_message("Choose server","Checking server connection","O cancels.");
      if(!network_get(probe,st->client_id,"text/xml",probe_body,sizeof(probe_body),8)){
        if(chosen->id[0] && !plex_server_identity(probe_body,chosen->id))continue;
        snprintf(chosen->url,sizeof(chosen->url),"%s",candidate);kind=chosen->relay[index]?2:chosen->local[index]?0:1;reachable=1;break;}
      if(network_exit_requested())return -1;
      if(network_cancelled()){snprintf(notice,sizeof(notice),"Connection check cancelled. Saved server unchanged.");return 0;}
    }
    if(!reachable){snprintf(notice,sizeof(notice),"No suitable connection responded. Check Plex Remote Access, Wi-Fi and TLS diagnostics.");return 0;}
    if(settings_server_url(servers[selected].url)<0){snprintf(notice,sizeof(notice),"The selected server address is invalid.");return 0;}
    snprintf(st->server,sizeof(st->server),"%s",servers[selected].url);
    snprintf(st->server_id,sizeof(st->server_id),"%s",servers[selected].id);st->connection_kind=kind;
    if(servers[selected].token[0])snprintf(st->token,sizeof(st->token),"%s",servers[selected].token);
    else snprintf(st->token,sizeof(st->token),"%s",st->account_token);
    if(!save(st))snprintf(notice,sizeof(notice),"Selected %s (%s)",servers[selected].name,kind==2?"Relay / 1 Mbps":kind==1?"remote HTTPS":"home network");return 1;
  }
  return 0;
}
static int settings_screen(settings_t *st,const char *section) {
  int cursor=0;
  for(;;) {
    if(network_exit_requested())return GUI_QUIT;
    char server[320],quality[100],resume[80],sorting[80],clocks[80],autoplay[80],subtitles[80],remote[100];
    snprintf(server,sizeof(server),"Server: %s",st->server);
    snprintf(quality,sizeof(quality),"Video quality: %d Mbps (H.264 / AAC)",st->bitrate/1000);
    snprintf(resume,sizeof(resume),"Resume playback: %s",st->resume?"On":"Off");
    snprintf(sorting,sizeof(sorting),"Sort: %s",sort_names[st->sort]);
    snprintf(clocks,sizeof(clocks),"Performance: %s",st->performance==2?"500 / 222 MHz":st->performance==1?"444 / 222 MHz":"Plugin / original clocks");
    snprintf(autoplay,sizeof(autoplay),"Autoplay next episode: %s",st->autoplay?"On":"Off");
    snprintf(subtitles,sizeof(subtitles),"Subtitles: %s",st->subtitles?"Server default / selected":"Off");
    snprintf(remote,sizeof(remote),"Connection: %s",st->remote_mode?"Away from home":"Automatic home / remote");
    const char *rows[]={server,"Find and select Plex server","Enter / replace Plex token",quality,resume,sorting,
      "Refresh libraries",section && *section?"Scan this library for new media":"Scan all libraries for new media",
      "Check for app updates","About / controls",clocks,autoplay,subtitles,"Connection diagnostics","Retry saved playback progress","Clear poster cache","Reset preferences","Unlink Plex account",remote,"Reconnect selected server","Remote access setup","Back"};
    int choice=gui_choice_cursor("Settings",notice[0]?notice:"Connection, playback and library preferences",rows,22,&cursor);
    if(choice==GUI_QUIT)return GUI_QUIT;
    if(choice<0 || choice==21)return GUI_BACK;
    if(choice==0) {
      char candidate[256];snprintf(candidate,sizeof(candidate),"%s",st->server);
      int r=gui_keyboard("Server address",candidate,sizeof(candidate),0);if(r==GUI_QUIT)return r;
      if(r==0) {
        if(settings_server_url(candidate)<0){snprintf(notice,sizeof(notice),"Use http://IP:32400 or https://hostname:port.");continue;}
        snprintf(st->server,sizeof(st->server),"%s",candidate);st->server_id[0]=0;st->connection_kind=st->remote_mode?1:0;save(st);return GUI_HOME;
      }
    } else if(choice==1) {int r=discover(st,0);if(r<0)return GUI_QUIT;if(r>0)return GUI_HOME;}
    else if(choice==2) {
      char token[128];snprintf(token,sizeof(token),"%s",st->account_token);
      int r=gui_keyboard("Plex token",token,sizeof(token),1);if(r==GUI_QUIT)return r;
      if(r==0){if(st->account_token[0] && strcmp(st->account_token,token)){st->client_id[0]=0;settings_ensure_client_id(st);}
        st->server_id[0]=0;snprintf(st->token,sizeof(st->token),"%s",token);snprintf(st->account_token,sizeof(st->account_token),"%s",token);save(st);return GUI_HOME;}
    } else if(choice==3) {st->bitrate=st->bitrate==1000?2000:st->bitrate==2000?4000:1000;save(st);}
    else if(choice==4) {st->resume=!st->resume;save(st);}
    else if(choice==5) {st->sort=(st->sort+1)%3;save(st);}
    else if(choice==6)return GUI_HOME;
    else if(choice==7) {if(library_scan(st,section,body,sizeof(body),notice,sizeof(notice))==GUI_QUIT)return GUI_QUIT;
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
    } else if(choice==10){st->performance=(st->performance+1)%3;performance_apply(st->performance);save(st);}
    else if(choice==11){st->autoplay=!st->autoplay;save(st);}
    else if(choice==12){st->subtitles=!st->subtitles;save(st);}
    else if(choice==13){
      int wifi_state=-1;int wifi_result=sceNetCtlInetGetState(&wifi_state);
      char wifi[100];snprintf(wifi,sizeof(wifi),"Wi-Fi: %s (state %d / error %X)",wifi_result<0?"unavailable":wifi_state==SCE_NETCTL_STATE_CONNECTED?"connected":"disconnected / connecting",wifi_state,wifi_result);
      char diag[320],perf[200];performance_describe(perf,sizeof(perf));
      gui_message("Connection diagnostics","Checking Plex server","O cancels.");plex_build_page_url(st->server,st->token,"/identity","","",0,1,url,sizeof(url));
      int r=request(st,url,"text/xml");snprintf(diag,sizeof(diag),"%s | HTTP %d | Error %X | TLS %X / %X | Pending progress %d",r?"Server failed":"Server reachable",network_last_status(),network_last_error(),http_last_ssl_err(),http_last_ssl_detail(),progress_pending());
      const char *rows2[]={wifi,perf,diag,"Decoder errors: ux0:data/plex-client/debug.log","Back"};if(gui_choice("Diagnostics","Clock values are read back from the system / plugin",rows2,5)==GUI_QUIT)return GUI_QUIT;
    } else if(choice==14){
      char detail[128];int matching=progress_pending_for(st);snprintf(detail,sizeof(detail),"Pending: %d on this connection; %d on previous connections",matching,progress_pending()-matching);
      const char *actions[]={"Retry progress on this connection","Discard progress from previous connections","Discard all pending progress","Back"};int r=gui_choice("Playback recovery",detail,actions,4);if(r==GUI_QUIT)return r;
      if(r==0){if(progress_retry(st))snprintf(notice,sizeof(notice),"Some progress remains pending. Reconnect and retry.");else snprintf(notice,sizeof(notice),"This connection's progress recovered. Other connections may still have records.");}
      else if(r==1 || r==2){const char *confirm[]={"Discard these saved positions","Cancel"};int choice=gui_choice("Discard pending progress","Positions already accepted by Plex are unaffected.",confirm,2);if(choice==GUI_QUIT)return choice;if(choice==0)snprintf(notice,sizeof(notice),"%s",progress_discard(st,r==1)?"Could not save journal. Pending positions retained.":"Pending positions discarded.");}
    }
    else if(choice==15){snprintf(notice,sizeof(notice),"%s",gui_cache_clear()?"Could not clear all cached posters.":"Poster cache cleared.");}
    else if(choice==16){st->bitrate=2000;st->resume=1;st->sort=0;st->autoplay=0;st->subtitles=1;st->performance=2;performance_apply(st->performance);save(st);}
    else if(choice==17){const char *rows2[]={"Unlink account on this Vita","Cancel"};int r=gui_choice("Unlink Plex","This removes this Vita's saved tokens.",rows2,2);if(r==GUI_QUIT)return r;
      if(r==0){st->token[0]=st->account_token[0]=st->client_id[0]=st->server_id[0]=0;settings_ensure_client_id(st);if(save(st))continue;return GUI_HOME;}}
    else if(choice==18){const char *modes[]={"Automatic home / remote","Away from home","Back"};int mode=gui_choice("Connection mode","Away mode uses secure remote connections only",modes,3);if(mode==GUI_QUIT)return mode;if(mode==0 || mode==1){st->remote_mode=mode;if(save(st))continue;int result=discover(st,st->server_id[0]!=0);if(result<0)return GUI_QUIT;if(result>0)return GUI_HOME;}}
    else if(choice==19){int result=discover(st,st->server_id[0]!=0);if(result<0)return GUI_QUIT;if(result>0)return GUI_HOME;}
    else if(choice==20){const char *info[]={"Enable Remote Access in Plex Media Server settings","Link this Vita account and select your Plex server","Use another Wi-Fi network or a phone hotspot","Plex Pass / Remote Watch Pass may be required","Relay uses 1 Mbps video; direct access is preferred","Help: support.plex.tv (Remote Access)","Back"};if(gui_choice("Watch away from home","Your server must stay online; setup happens on the server",info,7)==GUI_QUIT)return GUI_QUIT;}

  }
}
static int login(settings_t *st) {
  plex_pin_t pin={0};
  for(;;) {
    if(network_exit_requested())return GUI_QUIT;
    const char *rows[]={pin.pin_id?"Check link approval":"Get Plex link code","Enter Plex token manually","Settings"};
    char subtitle[256];
    if(pin.pin_id)snprintf(subtitle,sizeof(subtitle),"Code %.16s at plex.tv/link. %.190s",pin.code,notice[0]?notice:"Then check approval.");
    else snprintf(subtitle,sizeof(subtitle),"%s",notice[0]?notice:"Link your account once to browse your Plex libraries.");
    int r=gui_choice("Sign in to Plex",subtitle,rows,3);
    if(r==GUI_QUIT || r==GUI_BACK)return GUI_QUIT;
    if(r==2){if(settings_screen(st,NULL)==GUI_QUIT)return GUI_QUIT;if(st->token[0])return 0;continue;}
    if(r==1) {
      char token[128]={0};int k=gui_keyboard("Enter Plex token",token,sizeof(token),1);
      if(k==GUI_QUIT)return k;
      if(k==0 && token[0]){if(st->account_token[0] && strcmp(st->account_token,token)){st->client_id[0]=0;settings_ensure_client_id(st);}
        st->server_id[0]=0;snprintf(st->token,sizeof(st->token),"%s",token);snprintf(st->account_token,sizeof(st->account_token),"%s",token);save(st);return 0;}
    } else if(!pin.pin_id) {
      plex_pin_create_url(url,sizeof(url));gui_message("Plex sign in","Requesting a link code","If plex.tv is unavailable, you can enter a token manually.");
      if(network_pins(url,st->client_id,body,sizeof(body)) || plex_parse_pin_create(body,&pin))
        snprintf(notice,sizeof(notice),"Link request failed at %s (HTTP %d / 0x%X). Try again or enter a token.",network_last_stage(),network_last_status(),network_last_error());
    } else {
      plex_pin_poll_url(pin.pin_id,url,sizeof(url));gui_message("Plex sign in","Checking approval","Your account token will be saved on this Vita.");
      if(request(st,url,"application/json")){if(network_last_status()==404 || network_last_status()==410)memset(&pin,0,sizeof(pin));continue;}
      if(!plex_parse_auth_token(body,st->token,sizeof(st->token))) {
        snprintf(st->account_token,sizeof(st->account_token),"%s",st->token);if(!save(st))notice[0]=0;return 0;
      }
      snprintf(notice,sizeof(notice),"Not approved yet. Enter the code at plex.tv/link.");
    }
  }
}
int main(void) {
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  psvDebugScreenInit();sceIoMkdir("ux0:data/plex-client",0777);
  SceUID log=sceIoOpen("ux0:data/plex-client/debug.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
  if(log>=0){sceIoWrite(log,"Plex Vita " APP_VERSION "\n",sizeof("Plex Vita " APP_VERSION "\n")-1);sceIoClose(log);}
  settings_t st;settings_load(&st);save(&st);performance_apply(st.performance);
  if(gui_init()<0){psvDebugScreenPrintf("Cannot load Plex interface. Reinstall the complete VPK.\n");sceKernelDelayThread(4000000);performance_restore();sceKernelExitProcess(1);return 1;}
  gui_message("Plex for Vita","Connecting","Loading your account and saved server settings.");
  sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),0x10000);
  int net=http_init();if(net<0)snprintf(notice,sizeof(notice),"Network setup failed at %s (0x%X). Square retries.",http_init_stage(),net);
  if(net>=0 && st.token[0] && progress_pending())progress_retry(&st);
  int depth=0,count=0,total=0,fetch=1,connection_retry=0;
  if(net>=0 && st.token[0] && st.remote_mode && st.server_id[0]){connection_retry=1;discover(&st,1);}
  snprintf(locations[0].title,sizeof(locations[0].title),"Your libraries");
  snprintf(locations[0].path,sizeof(locations[0].path),"/library/sections");
  for(;;) {
    if(network_exit_requested())break;
    if(!st.token[0]){if(login(&st)==GUI_QUIT)break;depth=0;fetch=1;}
    location_t *loc=locations+depth;
    if(fetch) {
      gui_message(loc->title,loc->search[0]?"Searching Plex":"Loading library",loc->search[0]?loc->search:st.server);
      count=total=0;
      if(plex_build_page_url(st.server,st.token,loc->path,loc->search,
        strstr(loc->path,"/onDeck")?"":strstr(loc->path,"/recentlyAdded")?sorts[1]:depth && !strstr(loc->path,"/children")?sorts[st.sort]:"",loc->offset,BROWSE_MAX_ITEMS,url,sizeof(url))==0 && !request(&st,url,"text/xml")) {
        connection_retry=0;browse_page_t page={0};
        if(plex_parse_page(body,&page)<0)snprintf(notice,sizeof(notice),"Unexpected server response. Check server address in Settings.");
        else {
          int local_skip=page.offset==0 && loc->offset>0 && (depth==0 || page.size> BROWSE_MAX_ITEMS)?loc->offset:0;
          count=plex_parse_items_offset(body,depth?NULL:"Directory",items,BROWSE_MAX_ITEMS,local_skip);total=page.total;
          if(!count && loc->offset>0 && total<=loc->offset){loc->offset=total>0?((total-1)/BROWSE_MAX_ITEMS)*BROWSE_MAX_ITEMS:0;loc->cursor=0;fetch=1;continue;}
          if(loc->offset && !local_skip && page.offset!=loc->offset){count=0;snprintf(notice,sizeof(notice),"Server did not return the requested page. Square retries.");}
          else if(total<loc->offset+count)total=loc->offset+count;
          else notice[0]=0;
        }
      }
      else if(!connection_retry && st.server_id[0] && !network_cancelled() && !network_exit_requested()){
        connection_retry=1;if(discover(&st,1)>0){fetch=1;continue;}}
      if(!count && !notice[0])snprintf(notice,sizeof(notice),"%s",loc->search[0]?"No matches. Triangle changes your search.":"No items. Square refreshes; SELECT opens Settings.");
      fetch=0;
    }
    if(settings_unsaved)snprintf(notice,sizeof(notice),"Settings are not saved. Check free space, then save a setting again.");
    char subtitle[240];snprintf(subtitle,sizeof(subtitle),"%s%s%s",depth?(strstr(loc->path,"/onDeck")?"Continue Watching":strstr(loc->path,"/recentlyAdded")?"Recently added":sort_names[st.sort]):"Choose a library to explore",loc->search[0]?"  |  Search: ":"",loc->search);
    gui_view_t view={.title=loc->title,.subtitle=subtitle,.notice=notice,.server=st.server,.token=st.token,
      .items=items,.n=count,.libraries=depth==0,.offset=loc->offset,.total=total,.cursor=loc->cursor};
    int action=gui_browse_view(&view);loc->cursor=view.cursor;
    if(action==GUI_QUIT)break;
    if(action==GUI_VIEWS){const char *views[]={"Continue Watching","Recently Added","Unwatched","Your libraries"};int r=gui_choice("Home","Choose a Plex view",views,4);if(r==GUI_QUIT)break;
      if(r>=0){if(r==3){depth=0;}else{depth=1;memset(locations+1,0,sizeof(*locations));snprintf(locations[1].title,sizeof(locations[1].title),"%s",views[r]);snprintf(locations[1].path,sizeof(locations[1].path),"%s",r==0?"/library/onDeck":r==1?"/library/recentlyAdded":"/library/all?unwatched=1");}fetch=1;}}
    else if(action==GUI_HOME){depth=0;locations[0].offset=locations[0].cursor=0;fetch=1;}
    else if(action==GUI_BACK){if(depth){depth--;fetch=1;}else {int r=settings_screen(&st,NULL);if(r==GUI_QUIT)break;fetch=1;}}
    else if(action==GUI_SETTINGS){int old_sort=st.sort;int r=settings_screen(&st,loc->section);if(old_sort!=st.sort){loc->offset=loc->cursor=0;}if(r==GUI_QUIT)break;if(r==GUI_HOME)depth=0;fetch=1;}
    else if(action==GUI_SCAN){const char *section=loc->section;if(!depth && count && view.cursor>=0 && view.cursor<count)section=items[view.cursor].key;if(library_scan(&st,section,body,sizeof(body),notice,sizeof(notice))==GUI_QUIT)break;}
    else if(action==GUI_REFRESH){notice[0]=0;fetch=1;}
    else if(action==GUI_NEXT){loc->offset+=BROWSE_MAX_ITEMS;loc->cursor=0;fetch=1;}
    else if(action==GUI_PREVIOUS){loc->offset=loc->offset>BROWSE_MAX_ITEMS?loc->offset-BROWSE_MAX_ITEMS:0;loc->cursor=0;fetch=1;}
    else if(action==GUI_SEARCH) {
      char query[128];snprintf(query,sizeof(query),"%s",loc->search);
      int k=gui_keyboard("Search movies and series",query,sizeof(query),0);if(k==GUI_QUIT)break;
      if(k==0 && depth<31) {
        location_t *next=locations+depth+1;memset(next,0,sizeof(*next));
        snprintf(next->title,sizeof(next->title),"Search results");snprintf(next->search,sizeof(next->search),"%s",query);
        memcpy(next->section,loc->section,sizeof(next->section));
        if(loc->section[0])snprintf(next->path,sizeof(next->path),"/library/sections/%s/all",loc->section);
        else snprintf(next->path,sizeof(next->path),"/library/all");
        depth++;fetch=1;
      }
    } else if(action>=0 && action<count) {
      browse_item_t *it=items+action;
      if(depth==0 || it->is_directory) {
        if(depth>=31){snprintf(notice,sizeof(notice),"Maximum folder depth reached.");continue;}
        location_t *next=locations+depth+1;memset(next,0,sizeof(*next));snprintf(next->title,sizeof(next->title),"%s",it->title);
        if(depth)memcpy(next->section,loc->section,sizeof(next->section));
        else {if(strlen(it->key)>=sizeof(next->section)){snprintf(notice,sizeof(notice),"Library identifier is too long.");continue;}memcpy(next->section,it->key,strlen(it->key)+1);}
        if(depth==0)snprintf(next->path,sizeof(next->path),"/library/sections/%s/all",it->key);
        else {
          snprintf(next->path,sizeof(next->path),"%s",it->key);
          if(!strstr(next->path,"/children") && !strstr(next->path,"/allLeaves") && !strncmp(it->key,"/library/metadata/",18))
            snprintf(next->path,sizeof(next->path),"%s/children",it->key);
        }
        depth++;fetch=1;
      } else if(session_play(&st,it,notice,sizeof(notice)))break;
    }
  }
  player_stop();gui_shutdown();http_shutdown();performance_restore();sceKernelExitProcess(0);return 0;
}
#else
int main(void){puts("Use tools/run-regression-tests.ps1 for desktop tests.");return 0;}
#endif
