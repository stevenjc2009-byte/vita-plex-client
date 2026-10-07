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
#include "connection.h"
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
  size_t base=strlen(st->server);
  if(!settings_connection_allowed(st) && !strncmp(target,st->server,base) && target[base]=='/'){
    snprintf(notice,sizeof(notice),"Away mode needs a secure remote connection. Reconnect in Settings.");return -1;}

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
    settings_t candidate;int result=connection_probe(st,servers+selected,&candidate);
    if(result==-2)return -1;
    if(result==-1){snprintf(notice,sizeof(notice),"Connection check cancelled. Saved server unchanged.");return 0;}
    if(!result){snprintf(notice,sizeof(notice),"No suitable connection responded. Check Plex Remote Access, Wi-Fi and TLS diagnostics.");return 0;}
    if(save(&candidate))return 0;
    int pending_failed=progress_rebind(st,&candidate);
    *st=candidate;if(pending_failed){snprintf(notice,sizeof(notice),"Connected; progress migration failed. Check free space.");return 1;}snprintf(notice,sizeof(notice),"Selected %s (%s)",servers[selected].name,st->connection_kind==2?"Relay / 1 Mbps":st->connection_kind==1?"remote HTTPS":"home network");return 1;
  }
  return 0;
}
static int profiles_menu(settings_t *st,int account){
 const int slots=account?4:8;static settings_t profiles[8];char labels[9][160];const char *rows[9];int present[8];
 for(int i=0;i<slots;i++){present[i]=!settings_profile_load(account,i,profiles+i);snprintf(labels[i],sizeof(labels[i]),"%d. %.120s",i+1,present[i]?(profiles[i].profile_name[0]?profiles[i].profile_name:profiles[i].server):"Empty slot");rows[i]=labels[i];}
 rows[slots]="Save current account / server";int action=gui_choice(account?"Saved accounts":"Server favorites","Select to reconnect; saving uses a named slot on this Vita",rows,slots+1);
 if(action==GUI_QUIT)return GUI_QUIT;if(action<0)return 0;
 if(action==slots){int slot=gui_choice("Save to slot","Choosing an occupied slot replaces that saved profile",rows,slots);if(slot==GUI_QUIT)return slot;if(slot<0)return 0;settings_t saved=*st;snprintf(saved.profile_name,sizeof(saved.profile_name),"%s",present[slot]?profiles[slot].profile_name:"");int result=gui_keyboard(account?"Account name":"Favorite name",saved.profile_name,sizeof(saved.profile_name),0);if(result==GUI_QUIT)return result;if(result<0)return 0;
  snprintf(notice,sizeof(notice),"%s",settings_profile_save(account,slot,&saved)?"Could not save profile. Check free space.":"Profile saved on this Vita.");return 0;}
 if(!present[action])return 0;
 if(!account && strcmp(profiles[action].client_id,st->client_id)){snprintf(notice,sizeof(notice),"Switch to this favorite's saved account first.");return 0;}
 settings_t candidate=account?profiles[action]:*st;
 if(!account){snprintf(candidate.server,sizeof(candidate.server),"%s",profiles[action].server);snprintf(candidate.token,sizeof(candidate.token),"%s",profiles[action].token);snprintf(candidate.server_id,sizeof(candidate.server_id),"%s",profiles[action].server_id);candidate.connection_kind=profiles[action].connection_kind;}
 if(!settings_connection_allowed(&candidate)){snprintf(notice,sizeof(notice),"Away mode requires a secure remote endpoint. Reconnect the selected server.");return 0;}
 if(plex_build_page_url(candidate.server,candidate.token,"/identity","","",0,1,url,sizeof(url)) || request(&candidate,url,"text/xml") || (candidate.server_id[0] && !plex_server_identity(body,candidate.server_id))){snprintf(notice,sizeof(notice),"Saved server could not be verified. Find and select the server on this network.");return 0;}
 if(save(&candidate))return 0;progress_rebind(st,&candidate);*st=candidate;performance_apply(st->performance);snprintf(notice,sizeof(notice),"Saved %s selected.",account?"account":"server");return GUI_HOME;
}
static int choose_view(settings_t *st,const location_t *loc,location_t *next){
 const char *views[]={"Continue Watching","Recently Added","Unwatched","Watched","Filter by genre","Filter by year","Filter by media type","Collections","Playlists","Your libraries"};
 int selected=gui_choice("Browse Plex","Choose a view or filter",views,10);if(selected==GUI_QUIT)return -2;if(selected<0)return -1;if(selected==9)return 0;
 memset(next,0,sizeof(*next));snprintf(next->title,sizeof(next->title),"%s",views[selected]);snprintf(next->section,sizeof(next->section),"%s",loc->section);
 if(selected<=3){snprintf(next->path,sizeof(next->path),"%s",selected==0?"/library/onDeck":selected==1?"/library/recentlyAdded":selected==2?"/library/all?unwatched=1":"/library/all?unwatched=0");return 1;}
 if(selected==8){snprintf(next->path,sizeof(next->path),"/playlists");return 1;}
 if(selected==6){const char *types[]={"Movies","Series","Episodes","Artists","Albums","Tracks","Photos"};const int ids[]={1,2,4,8,9,10,13};int type=gui_choice("Media type","Filter the current library, or all libraries",types,7);if(type==GUI_QUIT)return -2;if(type<0)return -1;
  if(next->section[0])snprintf(next->path,sizeof(next->path),"/library/sections/%s/all?type=%d",next->section,ids[type]);else snprintf(next->path,sizeof(next->path),"/library/all?type=%d",ids[type]);return 1;}
 if(!next->section[0]){
  if(plex_build_page_url(st->server,st->token,"/library/sections","","",0,40,url,sizeof(url)) || request(st,url,"text/xml"))return -1;
  static browse_item_t libraries[40];const char *names[40];int n=plex_parse_items(body,"Directory",libraries,40);if(!n)return -1;for(int i=0;i<n;i++)names[i]=libraries[i].title;
  int library=gui_choice("Choose library","Filters and collections belong to a library",names,n);if(library==GUI_QUIT)return -2;if(library<0)return -1;
  if(strlen(libraries[library].key)>=sizeof(next->section) || strspn(libraries[library].key,"0123456789")!=strlen(libraries[library].key))return -1;snprintf(next->section,sizeof(next->section),"%s",libraries[library].key);
 }
 if(selected==7){snprintf(next->path,sizeof(next->path),"/library/sections/%s/collections",next->section);return 1;}
 char path[128];snprintf(path,sizeof(path),"/library/sections/%s/%s",next->section,selected==4?"genre":"year");
 if(plex_build_page_url(st->server,st->token,path,"","",0,100,url,sizeof(url)) || request(st,url,"text/xml"))return -1;
 static browse_item_t values[100];const char *names[100];int n=plex_parse_items(body,"Directory",values,100);if(!n){snprintf(notice,sizeof(notice),"No filter values were supplied by this library.");return -1;}for(int i=0;i<n;i++)names[i]=values[i].title;
 int value=gui_choice(selected==4?"Genre":"Year","Select a server-provided filter",names,n);if(value==GUI_QUIT)return -2;if(value<0)return -1;
 char encoded[768];plex_url_encode(values[value].key,encoded,sizeof(encoded));int count=snprintf(next->path,sizeof(next->path),"/library/sections/%s/all?%s=%s",next->section,selected==4?"genre":"year",encoded);if(count<0 || count>=(int)sizeof(next->path))return -1;return 1;
}
static int settings_screen(settings_t *st,const char *section) {
  int cursor=0;
  for(;;) {
    if(network_exit_requested())return GUI_QUIT;
    char server[320],quality[100],resume[80],sorting[80],clocks[80],autoplay[80],subtitles[80],remote[100],remote_quality[100],relay_quality[100],adaptive[100];
    snprintf(server,sizeof(server),"Server: %s",st->server);
    snprintf(quality,sizeof(quality),"Video quality: %d Mbps (H.264 / AAC)",st->bitrate/1000);
    snprintf(resume,sizeof(resume),"Resume playback: %s",st->resume?"On":"Off");
    snprintf(sorting,sizeof(sorting),"Sort: %s",sort_names[st->sort]);
    snprintf(clocks,sizeof(clocks),"Performance: %s",st->performance==2?"500 / 222 MHz":st->performance==1?"444 / 222 MHz":"Plugin / original clocks");
    snprintf(autoplay,sizeof(autoplay),"Autoplay next episode: %s",st->autoplay?"On":"Off");
    snprintf(subtitles,sizeof(subtitles),"Subtitles: %s",st->subtitles?"Server default / selected":"Off");
    snprintf(remote,sizeof(remote),"Connection: %s",st->remote_mode?"Away from home":"Automatic home / remote");
    snprintf(remote_quality,sizeof(remote_quality),"Remote quality: %d kbps",st->remote_bitrate);snprintf(relay_quality,sizeof(relay_quality),"Relay quality: %d kbps",st->relay_bitrate);snprintf(adaptive,sizeof(adaptive),"Adapt quality on sustained buffering: %s",st->adaptive?"On":"Off");
    const char *rows[]={server,"Find and select Plex server","Enter / replace Plex token",quality,resume,sorting,
      "Refresh libraries",section && *section?"Scan this library for new media":"Scan all libraries for new media",
      "Check for app updates","About / controls",clocks,autoplay,subtitles,"Connection diagnostics","Retry saved playback progress","Clear poster cache","Reset preferences","Unlink Plex account",remote,"Reconnect selected server","Remote access setup",remote_quality,relay_quality,adaptive,"Server favorites","Saved Plex accounts","Back"};
    int choice=gui_choice_cursor("Settings",notice[0]?notice:"Connection, playback and library preferences",rows,27,&cursor);
    if(choice==GUI_QUIT)return GUI_QUIT;
    if(choice<0 || choice==26)return GUI_BACK;
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
    else if(choice==21){st->remote_bitrate=st->remote_bitrate==1000?2000:st->remote_bitrate==2000?4000:1000;save(st);}
    else if(choice==22){st->relay_bitrate=st->relay_bitrate==1000?500:1000;save(st);}
    else if(choice==23){st->adaptive=!st->adaptive;save(st);}
    else if(choice==24 || choice==25){int r=profiles_menu(st,choice==25);if(r==GUI_QUIT || r==GUI_HOME)return r;}
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
  int depth=0,count=0,total=0,fetch=1,connection_retry=0;
  // Identify a manually configured endpoint before journaling checkpoints.
  // This lets the first subsequent LAN/WAN change use a stable server scope.
  if(net>=0 && st.token[0] && !st.server_id[0] && settings_connection_allowed(&st)){
    if(!plex_build_page_url(st.server,st.token,"/identity","","",0,1,url,sizeof(url)) && !request(&st,url,"text/xml")){
      const char *tag=strstr(body,"<MediaContainer"),*end=tag?strchr(tag,'>'):NULL;char id[128];
      if(end && !plex_xml_attr(tag,end,"machineIdentifier",id,sizeof(id)) && id[0] && !strpbrk(id,"\r\n")){
        settings_t candidate=st;snprintf(candidate.server_id,sizeof(candidate.server_id),"%s",id);if(candidate.remote_mode)candidate.connection_kind=1;
        if(!save(&candidate)){progress_rebind(&st,&candidate);st=candidate;}
      }
    }
  }
  if(net>=0 && st.token[0] && st.remote_mode && st.server_id[0]){connection_retry=1;discover(&st,1);}
  if(network_cancelled())fetch=0;
  if(net>=0 && st.token[0] && settings_connection_allowed(&st) && !network_cancelled() && !network_exit_requested() && progress_pending())progress_retry(&st);
  snprintf(locations[0].title,sizeof(locations[0].title),"Your libraries");
  snprintf(locations[0].path,sizeof(locations[0].path),"/library/sections");
  for(;;) {
    if(network_exit_requested())break;
    if(!st.token[0]){if(login(&st)==GUI_QUIT)break;depth=0;fetch=1;}
    location_t *loc=locations+depth;
    if(fetch) {
      gui_message(loc->title,loc->search[0]?"Searching Plex":"Loading library",loc->search[0]?loc->search:st.server);
      count=total=0;
      if(!settings_connection_allowed(&st) && !connection_retry && st.server_id[0]){connection_retry=1;discover(&st,1);}
      if(!settings_connection_allowed(&st)){snprintf(notice,sizeof(notice),"Away mode needs a secure remote connection. Settings > Reconnect selected server.");}
      else if(plex_build_page_url(st.server,st.token,loc->path,loc->search,
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
    if(network_exit_requested())break;
    if(settings_unsaved)snprintf(notice,sizeof(notice),"Settings are not saved. Check free space, then save a setting again.");
    char subtitle[240];snprintf(subtitle,sizeof(subtitle),"%s%s%s",depth?(strstr(loc->path,"/onDeck")?"Continue Watching":strstr(loc->path,"/recentlyAdded")?"Recently added":sort_names[st.sort]):"Choose a library to explore",loc->search[0]?"  |  Search: ":"",loc->search);
    gui_view_t view={.title=loc->title,.subtitle=subtitle,.notice=notice,.server=st.server,.token=st.token,
      .items=items,.n=count,.libraries=depth==0,.offset=loc->offset,.total=total,.cursor=loc->cursor,.poll_scan=library_scan_active()};
    int action=gui_browse_view(&view);loc->cursor=view.cursor;
    if(action==GUI_QUIT)break;
    if(action==GUI_VIEWS){location_t next;int r=choose_view(&st,loc,&next);if(r==-2)break;if(r>=0){depth=r;if(r)locations[1]=next;fetch=1;}}
    else if(action==GUI_HOME){depth=0;locations[0].offset=locations[0].cursor=0;fetch=1;}
    else if(action==GUI_BACK){if(depth){depth--;fetch=1;}else {int r=settings_screen(&st,NULL);if(r==GUI_QUIT)break;fetch=1;}}
    else if(action==GUI_SETTINGS){int old_sort=st.sort;int r=settings_screen(&st,loc->section);if(old_sort!=st.sort){loc->offset=loc->cursor=0;}if(r==GUI_QUIT)break;if(r==GUI_HOME)depth=0;fetch=1;}
    else if(action==GUI_SCAN_POLL){library_scan_poll(&st,body,sizeof(body),notice,sizeof(notice));fetch=1;}
    else if(action==GUI_SCAN){const char *section=loc->section;if(!depth && count && view.cursor>=0 && view.cursor<count)section=items[view.cursor].key;if(library_scan(&st,section,body,sizeof(body),notice,sizeof(notice))==GUI_QUIT)break;fetch=1;}
    else if(action==GUI_REFRESH){notice[0]=0;connection_retry=0;fetch=1;}
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
          if(!strcmp(it->type,"playlist") && !strstr(it->key,"/items"))snprintf(next->path,sizeof(next->path),"%s/items",it->key);
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
