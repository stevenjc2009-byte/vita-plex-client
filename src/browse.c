#include "browse.h"
#include "plex_auth.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static unsigned utf8(unsigned cp, char out[4]) {
  if (cp < 128) { out[0] = (char)cp; return 1; }
  if (cp < 2048) { out[0] = 0xC0 | (cp >> 6); out[1] = 0x80 | (cp & 63); return 2; }
  if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';
  if (cp < 65536) { out[0] = 0xE0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 63); out[2] = 0x80 | (cp & 63); return 3; }
  if (cp > 0x10FFFF) cp = '?';
  out[0] = 0xF0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 63);
  out[2] = 0x80 | ((cp >> 6) & 63); out[3] = 0x80 | (cp & 63); return 4;
}

int plex_xml_attr(const char *s, const char *end, const char *name, char *out, unsigned cap) {
  if (!s || !end || !out || !cap) return -1;
  out[0] = 0;
  // Tokenize attributes and skip complete quoted values.
  while (s < end) {
    while (s < end && !isspace((unsigned char)*s)) s++;
    while (s < end && isspace((unsigned char)*s)) s++;
    const char *key = s;
    while (s < end && (isalnum((unsigned char)*s) || *s == '_' || *s == ':')) s++;
    size_t keylen = (size_t)(s - key);
    while (s < end && isspace((unsigned char)*s)) s++;
    if (s == end || *s != '=') continue;
    s++;
    while (s < end && isspace((unsigned char)*s)) s++;
    if (s == end || (*s != '"' && *s != '\'')) continue;
    char quote = *s++;
    const char *value = s;
    while (s < end && *s != quote) s++;
    if (s == end) return -1;
    const char *value_end = s++;
    if (keylen != strlen(name) || strncmp(key, name, keylen)) continue;
    unsigned n = 0;
    while (value < value_end) {
      char encoded[4]; unsigned count = 1;
      encoded[0] = *value++;
      if (encoded[0] == '&') {
        const char *semi = memchr(value, ';', (size_t)(value_end - value));
        if (semi && semi - value < 12) {
          unsigned cp = 0;
          if (semi - value == 3 && !strncmp(value, "amp", 3)) cp = '&';
          else if (semi - value == 2 && !strncmp(value, "lt", 2)) cp = '<';
          else if (semi - value == 2 && !strncmp(value, "gt", 2)) cp = '>';
          else if (semi - value == 4 && !strncmp(value, "quot", 4)) cp = '"';
          else if (semi - value == 4 && !strncmp(value, "apos", 4)) cp = '\'';
          else if (*value == '#') {
            char number[16]; size_t len = (size_t)(semi - value - 1);
            memcpy(number, value + 1, len); number[len] = 0;
            char *tail; int hex = number[0] == 'x' || number[0] == 'X';
            cp = (unsigned)strtoul(number + hex, &tail, hex ? 16 : 10);
            if (*tail || !cp || cp > 0x10FFFF) cp = '?';
          }
          if (cp) { count = utf8(cp, encoded); value = semi + 1; }
        }
      }
      if (n + count >= cap) { out[n] = 0; return 1; }
      memcpy(out + n, encoded, count); n += count;
    }
    out[n] = 0;
    return 0;
  }
  return -1;
}

static unsigned number(const char *s, const char *e, const char *name) {
  char b[32], *tail;
  if (plex_xml_attr(s, e, name, b, sizeof(b)) != 0 || !isdigit((unsigned char)b[0])) return 0;
  unsigned long v = strtoul(b, &tail, 10);
  return *tail || v > INT_MAX ? 0 : (unsigned)v;
}
static const char *tag_end(const char *p) {
  char quote=0;
  for(;*p;p++) {
    if(quote){if(*p==quote)quote=0;}
    else if(*p=='"' || *p=='\'')quote=*p;
    else if(*p=='>')return p;
  }
  return NULL;
}

