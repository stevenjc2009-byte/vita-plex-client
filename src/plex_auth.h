#pragma once

// Tiny Plex auth + browse helpers. No JSON lib: Vita RAM is tight,
// responses parsed by substring search.

#define PLEX_CLIENT_ID_LEN 36
#define PLEX_CODE_LEN 16
#define PLEX_TOKEN_LEN 128

typedef struct {
  int pin_id;
  char code[PLEX_CODE_LEN];
} plex_pin_t;

// Headers every plex.tv call needs. Product identifies us to Plex.
void plex_auth_headers(const char *client_id,
  char *out, unsigned out_len);

// POST https://plex.tv/api/v2/pins?strong=true -> returns pin id + code.
// Parse with plex_parse_pin_create(). GET poll URL with plex_pin_poll_url().
// When user approved, poll response contains "authToken":"...".
void plex_pin_create_url(char *out, unsigned out_len);
void plex_pin_poll_url(int pin_id, char *out, unsigned out_len);
int plex_parse_pin_create(const char *json, plex_pin_t *pin);
int plex_parse_auth_token(const char *json, char *token_out, unsigned len);
int plex_json_string(const char *json, const char *key,
  char *out, unsigned out_len);

// Minimal percent-encode for metadata key path param.
void plex_url_encode(const char *in, char *out, unsigned out_len);
