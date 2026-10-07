#include "library.h"
#include "gui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int choices[4],choice_at,calls,fail,cancelled,scan;
static const char *listing;
int gui_choice(const char *title,const char *subtitle,const char **rows,int count){(void)title;(void)subtitle;int choice=choices[choice_at++];assert(choice<0 || choice<count);assert(rows[0]);return choice;}
void gui_message(const char *title,const char *message,const char *detail){(void)title;(void)message;(void)detail;}
int network_get(const char *url,const char *client,const char *accept,char *body,unsigned cap,unsigned deadline){(void)client;(void)accept;assert(deadline==15);calls++;assert(strstr(url,"X-Plex-Token=a%26b"));if(strstr(url,"/refresh?")){scan++;assert(strstr(url,"/93/refresh?") || strstr(url,"/all/refresh?"));assert(!strstr(url,"force="));}snprintf(body,cap,"%s",listing);return fail;}
int network_cancelled(void){return cancelled;}
int network_last_status(void){return fail?403:200;}int network_last_error(void){return 0;}
static void reset(void){choice_at=calls=fail=cancelled=scan=0;memset(choices,0,sizeof(choices));listing="<MediaContainer><Directory key='93' title='Movies' refreshing='0'/></MediaContainer>";}
int main(void){settings_t s={0};snprintf(s.server,sizeof(s.server),"http://test:32400");snprintf(s.token,sizeof(s.token),"a&b");char body[4096],notice[256];
 reset();assert(library_scan(&s,"93",body,sizeof(body),notice,sizeof(notice))==GUI_BACK);assert(scan==1 && calls==1 && strstr(notice,"Scan requested"));
 assert(library_scan_active());listing="<MediaContainer><Directory key='93' refreshing='1'/></MediaContainer>";assert(library_scan_poll(&s,body,sizeof(body),notice,sizeof(notice))==1 && library_scan_active());
 listing="<MediaContainer><Directory key='93' refreshing='0'/></MediaContainer>";assert(!library_scan_poll(&s,body,sizeof(body),notice,sizeof(notice)) && !library_scan_active() && strstr(notice,"finished"));
 reset();choices[0]=1;choices[1]=1;choices[2]=0;library_scan(&s,NULL,body,sizeof(body),notice,sizeof(notice));assert(scan==1 && calls==2);
 reset();choices[1]=1;library_scan(&s,NULL,body,sizeof(body),notice,sizeof(notice));assert(!calls);
 reset();choices[0]=GUI_QUIT;assert(library_scan(&s,"93",body,sizeof(body),notice,sizeof(notice))==GUI_QUIT && !calls);
 reset();library_scan(&s,"93/../all",body,sizeof(body),notice,sizeof(notice));assert(!calls && strstr(notice,"Invalid"));
 reset();choices[0]=2;library_scan(&s,"93",body,sizeof(body),notice,sizeof(notice));assert(calls==1 && !scan && strstr(notice,"no active"));
 reset();choices[0]=2;listing="<MediaContainer><Directory key='93' refreshing='1'/></MediaContainer>";library_scan(&s,"93",body,sizeof(body),notice,sizeof(notice));assert(strstr(notice,"active library scan"));
 reset();choices[0]=2;listing="<MediaContainer><Directory key='93'/></MediaContainer>";library_scan(&s,"93",body,sizeof(body),notice,sizeof(notice));assert(strstr(notice,"does not expose"));
 reset();fail=-1;library_scan(&s,"93",body,sizeof(body),notice,sizeof(notice));assert(strstr(notice,"403"));
 reset();fail=-1;cancelled=1;library_scan(&s,"93",body,sizeof(body),notice,sizeof(notice));assert(strstr(notice,"cancelled"));
 reset();s.remote_mode=1;assert(library_scan(&s,"93",body,sizeof(body),notice,sizeof(notice))==GUI_BACK && !calls && !choice_at && strstr(notice,"secure remote"));
 puts("Library scan selection, confirmation, scope, status, cancellation and denied access tests passed");return 0;}
