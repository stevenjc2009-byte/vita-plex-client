#include "gui.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
int main(void) {
  assert(gui_init()==0);
  browse_item_t libraries[6]={0};
  const char *names[]={"Movies","TV Shows","Anime","Documentaries","Home Videos","Music"};
  for(int i=0;i<6;i++){snprintf(libraries[i].title,sizeof(libraries[i].title),"%s",names[i]);
    snprintf(libraries[i].type,sizeof(libraries[i].type),"%s",i==1 || i==2?"show":i==5?"artist":"movie");libraries[i].is_directory=1;}
  gui_view_t v={.title="Your libraries",.subtitle="Your Plex server  /  Connected",.items=libraries,.n=6,.total=6,.libraries=1};
  gui_browse_view(&v);assert(!gui_snapshot("build/libraries-preview.ppm"));
  browse_item_t movies[10]={0};
  const char *titles[]={"The Last Horizon","Midnight Signal","Into the Wild Sky","A Different Season","Echoes of Tomorrow",
    "Northbound","The Quiet City","Golden Hour","Between Two Worlds","After the Rain"};
  for(int i=0;i<10;i++) {
    snprintf(movies[i].title,sizeof(movies[i].title),"%s",titles[i]);snprintf(movies[i].type,sizeof(movies[i].type),"movie");
    snprintf(movies[i].thumb,sizeof(movies[i].thumb),"/sample/%d",i);movies[i].duration=7200000;
  }
  movies[0].view_offset=2400000;
  v=(gui_view_t){.title="Movies",.subtitle="Title A-Z  /  Your libraries",.items=movies,.n=10,.total=286,
    .server="http://preview:32400",.token="preview",.cursor=0};
  gui_browse_view(&v);assert(!gui_snapshot("build/movies-preview.ppm"));
  snprintf(movies[0].year,sizeof(movies[0].year),"2025");snprintf(movies[0].content_rating,sizeof(movies[0].content_rating),"PG-13");
  snprintf(movies[0].summary,sizeof(movies[0].summary),"A journey beyond the familiar becomes a search for a way home. Explore the details, resume where you left off, or play the movie from the beginning.");
  gui_details(movies,"http://preview:32400","preview","");assert(!gui_snapshot("build/details-preview.ppm"));
  const char *settings[]={"Server: http://192.168.0.32:32400","Find and select Plex server","Enter / replace Plex token",
    "Video quality: 2 Mbps (H.264 / AAC)","Resume playback: On","Sort: Title A-Z","Refresh libraries"};
  gui_choice("Settings","Connection, playback and library preferences",settings,7);
  assert(!gui_snapshot("build/settings-preview.ppm"));gui_shutdown();puts("Interface previews rendered");return 0;
}
