// Self-updater implementation. Release metadata comes from the GitHub
// API; the asset URL 302-redirects, so the download enables auto-redirect.

#include "update.h"
#include "plex_auth.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int update_version_newer(const char *candidate, const char *current) {
  unsigned a, b, c, d;
  char tail;
  if (!candidate || !current) return 0;
  if (*candidate == 'v') candidate++;
  if (*current == 'v') current++;
  if (!isdigit((unsigned char)*candidate) || !isdigit((unsigned char)*current)) return 0;
  if (sscanf(candidate, "%u.%u%c", &a, &b, &tail) != 2 ||
      sscanf(current, "%u.%u%c", &c, &d, &tail) != 2) return 0;
  return a > c || (a == c && b > d);
}

#ifdef __vita__

#include "http.h"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>
#include <stdio.h>
#include <string.h>

int update_check(char *dl_url_out, unsigned url_len,
    char *tag_out, unsigned tag_len) {
  char url[256], tag[32];
  static char body[8192];
  snprintf(url, sizeof(url),
    "https://api.github.com/repos/%s/releases/latest", UPDATE_REPO);
  if (http_get(url, "PlexVita", "application/vnd.github+json",
        body, sizeof(body)) != 0)
    return -1;
  if (plex_json_string(body, "tag_name", tag, sizeof(tag)) != 0) return -1;
  if (tag_out) snprintf(tag_out, tag_len, "%s", tag);
  if (!update_version_newer(tag, APP_VERSION)) return 0;
  const char *p = body;
  while ((p = strstr(p, "\"browser_download_url\"")) != NULL) {
    if (plex_json_string(p, "browser_download_url", dl_url_out, url_len) == 0) {
      size_t n = strlen(dl_url_out);
      if (n > 4 && !strcmp(dl_url_out + n - 4, ".vpk")) return 1;
    }
    p++;
  }
  return -1;
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
