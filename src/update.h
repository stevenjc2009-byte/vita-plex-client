#pragma once
// Self-updater: checks GitHub releases, downloads the VPK, installs it
// via the promoter utility. Vita-only; host builds see stubs.

// Must match VITA_VERSION in CMakeLists and the release tag (v + this).
#define APP_VERSION "01.18"
#define UPDATE_REPO "stevenjc2009-byte/vita-plex-client"
#define UPDATE_VPK_PATH "ux0:data/plex-client/update.vpk"

// 1 = newer release found (download URL copied out), 0 = current,
// -1 = check failed (offline etc; caller should just continue).
int update_check(char *dl_url_out, unsigned url_len,
  char *tag_out, unsigned tag_len);

// Downloads the VPK to UPDATE_VPK_PATH. Returns 0 on success.
// progress_cb(received_bytes, total_bytes_or_0) may be NULL.
int update_download(const char *dl_url,
  void (*progress_cb)(unsigned received, unsigned total));

// Installs UPDATE_VPK_PATH over this app. Does not return on success
// (exits the process so the user can relaunch the new build).
int update_install(void);
