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
int http_download_art(const char *url, const char *path, volatile int *cancel);
// Last HTTP status seen by run() (0 = none yet). For UI diagnostics.
int http_last_status(void);
// Last SceHttp error from run() (0 = none). For UI diagnostics.
int http_last_error(void);
// Detail from sceHttpsGetSslError (0 = none). For UI diagnostics.
int http_last_ssl_err(void);
unsigned http_last_ssl_detail(void);

void http_prepare(unsigned deadline_seconds);
void http_cancel(void);
void http_shutdown(void);

int http_put(const char *url,const char *client,char *body,unsigned cap);

const char *http_init_stage(void);

// Independent playback transport; ordinary metadata/progress cancellation does
// not abort it. Returns 0, or the actual HTTP/library failure; caps binary data.
int http_media_fetch(const char *url,void *data,unsigned cap,unsigned *used,volatile int *cancel);
void http_media_abort(void);
