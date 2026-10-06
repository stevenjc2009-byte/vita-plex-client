#include "plex_auth.h"
#include "plex.h"
#include "browse.h"
#include "settings.h"
#include "update.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
  assert(!settings_save(&settings));
  remove("config.ini");
  puts("Plex regression tests passed");
  return 0;
}
