#include "plex_auth.h"
#include <stdio.h>
#include <string.h>

void plex_auth_headers(const char *client_id, char *out, unsigned out_len) {
  snprintf(out, out_len,
    "X-Plex-Product: PlexVita\r\n"
    "X-Plex-Version: 1.0\r\n"
    "X-Plex-Platform: PlayStation Vita\r\n"
    "X-Plex-Device: PS Vita\r\n"
    "X-Plex-Client-Identifier: %s\r\n"
    "Accept: application/json",
    client_id);
}

void plex_pin_create_url(char *out, unsigned out_len) {
  snprintf(out, out_len, "https://plex.tv/api/v2/pins?strong=true");
}

void plex_pin_poll_url(int pin_id, char *out, unsigned out_len) {
  snprintf(out, out_len, "https://plex.tv/api/v2/pins/%d", pin_id);
}

static int extract_str(const char *json, const char *key,
    char *out, unsigned out_len) {
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = strstr(json, pat);
  if (!p) return -1;
  p = strchr(p + strlen(pat), ':');
  if (!p) return -1;
  p++;
  while (*p == ' ' || *p == '"') {
    if (*p == '"') { p++; break; }
    p++;
  }
  unsigned i = 0;
  while (*p && *p != '"' && i + 1 < out_len) out[i++] = *p++;
  out[i] = 0;
  return i ? 0 : -1;
}

static int extract_int(const char *json, const char *key, int *val) {
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *p = strstr(json, pat);
  if (!p) return -1;
  p = strchr(p + strlen(pat), ':');
  if (!p) return -1;
  return sscanf(p + 1, "%d", val) == 1 ? 0 : -1;
}

int plex_parse_pin_create(const char *json, plex_pin_t *pin) {
  if (extract_int(json, "id", &pin->pin_id) != 0) return -1;
  if (extract_str(json, "code", pin->code, sizeof(pin->code)) != 0) return -1;
  return 0;
}

int plex_parse_auth_token(const char *json, char *token_out, unsigned len) {
  return extract_str(json, "authToken", token_out, len);
}

void plex_url_encode(const char *in, char *out, unsigned out_len) {
  unsigned j = 0;
  for (unsigned i = 0; in[i] && j + 4 < out_len; i++) {
    char c = in[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '/') {
      out[j++] = c;
    } else {
      snprintf(out + j, out_len - j, "%%%02X", (unsigned char)c);
      j += 3;
    }
  }
  out[j] = 0;
}
