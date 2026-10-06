#pragma once
#include <stdint.h>
#define BROWSE_MAX_ITEMS 40
#define BROWSE_TITLE_LEN 160
#define BROWSE_KEY_LEN 256
typedef struct {
  char title[BROWSE_TITLE_LEN], key[BROWSE_KEY_LEN], thumb[BROWSE_KEY_LEN];
  char type[24], rating_key[32], parent_title[160], summary[768];
  char year[12], content_rating[24];
  uint32_t duration, view_offset;
  int index, parent_index, is_directory;
} browse_item_t;
typedef struct { int offset, size, total; } browse_page_t;
typedef struct { char name[160], url[256], token[128]; } plex_server_t;
int plex_xml_attr(const char *start, const char *end, const char *name,
  char *out, unsigned size);
int plex_parse_items(const char *xml, const char *tag, browse_item_t *out, int max);
int plex_parse_page(const char *xml, browse_page_t *page);
int plex_parse_servers(const char *xml, plex_server_t *out, int max);
int plex_build_page_url(const char *server, const char *token, const char *path,
  const char *search, const char *sort, int offset, int count, char *out, unsigned size);
void plex_build_items_url(const char *server, const char *token,
  const char *section_key, char *out, unsigned size);
void plex_build_timeline_url(const char *server, const char *token,
  const char *rating_key, int state, char *out, unsigned size);
