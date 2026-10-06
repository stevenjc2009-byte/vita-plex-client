#include "settings.h"
#include <stdio.h>
#include <string.h>

#ifdef __vita__
#define CONFIG_PATH "ux0:data/plex-client/config.ini"
#else
#define CONFIG_PATH "config.ini"
#endif

void settings_defaults(settings_t *s) {
  memset(s, 0, sizeof(*s));
  snprintf(s->server, sizeof(s->server), "http://192.168.0.32:32400");
}

void settings_ensure_client_id(settings_t *s) {
  if (s->client_id[0]) return;
  // Simple pseudo-UUID; uniqueness only matters per Plex account.
  static unsigned seed = 0x12345678;
  seed = seed * 1664525u + 1013904223u;
  snprintf(s->client_id, sizeof(s->client_id),
    "vita-%08x-4b21-plex", seed);
}

int settings_load(settings_t *s) {
  settings_defaults(s);
  FILE *f = fopen(CONFIG_PATH, "r");
  if (!f) return -1;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    if (sscanf(line, "server=%127[^\n]", s->server) == 1) continue;
    if (sscanf(line, "token=%127[^\n]", s->token) == 1) continue;
    if (sscanf(line, "client_id=%39[^\n]", s->client_id) == 1) continue;
  }
  fclose(f);
  settings_ensure_client_id(s);
  // Migrate installs that still carry the placeholder IP from v01.01.
  if (!strcmp(s->server, "http://192.168.1.10:32400")) {
    snprintf(s->server, sizeof(s->server), "http://192.168.0.32:32400");
    settings_save(s);
  }
  return 0;
}

int settings_save(const settings_t *s) {
  FILE *f = fopen(CONFIG_PATH, "w");
  if (!f) return -1;
  fprintf(f, "server=%s\ntoken=%s\nclient_id=%s\n",
    s->server, s->token, s->client_id);
  fclose(f);
  return 0;
}
