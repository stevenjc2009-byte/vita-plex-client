#pragma once
// Plex browse: parse <Directory title=".." key=".."> from sections XML,
// and <Video title=".." key=".."> from library items. Tiny scanner,
// no XML lib needed.

#define BROWSE_MAX_ITEMS 64
#define BROWSE_TITLE_LEN 64
#define BROWSE_KEY_LEN 128

typedef struct {
  char title[BROWSE_TITLE_LEN];
  char key[BROWSE_KEY_LEN];
  char thumb[BROWSE_KEY_LEN]; // may be empty: caller draws a tile
  int is_directory;
} browse_item_t;

// Returns count (0..max). Works for both Directory and Video tags.
int plex_parse_items(const char *xml, const char *tag,
  browse_item_t *out, int max);

// URL builders.
void plex_build_items_url(const char *server, const char *token,
  const char *section_key, char *out, unsigned out_len);
void plex_build_timeline_url(const char *server, const char *token,
  const char *rating_key, int state, char *out, unsigned out_len);
