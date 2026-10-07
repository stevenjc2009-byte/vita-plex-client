#include "library.h"
#include "browse.h"
#include "gui.h"
#include "network.h"
#include "http.h"
#include <stdio.h>
#include <string.h>
static int watching,polls;static char watched_server[256],watched_section[32];
static int request(const settings_t *st,const char *url,char *body,unsigned size,char *notice,unsigned cap){
 int result=network_get(url,st->client_id,"text/xml",body,size,15);
 if(result)snprintf(notice,cap,network_cancelled()?"Request cancelled.":"Library request failed (HTTP %d / error 0x%X).",network_last_status(),network_last_error());return result;
}
int library_scan(settings_t *st,const char *section,char *body,unsigned body_size,char *notice,unsigned cap) {
  if(!settings_connection_allowed(st)){snprintf(notice,cap,"Away mode needs a secure remote connection. Reconnect in Settings.");return GUI_BACK;}
  char url[4096];
  if(section && strlen(section)>=32){snprintf(notice,cap,"Library identifier is too long.");return GUI_BACK;}
  char chosen[32];snprintf(chosen,sizeof(chosen),"%s",section && *section?section:"all");
  const char *rows[]={strcmp(chosen,"all")?"Scan this library":"Scan all libraries","Choose a library to scan","Check scan status","Cancel"};
  int action=gui_choice("Scan library files","Plex checks the server's media folders for new or changed files.",rows,4);
  if(action==GUI_QUIT)return action;if(action<0 || action==3)return GUI_BACK;
  if(action==1){
    gui_message("Scan library files","Loading libraries","O cancels.");
    if(plex_build_page_url(st->server,st->token,"/library/sections","","",0,40,url,sizeof(url)) || request(st,url,body,body_size,notice,cap))return GUI_BACK;
    static browse_item_t libraries[40];int n=plex_parse_items(body,"Directory",libraries,40);const char *names[41];names[0]="All libraries";
    for(int i=0;i<n;i++)names[i+1]=libraries[i].title;
    int selected=gui_choice("Choose library","Scan runs on your Plex server",names,n+1);if(selected==GUI_QUIT)return selected;if(selected<0)return GUI_BACK;
    if(selected && strlen(libraries[selected-1].key)>=sizeof(chosen)){snprintf(notice,cap,"Library identifier is too long.");return GUI_BACK;}
    const char *key=selected?libraries[selected-1].key:"all";memcpy(chosen,key,strlen(key)+1);
  }
  if(strcmp(chosen,"all") && (!chosen[0] || strspn(chosen,"0123456789")!=strlen(chosen))){snprintf(notice,cap,"Invalid library identifier.");return GUI_BACK;}
  if(action==2){
    gui_message("Library scan status","Checking Plex","O cancels.");
    if(plex_build_page_url(st->server,st->token,"/library/sections","","",0,40,url,sizeof(url)) || request(st,url,body,body_size,notice,cap))return GUI_BACK;
    int active=0,matched=0,known=0;const char *p=body;
    while((p=strstr(p,"<Directory"))){const char *end=strchr(p,'>');if(!end)break;char key[32],refreshing[16];plex_xml_attr(p,end,"key",key,sizeof(key));plex_xml_attr(p,end,"refreshing",refreshing,sizeof(refreshing));
      if(!strcmp(chosen,"all") || !strcmp(chosen,key)){matched++;known+=refreshing[0]!=0;active+=!strcmp(refreshing,"1") || !strcmp(refreshing,"true");}p=end+1;}
    snprintf(notice,cap,"%s",!matched?"Library no longer available.":active?"Plex reports an active library scan. Check again later.":known!=matched?"Plex does not expose scan status here. Square refreshes this list.":"Plex reports no active scan. Square refreshes the displayed items.");return GUI_BACK;
  }
  const char *confirm[]={"Start scan","Cancel"};char target[100];snprintf(target,sizeof(target),"%s",!strcmp(chosen,"all")?"Scan all libraries for new media?":"Scan the selected library for new media?");
  int confirmed=gui_choice("Scan library files",target,confirm,2);if(confirmed==GUI_QUIT)return confirmed;if(confirmed!=0)return GUI_BACK;
  char path[128];snprintf(path,sizeof(path),"/library/sections/%s/refresh",chosen);
  if(plex_build_page_url(st->server,st->token,path,"","",0,1,url,sizeof(url)))return GUI_BACK;
  gui_message("Scan library files","Starting server scan","Scanning continues on Plex after you leave this screen.");
  if(!request(st,url,body,body_size,notice,cap)){watching=1;polls=0;snprintf(watched_server,sizeof(watched_server),"%s",st->server);snprintf(watched_section,sizeof(watched_section),"%s",chosen);snprintf(notice,cap,"Scan requested. Status and listings refresh automatically.");}return GUI_BACK;
}

int library_scan_active(void){return watching;}
int library_scan_poll(const settings_t *st,char *body,unsigned size,char *notice,unsigned cap){
 if(!watching)return 0;if(strcmp(watched_server,st->server) || !settings_connection_allowed(st)){watching=0;return 0;}
 char url[4096];if(plex_build_page_url(st->server,st->token,"/library/sections","","",0,40,url,sizeof(url)) || request(st,url,body,size,notice,cap)){watching=0;return -1;}
 int active=0,matched=0,known=0;const char *p=body;
 while((p=strstr(p,"<Directory"))){const char *end=strchr(p,'>');if(!end)break;char key[32],refresh[16];plex_xml_attr(p,end,"key",key,sizeof(key));plex_xml_attr(p,end,"refreshing",refresh,sizeof(refresh));
  if(!strcmp(watched_section,"all") || !strcmp(watched_section,key)){matched++;known+=refresh[0]!=0;active+=!strcmp(refresh,"1") || !strcmp(refresh,"true");}p=end+1;}
 polls++;if(!matched || (known==matched && !active) || (known!=matched && polls>=3) || polls>=120)watching=0;
 snprintf(notice,cap,"%s",active?"Plex is scanning; listings refresh automatically.":known==matched?"Scan finished. Listings refreshed.":"Plex does not expose scan status; listings refreshed.");return watching;
}
