#include "connection.h"
#include "network.h"
#include "gui.h"
#include "plex_auth.h"
#include <stdio.h>
#include <string.h>
int connection_probe(const settings_t *current,const plex_server_t *server,settings_t *connected){
 if(!current || !server || !connected)return 0;
 int order[8],n=plex_connection_order(server,current->remote_mode,order,8);
 const char *token=server->token[0]?server->token:current->account_token[0]?current->account_token:current->token;
 if(!*token || !server->id[0] || strpbrk(token,"\r\n") || strpbrk(server->id,"\r\n"))return 0;
 char encoded[384],url[1024],body[2048];plex_url_encode(token,encoded,sizeof(encoded));
 for(int i=0;i<n;i++){
  if(network_exit_requested())return -2;
  int index=order[i];const char *candidate=server->connections[index];
  int length=snprintf(url,sizeof(url),"%s/identity?X-Plex-Token=%s",candidate,encoded);if(length<0 || (unsigned)length>=sizeof(url))continue;
  gui_message("Choose server","Checking server connection","O cancels.");
  int result=network_get(url,current->client_id,"text/xml",body,sizeof(body),15);
  if(network_exit_requested())return -2;
  if(network_cancelled())return -1;
  if(result || !plex_server_identity(body,server->id))continue;
  settings_t next=*current;
  snprintf(next.server,sizeof(next.server),"%s",candidate);snprintf(next.server_id,sizeof(next.server_id),"%s",server->id);
  snprintf(next.token,sizeof(next.token),"%s",token);next.connection_kind=server->relay[index]?2:server->local[index]?0:1;
  if(settings_server_url(next.server) || !settings_connection_allowed(&next))continue;
  *connected=next;return 1;
 }return 0;
}