int plex_parse_items(const char *xml, const char *tag, browse_item_t *out, int max) {
  if (!xml || !out || max < 1) return 0;
  int n = 0;
  const char *p = xml;
  while (n < max && (p = strchr(p, '<'))) {
    const char *start = ++p;
    while (isalnum((unsigned char)*p)) p++;
    size_t len = (size_t)(p - start);
    int dir = len == 9 && !strncmp(start, "Directory", 9);
    int video = len == 5 && !strncmp(start, "Video", 5);
    int other = (len == 5 && !strncmp(start, "Track", 5)) || (len == 5 && !strncmp(start, "Photo", 5));
    if (!isspace((unsigned char)*p) || (tag ? len != strlen(tag) || strncmp(start, tag, len) : !dir && !video && !other)) continue;
    const char *end = tag_end(p);
    if (!end) break;
    browse_item_t *it = out + n;
    memset(it, 0, sizeof(*it));
    if (plex_xml_attr(start, end, "title", it->title, sizeof(it->title)) < 0 ||
        plex_xml_attr(start, end, "key", it->key, sizeof(it->key)) != 0 || !it->key[0]) { p = end + 1; continue; }
    plex_xml_attr(start, end, "thumb", it->thumb, sizeof(it->thumb));
    if (!it->thumb[0]) plex_xml_attr(start, end, "parentThumb", it->thumb, sizeof(it->thumb));
    if (!it->thumb[0]) plex_xml_attr(start, end, "grandparentThumb", it->thumb, sizeof(it->thumb));
    plex_xml_attr(start, end, "type", it->type, sizeof(it->type));
    plex_xml_attr(start, end, "ratingKey", it->rating_key, sizeof(it->rating_key));
    plex_xml_attr(start, end, "grandparentTitle", it->parent_title, sizeof(it->parent_title));
    if (!it->parent_title[0]) plex_xml_attr(start, end, "parentTitle", it->parent_title, sizeof(it->parent_title));
    plex_xml_attr(start, end, "summary", it->summary, sizeof(it->summary));
    plex_xml_attr(start, end, "year", it->year, sizeof(it->year));
    plex_xml_attr(start, end, "contentRating", it->content_rating, sizeof(it->content_rating));
    it->duration = number(start, end, "duration");
    it->view_offset = number(start, end, "viewOffset");
    it->index = (int)number(start, end, "index");
    it->parent_index = (int)number(start, end, "parentIndex");
    it->is_directory = dir;
    if (!it->type[0]) snprintf(it->type, sizeof(it->type), "%s", video ? "movie" : dir ? "folder" : "other");
    n++; p = end + 1;
  }
  return n;
}

int plex_parse_page(const char *xml, browse_page_t *page) {
  const char *p = strstr(xml, "<MediaContainer"), *end;
  if (!p || !(end = tag_end(p))) return -1;
  page->offset = (int)number(p, end, "offset");
  page->size = (int)number(p, end, "size");
  char b[32];
  page->total = plex_xml_attr(p, end, "totalSize", b, sizeof(b)) == 0 ?
    (int)number(p, end, "totalSize") : page->offset + page->size;
  return 0;
}

int plex_parse_servers(const char *xml, plex_server_t *out, int max) {
  int n = 0;
  const char *p = xml;
  while (n < max && (p = strstr(p, "<Device "))) {
    const char *e = tag_end(p), *close = strstr(p, "</Device>");
    if (!e || !close) break;
    char provides[80];
    if (plex_xml_attr(p, e, "provides", provides, sizeof(provides)) == 0 && strstr(provides, "server")) {
      plex_server_t server = {0};
      plex_xml_attr(p, e, "name", server.name, sizeof(server.name));
      plex_xml_attr(p, e, "accessToken", server.token, sizeof(server.token));
      const char *c = e;
      int best = -1;
      while ((c = strstr(c, "<Connection ")) && c < close) {
        const char *ce = tag_end(c);
        char uri[256], local[8];
        if (!ce || ce > close) break;
        plex_xml_attr(c, ce, "local", local, sizeof(local));
        if (plex_xml_attr(c, ce, "uri", uri, sizeof(uri)) == 0) {
          int score = (!strcmp(local, "1") ? 4 : 0) + (!strncmp(uri, "http://", 7) ? 2 : 0);
          if (score > best) { best = score; snprintf(server.url, sizeof(server.url), "%s", uri); }
        }
        c = ce + 1;
      }
      if (server.url[0]) out[n++] = server;
    }
    p = close + 9;
  }
  return n;
}

int plex_build_page_url(const char *server, const char *token, const char *path,
  const char *search, const char *sort, int offset, int count, char *out, unsigned cap) {
  char tok[384], query[384], sorting[128];
  if (!path || path[0] != '/' || offset < 0 || count < 1 || strchr(path, '#')) return -1;
  plex_url_encode(token, tok, sizeof(tok)); plex_url_encode(search ? search : "", query, sizeof(query));
  plex_url_encode(sort ? sort : "", sorting, sizeof(sorting));
  int n = snprintf(out, cap, "%s%s%cX-Plex-Token=%s&X-Plex-Container-Start=%d&X-Plex-Container-Size=%d%s%s%s%s",
    server, path, strchr(path, '?') ? '&' : '?', tok, offset, count,
    query[0] ? "&title=" : "", query, sorting[0] ? "&sort=" : "", sorting);
  return n < 0 || (unsigned)n >= cap ? -1 : 0;
}
void plex_build_items_url(const char *server, const char *token, const char *section_key, char *out, unsigned cap) {
  char path[320]; snprintf(path, sizeof(path), "/library/sections/%s/all", section_key);
  plex_build_page_url(server, token, path, "", "titleSort:asc", 0, BROWSE_MAX_ITEMS, out, cap);
}
void plex_build_timeline_url(const char *server, const char *token, const char *rating_key, int state, char *out, unsigned cap) {
  char tok[384]; plex_url_encode(token, tok, sizeof(tok));
  snprintf(out, cap, "%s/:/timeline?ratingKey=%s&state=%s&X-Plex-Token=%s", server, rating_key,
    state == 1 ? "playing" : state == 2 ? "paused" : "stopped", tok);
}
