#pragma once
// SceHttp wrapper (Vita). Host builds get stubs.

int http_init(void);
int http_post_pins(const char *url, const char *client_id,
  char *body, unsigned body_len);
int http_get(const char *url, const char *client_id, const char *accept,
  char *body, unsigned body_len);
// Stream a URL straight to a file (follows redirects).
// progress_cb(received, total_or_0) may be NULL. 0 = ok.
int http_download(const char *url, const char *path,
  void (*progress_cb)(unsigned received, unsigned total));
