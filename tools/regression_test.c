#include "plex_auth.h"
#include "plex.h"
#include "browse.h"
#include "settings.h"
#include "update.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_test_dir(path) _mkdir(path)
#define remove_test_dir(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_test_dir(path) mkdir(path,0700)
#define remove_test_dir(path) rmdir(path)
#endif

int main(void) {
  char token[128] = "previous", small[4], url[1024];
  assert(plex_parse_auth_token("{\"authToken\":null,\"id\":1}", token, sizeof(token)) < 0);
  assert(!token[0]);
  assert(plex_parse_auth_token("{\"authToken\": \n\t\"valid\"}", token, sizeof(token)) == 0);
  assert(!strcmp(token, "valid"));
  assert(plex_parse_auth_token("{\"authToken\":\"unterminated}", token, sizeof(token)) < 0);
  assert(plex_parse_auth_token("{\"authToken\":\"long-token\"}", small, sizeof(small)) < 0);
  assert(!small[0]);
  assert(plex_parse_auth_token("{}", small, 0) < 0);
  plex_pin_t pin = {123, "old"};
  assert(plex_parse_pin_create("{\"id\":987,\"code\":null}", &pin) < 0);
  assert(!pin.pin_id && !pin.code[0]);
  assert(plex_parse_pin_create("{\"id\":987,\"code\":\"ABCD\"}", &pin) == 0);
  assert(pin.pin_id == 987 && !strcmp(pin.code, "ABCD"));

  char escaped[128];
  assert(!plex_json_string("{\"body\":\"escaped \\\"tag_name\\\" text\",\"tag_name\":\"v01.33\"}","tag_name",escaped,sizeof(escaped)) && !strcmp(escaped,"v01.33"));
  assert(!plex_json_string("{\"value\":\"Caf\\u00e9 \\/ test\"}","value",escaped,sizeof(escaped)) && !strcmp(escaped,"Caf\xC3\xA9 / test"));
  assert(plex_parse_pin_create("{\"id\":9999999999999999,\"code\":\"ABCD\"}",&pin)<0);
  assert(plex_json_string("{\"value\":\"\\uD800\"}","value",escaped,sizeof(escaped))<0);

  plex_url_encode("abc", small, sizeof(small));
  assert(!strcmp(small, "abc"));
  plex_url_encode("&", small, sizeof(small));
  assert(!strcmp(small, "%26"));
  plex_url_encode("a/b?x=1&y=2", url, sizeof(url));
  assert(!strcmp(url, "a%2Fb%3Fx%3D1%26y%3D2"));
  plex_build_vita_transcode_url("http://server:32400", "a&b",
    "/library/metadata/99?x=1", url, sizeof(url));
  assert(strstr(url, "path=%2Flibrary%2Fmetadata%2F99%3Fx%3D1&"));
  assert(strstr(url, "X-Plex-Token=a%26b"));

  browse_item_t items[4];
  const char *xml = "<MediaContainer><Video ratingKey=\"wrong\" key=\"/right\" "
    "title=\"Tom &amp; Jerry\"/><Directory\n title=\"Season 1\" "
    "key=\"/library/metadata/10/children\"/><VideoExtra title=\"skip\" key=\"no\"/>"
    "</MediaContainer>";
  assert(plex_parse_items(xml, NULL, items, 4) == 2);
  assert(!strcmp(items[0].key, "/right") && !strcmp(items[0].title, "Tom & Jerry"));
  assert(!items[0].is_directory && items[1].is_directory);
  assert(plex_parse_items(xml, "Video", items, 4) == 1);
  assert(plex_parse_items(xml, "Directory", items, 4) == 1);
  assert(plex_parse_items(xml, NULL, items, 1) == 1);
  const char *meta="<MediaContainer offset='40' size='2' totalSize='113'><Video title = 'Caf&#233; &gt; night' key='/library/metadata/3' type='episode' duration='30000' viewOffset='12000' parentIndex='2' index='3' summary='A > B, title=wrong'/></MediaContainer>";
  assert(plex_parse_items(meta,NULL,items,4)==1);
  assert(!strcmp(items[0].title,"Caf\xC3\xA9 > night"));
  assert(!strcmp(items[0].summary,"A > B, title=wrong"));
  assert(items[0].duration==30000 && items[0].view_offset==12000 && items[0].index==3 && items[0].parent_index==2);
  browse_page_t page;
  assert(!plex_parse_page(meta,&page) && page.offset==40 && page.size==2 && page.total==113);
  assert(plex_parse_page("<html>Error</html>",&page)<0);
  assert(!plex_build_page_url("http://server:32400","a&b","/library/sections/2/all","hello & world","titleSort:asc",80,40,url,sizeof(url)));
  assert(strstr(url,"X-Plex-Container-Start=80&X-Plex-Container-Size=40"));
  assert(strstr(url,"&title=hello%20%26%20world&sort=titleSort%3Aasc"));
  assert(!plex_build_page_url("http://server:32400","a&b","/library/sections/93/refresh","","",0,1,url,sizeof(url)));
  assert(strstr(url,"/library/sections/93/refresh?") && strstr(url,"X-Plex-Token=a%26b") && !strstr(url,"force="));
  assert(plex_build_page_url("http://server:32400","token","/path",NULL,NULL,-1,40,url,sizeof(url))<0);
  plex_server_t servers[2];
  const char *resources="<MediaContainer><Device name='Home' provides='server' accessToken='server-token'><Connection local='0' uri='https://public:32400'/><Connection local='1' uri='http://192.168.0.32:32400'/></Device><Device name='Web' provides='client'></Device></MediaContainer>";
  assert(plex_parse_servers(resources,servers,2)==1);
  assert(!strcmp(servers[0].url,"http://192.168.0.32:32400") && !strcmp(servers[0].token,"server-token"));
  assert(plex_parse_items_offset(xml,NULL,items,4,1)==1 && !strcmp(items[0].title,"Season 1"));
  plex_stream_t tracks[4];char part[32];
  const char *track_xml="<MediaContainer><Video><Media><Part id='55'><Stream id='1' streamType='1'/><Stream id='2' streamType='2' displayTitle='English' selected='1'/><Stream id='3' streamType='3' displayTitle='French &amp; SDH'/></Part></Media></Video></MediaContainer>";
  assert(plex_parse_streams(track_xml,tracks,4,part,sizeof(part))==2 && !strcmp(part,"55"));
  assert(tracks[0].type==2 && tracks[0].selected==1 && !strcmp(tracks[1].label,"French & SDH"));
  char media[4096];
  assert(plex_hls_media_url("http://s:32400/path/start.m3u8?token=old","#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=100\nchild/index.m3u8\n","a&b",media,sizeof(media))==1);
  assert(!strcmp(media,"http://s:32400/path/child/index.m3u8?X-Plex-Token=a%26b"));
  assert(plex_hls_media_url("http://s:32400/path/start.m3u8","#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=100\nhttp://evil/steal\n","secret",media,sizeof(media))<0);
  assert(plex_hls_media_url("http://s/path/index.m3u8","#EXTM3U\n#EXTINF:4,\n0000.ts\n","token",media,sizeof(media))==0);
  assert(plex_hls_media_url("http://s/path","<html>error</html>","token",media,sizeof(media))<0);
  const char *remote_xml="<MediaContainer><Device name=\"Home\" provides=\"server\" clientIdentifier=\"machine-1\" accessToken=\"server-token\"><Connection uri=\"http://192.168.0.32:32400\" local=\"1\"/><Connection uri=\"https://home.plex.direct:32400\" local=\"1\"/><Connection uri=\"http://public:32400\" local=\"0\"/><Connection uri=\"https://public.plex.direct:32400\" local=\"0\"/><Connection uri=\"https://relay.plex.direct:443\" local=\"0\" relay=\"1\"/></Device></MediaContainer>";
  assert(plex_parse_servers(remote_xml,servers,2)==1 && !strcmp(servers[0].id,"machine-1") && servers[0].connection_count==5);
  assert(plex_server_identity("<?xml version='1.0'?><MediaContainer machineIdentifier='machine-1'/>","machine-1"));
  assert(!plex_server_identity("<MediaContainer machineIdentifier='other'/>","machine-1"));
  assert(!plex_server_identity("<html>login</html>","machine-1"));
  int order[8];assert(plex_connection_order(servers,0,order,8)==4 && order[0]==1 && order[1]==0 && order[2]==3 && order[3]==4);
  assert(plex_connection_order(servers,1,order,8)==2 && order[0]==3 && order[1]==4);
  assert(plex_connection_order(servers,1,order,1)==1 && order[0]==3);
  settings_t playback;settings_defaults(&playback);
  snprintf(playback.client_id,sizeof(playback.client_id),"test-vita");snprintf(playback.token,sizeof(playback.token),"secret&token");
  assert(!plex_build_playback_url(&playback,"/library/metadata/42","test-session",12000,media,sizeof(media)));
  assert(strstr(media,"offset=12&session=test-session") && strstr(media,"X-Plex-Client-Identifier=test-vita"));
  assert(strstr(media,"X-Plex-Client-Profile-Extra=") && strstr(media,"container%3Dmpegts"));
  assert(strstr(media,"X-Plex-Client-Profile-Name=Chrome"));

  assert(update_version_newer("v01.31", "01.30"));
  assert(update_version_newer("01.100", "01.99"));
  assert(!update_version_newer("01.29", "01.31"));
  assert(!update_version_newer("v01.31", "01.31"));
  assert(!update_version_newer("invalid", "01.31"));
  assert(!update_version_newer("01.32-preview", "01.31"));

  // Run this executable in an empty scratch directory: config.ini is test data.
  settings_t settings;
  remove("config.ini");
  assert(settings_load(&settings) < 0 && settings.client_id[0]);
  FILE *f = fopen("config.ini", "wb");
  assert(f);
  fputs("server=http://test:32400\r\ntoken=abc\r\nclient_id=test-vita\r\n", f);
  assert(!fclose(f));
  assert(!settings_load(&settings));
  assert(!strcmp(settings.server, "http://test:32400"));
  assert(!strcmp(settings.token, "abc"));
  assert(!strcmp(settings.client_id, "test-vita"));
  assert(!strcmp(settings.account_token,"abc"));
  assert(settings.bitrate==2000 && settings.resume==1 && settings.sort==0);
  char address[256]="http://192.168.0.32:32400/  ";assert(!settings_server_url(address));
  assert(!strcmp(address,"http://192.168.0.32:32400"));
  snprintf(address,sizeof(address),"http://user:secret@server:32400");assert(settings_server_url(address)<0);
  strcpy(settings.server_id,"machine-1");settings.remote_mode=1;settings.connection_kind=2;
  int saved=settings_save(&settings);
  if(saved)perror("settings_save");
  settings_t roundtrip;assert(!settings_load(&roundtrip) && roundtrip.remote_mode==1 && roundtrip.connection_kind==2 && !strcmp(roundtrip.server_id,"machine-1"));
  assert(!plex_build_playback_url(&roundtrip,"/library/metadata/42","session",0,url,sizeof(url)) && strstr(url,"maxVideoBitrate=1000") && strstr(url,"location=wan"));
  assert(!saved);
  assert(!make_test_dir("config.ini.tmp"));settings_t failed=roundtrip;strcpy(failed.server,"https://new.example:32400");
  assert(settings_save(&failed)<0);settings_t retained;assert(!settings_load(&retained) && !strcmp(retained.server,roundtrip.server) && !strcmp(retained.server_id,roundtrip.server_id));
  assert(!remove_test_dir("config.ini.tmp"));
  remove("config.ini");
  assert(!plex_build_music_url(&settings,"/library/metadata/42","session",12000,url,sizeof(url)));
  assert(strstr(url,"/music/:/transcode/universal/start.m3u8?") && strstr(url,"audioCodec=aac") && strstr(url,"offset=12"));
  assert(plex_build_music_url(&settings,"https://other/42","session",0,url,sizeof(url))<0);
  assert(!plex_build_photo_url(&settings,"/library/parts/42/file.jpg",url,sizeof(url)) && strstr(url,"format=jpeg"));
  assert(plex_build_photo_url(&settings,"//other/file.jpg",url,sizeof(url))<0);
  char photo_part[256];assert(!plex_first_part_key("<Photo><Media><Part key='/library/parts/42/file.jpg'/></Media></Photo>",photo_part,sizeof(photo_part)));
  assert(!strcmp(photo_part,"/library/parts/42/file.jpg"));assert(plex_first_part_key("<Part key='https://other/file.jpg'/>",photo_part,sizeof(photo_part))<0);
  puts("Plex regression tests passed");
  return 0;
}
