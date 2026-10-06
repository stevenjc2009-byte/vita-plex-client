#pragma once
// Persisted settings: ux0:data/plex-client/config.ini

#define SETTINGS_SERVER_LEN 256

typedef struct {
  char server[SETTINGS_SERVER_LEN]; // e.g. http://192.168.1.10:32400
  char token[128];
  char account_token[128];
  char client_id[40];
  int bitrate, resume, sort;
} settings_t;

void settings_defaults(settings_t *s);
int settings_load(settings_t *s); // 0 ok, -1 missing (uses defaults)
int settings_save(const settings_t *s);
void settings_ensure_client_id(settings_t *s);
int settings_server_url(char *url);
