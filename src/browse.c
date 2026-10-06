#include "browse.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int get_attr(const char *tag, const char *attr,
    char *out, unsigned out_len) {
  char pat[32];
  snprintf(pat, sizeof(pat), "%s=\"", attr);
  const char *p = strstr(tag, pat);
  while (p && p != tag && !isspace((unsigned char)p[-1]))
    p = strstr(p + 1, pat);
  if (!p) return -1;
  p += strlen(pat);
  if (!out_len) return -1;
  unsigned i = 0;
  while (*p && *p != '"' && i + 1 < out_len) {
    char decoded = *p++;
    if (decoded == '&') {
      static const char *entities[] = {"amp;", "lt;", "gt;", "quot;", "apos;"};
      static const char values[] = "&<>\"'";
      for (unsigned e = 0; e < 5; e++) {
        size_t len = strlen(entities[e]);
        if (!strncmp(p, entities[e], len)) {
          decoded = values[e]; p += len; break;
        }
      }
    }
    out[i++] = decoded;
  }
  out[i] = 0;
  if (strcmp(attr, "title") && *p != '"') { out[0] = 0; return -1; }
  return i ? 0 : -1;
}

int plex_parse_items(const char *xml, const char *tag,
    browse_item_t *out, int max) {
  int n = 0;
  char open[32];
  if (!xml || !out || max <= 0) return 0;
  snprintf(open, sizeof(open), "<%s", tag ? tag : "");
  const char *p = xml;
  while (n < max) {
    p = strstr(p, open);
    if (!p) break;
    int directory = !strncmp(p, "<Directory", 10) &&
      isspace((unsigned char)p[10]);
    int video = !strncmp(p, "<Video", 6) && isspace((unsigned char)p[6]);
    if ((!tag && !directory && !video) ||
        (tag && !isspace((unsigned char)p[strlen(open)]))) { p++; continue; }
    const char *end = strchr(p, '>');
    if (!end) break;
    char chunk[2048]; // <Video> tags carry dozens of attrs
    size_t len = (size_t)(end - p);
    if (len >= sizeof(chunk)) len = sizeof(chunk) - 1;
    memcpy(chunk, p, len);
    chunk[len] = 0;
    if (get_attr(chunk, "title", out[n].title, sizeof(out[n].title)) == 0 &&
        get_attr(chunk, "key", out[n].key, sizeof(out[n].key)) == 0) {
      if (get_attr(chunk, "thumb", out[n].thumb, sizeof(out[n].thumb)) != 0)
        out[n].thumb[0] = 0;
      n++;
      out[n - 1].is_directory = directory;
    }
    p = end + 1;
  }
  return n;
}

void plex_build_items_url(const char *server, const char *token,
    const char *section_key, char *out, unsigned out_len) {
  snprintf(out, out_len, "%s/library/sections/%s/all?X-Plex-Token=%s",
    server, section_key, token);
}

void plex_build_timeline_url(const char *server, const char *token,
    const char *rating_key, int state, char *out, unsigned out_len) {
  snprintf(out, out_len,
    "%s/:/timeline?ratingKey=%s&state=%d&X-Plex-Token=%s",
    server, rating_key, state, token);
}
