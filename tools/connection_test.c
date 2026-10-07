#include "connection.h"
#include "network.h"
#include "gui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int calls,scenario,cancelled,exiting;static char seen[8][1024];
void gui_message(const char*a,const char*b,const char*c){(void)a;(void)b;(void)c;}
int network_exit_requested(void){return exiting;}int network_cancelled(void){return cancelled;}
int network_get(const char*u,const char*c,const char*a,char*b,unsigned cap,unsigned deadline){
 assert(c && a && deadline>0 && calls<8);snprintf(seen[calls],sizeof(seen[0]),"%s",u);calls++;
 if(scenario==1){cancelled=1;return -2;}if(scenario==2){exiting=1;return -2;}
 if(scenario==3)return -1;
 if(scenario==4 && calls==1){snprintf(b,cap,"<?xml version='1.0'?><MediaContainer machineIdentifier='wrong'/>");return 0;}
 if(scenario==5){snprintf(b,cap,"<html>login</html>");return 0;}
 if(scenario==0 && calls==1)return -1;
 snprintf(b,cap,"<?xml version='1.0'?><MediaContainer machineIdentifier='machine-1'/>");return 0;
}
static void reset(int test){calls=cancelled=exiting=0;scenario=test;memset(seen,0,sizeof(seen));}
int main(void){
 const char *xml="<MediaContainer><Device provides='server' name='Home' clientIdentifier='machine-1' accessToken='secret&amp;token'><Connection uri='http://192.168.0.32:32400' local='1'/><Connection uri='https://home.plex.direct:32400' local='1'/><Connection uri='https://direct.plex.direct:32400' local='0'/><Connection uri='https://relay.plex.direct:443' local='0' relay='1'/><Connection uri='http://public:32400' local='0'/></Device></MediaContainer>";
 plex_server_t server;assert(plex_parse_servers(xml,&server,1)==1);
 settings_t current={0},out={0},before;strcpy(current.server,"http://192.168.0.32:32400");strcpy(current.server_id,"machine-1");strcpy(current.client_id,"vita");strcpy(current.account_token,"account");current.remote_mode=1;before=current;
 assert(!settings_connection_allowed(&current));
 strcpy(out.server,"unchanged");settings_t unchanged=out;
 reset(0);assert(connection_probe(&current,&server,&out)==1 && calls==2 && !strcmp(out.server,"https://relay.plex.direct:443") && out.connection_kind==2 && settings_connection_allowed(&out));
 assert(strstr(seen[0],"https://direct.") && strstr(seen[1],"https://relay.") && strstr(seen[0],"secret%26token"));assert(!memcmp(&current,&before,sizeof(current)));
 for(int test=1;test<=3;test++){reset(test);out=unchanged;assert(connection_probe(&current,&server,&out)==(test==1?-1:test==2?-2:0));assert(!memcmp(&out,&unchanged,sizeof(out)));assert(calls==(test==3?2:1));}
 reset(4);out=unchanged;assert(connection_probe(&current,&server,&out)==1 && calls==2 && out.connection_kind==2);
 reset(5);out=unchanged;assert(!connection_probe(&current,&server,&out) && !memcmp(&out,&unchanged,sizeof(out)));
 reset(6);current.remote_mode=0;assert(connection_probe(&current,&server,&out)==1 && calls==1 && !strcmp(out.server,"https://home.plex.direct:32400") && !out.connection_kind);
 int order[8];assert(plex_connection_order(&server,0,order,1)==1 && order[0]==1);assert(!plex_connection_order(&server,0,order,0));
 server.token[0]=0;current.remote_mode=1;reset(6);assert(connection_probe(&current,&server,&out)==1 && strstr(seen[0],"Token=account"));
 strcpy(server.connections[0],"https://user:pass@host:32400");server.local[0]=0;server.connection_count=1;reset(6);assert(!connection_probe(&current,&server,&out) && !calls);
 strcpy(server.connections[0],"http://public:32400");reset(6);assert(!connection_probe(&current,&server,&out) && !calls);
 current.account_token[0]=current.token[0]=0;reset(6);assert(!connection_probe(&current,&server,&out) && !calls);
 current.remote_mode=1;strcpy(current.server,"https://home.plex.direct:32400");strcpy(current.server_id,"machine-1");current.connection_kind=0;assert(!settings_connection_allowed(&current));
 current.remote_mode=1;current.server_id[0]=0;strcpy(current.server,"https://manual.example:32400");assert(settings_connection_allowed(&current));strcpy(current.server,"http://manual.example:32400");assert(!settings_connection_allowed(&current));
 puts("Actual connection probing: HTTPS fallback, identity, tokens, cancellation, exit, failure rollback and Away guard passed");return 0;
}
