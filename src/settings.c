#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef __vita__
#include <psp2/kernel/rng.h>
#define CONFIG_PATH "ux0:data/plex-client/config.ini"
#else
#define CONFIG_PATH "config.ini"
#endif

void settings_defaults(settings_t *s) {
  memset(s, 0, sizeof(*s));
  snprintf(s->server, sizeof(s->server), "http://192.168.0.32:32400");
  s->bitrate = 2000;
  s->resume = 1;
}

void settings_ensure_client_id(settings_t *s) {
  if (s->client_id[0]) return;
  // Simple pseudo-UUID; uniqueness only matters per Plex account.
  static unsigned seed;
#ifdef __vita__
  if (sceKernelGetRandomNumber(&seed, sizeof(seed)) < 0)
#endif
    seed ^= (unsigned)time(NULL) ^ (unsigned)clock();
  seed = seed * 1664525u + 1013904223u;
  snprintf(s->client_id, sizeof(s->client_id),
    "vita-%08x-4b21-plex", seed);
}

int settings_load(settings_t *s) {
  settings_defaults(s);
  settings_ensure_client_id(s);
  FILE *f = fopen(CONFIG_PATH, "r");
  if (!f) f = fopen(CONFIG_PATH ".bak", "r");
  if (!f) return -1;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (sscanf(line, "server=%255[^\n]", s->server) == 1) continue;
    if (sscanf(line, "token=%127[^\n]", s->token) == 1) continue;
    if (sscanf(line, "account_token=%127[^\n]", s->account_token) == 1) continue;
    if (sscanf(line, "client_id=%39[^\n]", s->client_id) == 1) continue;
    if (sscanf(line, "bitrate=%d", &s->bitrate) == 1) continue;
    if (sscanf(line, "resume=%d", &s->resume) == 1) continue;
    if (sscanf(line, "sort=%d", &s->sort) == 1) continue;
  }
  fclose(f);
  settings_ensure_client_id(s);
  if (s->bitrate != 1000 && s->bitrate != 2000 && s->bitrate != 4000) s->bitrate=2000;
  s->resume=!!s->resume;
  if (s->sort<0 || s->sort>2) s->sort=0;
  if (!s->account_token[0]) snprintf(s->account_token,sizeof(s->account_token),"%s",s->token);
  return 0;
}

int settings_save(const settings_t *s) {
  FILE *f = fopen(CONFIG_PATH ".tmp", "w");
  if (!f) return -1;
  int r = fprintf(f, "server=%s\ntoken=%s\naccount_token=%s\nclient_id=%s\nbitrate=%d\nresume=%d\nsort=%d\n",
    s->server, s->token, s->account_token, s->client_id, s->bitrate, s->resume, s->sort);
  int closed = fclose(f);
  if (r < 0 || closed != 0) { remove(CONFIG_PATH ".tmp"); return -1; }
  // Vita rename replaces the destination; the host C runtime on Windows does
  // not. Keep a backup instead of deleting the only working settings file.
  remove(CONFIG_PATH ".bak");
  int had_old=rename(CONFIG_PATH,CONFIG_PATH ".bak")==0;
  if (rename(CONFIG_PATH ".tmp",CONFIG_PATH)!=0) {
    if(had_old)rename(CONFIG_PATH ".bak",CONFIG_PATH);
    return -1;
  }
  remove(CONFIG_PATH ".bak");return 0;
}

int settings_server_url(char *url) {
  size_t n=strlen(url);
  while(n && isspace((unsigned char)url[n-1]))url[--n]=0;
  while(n && url[n-1]=='/')url[--n]=0;
  const char *host=!strncmp(url,"http://",7)?url+7:!strncmp(url,"https://",8)?url+8:NULL;
  if(!host || !*host)return -1;
  for(const char *p=host;*p;p++)if(isspace((unsigned char)*p) || strchr("/?#@\\",*p))return -1;
  return 0;
}
