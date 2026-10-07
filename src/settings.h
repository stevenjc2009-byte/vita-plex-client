#pragma once
// Persisted settings: ux0:data/plex-client/config.ini

#define SETTINGS_SERVER_LEN 256

typedef struct {
  char server[SETTINGS_SERVER_LEN]; // e.g. http://192.168.1.10:32400
  char token[128];
  char account_token[128];
  char client_id[40];
  char server_id[128];
  int remote_mode,connection_kind; // 0 automatic/local, 1 away/remote, kind 2 relay
  int bitrate, resume, sort;
  int performance, autoplay, subtitles;
} settings_t;

void settings_defaults(settings_t *s);
int settings_load(settings_t *s); // 0 ok, -1 missing (uses defaults)
int settings_save(const settings_t *s);
void settings_ensure_client_id(settings_t *s);
int settings_server_url(char *url);
