#include "plex.h"
#include <stdio.h>
#include <string.h>
static int line(char *out,unsigned size){if(!fgets(out,(int)size,stdin))return -1;out[strcspn(out,"\r\n")]=0;return 0;}
int main(void) {
  settings_t st={0};char key[256],session[80],url[4096];st.bitrate=2000;
  if(line(st.server,sizeof(st.server)) || line(st.token,sizeof(st.token)) || line(st.client_id,sizeof(st.client_id)) || line(key,sizeof(key)) || line(session,sizeof(session)))return 1;
  if(plex_build_playback_url(&st,key,session,0,url,sizeof(url)))return 2;
  puts(url);return 0;
}
