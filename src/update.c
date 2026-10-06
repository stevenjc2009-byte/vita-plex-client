// Self-updater implementation. Release metadata comes from the GitHub
// API; the asset URL 302-redirects, so the download enables auto-redirect.

#ifdef __vita__

#include "update.h"
#include "http.h"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
#include <stdio.h>
#include <string.h>

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

int update_check(char *dl_url_out, unsigned url_len,
    char *tag_out, unsigned tag_len) {
  char url[256], tag[32];
  static char body[8192];
  snprintf(url, sizeof(url),
    "https://api.github.com/repos/%s/releases/latest", UPDATE_REPO);
  if (http_get(url, "PlexVita", "application/vnd.github+json",
        body, sizeof(body)) != 0)
    return -1;
  if (extract_str(body, "tag_name", tag, sizeof(tag)) != 0) return -1;
  if (extract_str(body, "browser_download_url",
        dl_url_out, url_len) != 0)
    return -1;
  const char *v = tag[0] == 'v' ? tag + 1 : tag;
  if (tag_out) snprintf(tag_out, tag_len, "%s", tag);
  return strcmp(v, APP_VERSION) != 0 ? 1 : 0;
}

int update_download(const char *dl_url,
    void (*progress_cb)(unsigned received, unsigned total)) {
  return http_download(dl_url, UPDATE_VPK_PATH, progress_cb);
}

int update_install(void) {
  int r = scePromoterUtilityInit();
  if (r < 0) return r;
  r = scePromoterUtilityPromotePkg(UPDATE_VPK_PATH, 1);
  scePromoterUtilityExit();
  if (r < 0) return r;
  sceKernelExitProcess(0);
  return 0; // not reached
}

#else

int update_check(char *u, unsigned ul, char *t, unsigned tl) {
  (void)u; (void)ul; (void)t; (void)tl;
  return 0;
}
int update_download(const char *u, void (*p)(unsigned, unsigned)) {
  (void)u; (void)p;
  return -1;
}
int update_install(void) { return -1; }

#endif
