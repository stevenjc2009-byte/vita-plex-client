// Vita Plex Client — login + browse + play.
// X = confirm/select, O = back, up/down = move, START = quit.

#include <stdio.h>
#include <string.h>

#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include "debugScreen.h"
#define DBG_INIT() psvDebugScreenInit()
#define DBG_CLEAR() psvDebugScreenPuts("\033[2J")
#define DBG_PRINT(...) psvDebugScreenPrintf(__VA_ARGS__)
#else
#define DBG_INIT() ((void)0)
#define DBG_CLEAR() ((void)0)
#define DBG_PRINT(...) printf(__VA_ARGS__)
#endif

#include "plex.h"
#include "plex_auth.h"
#include "browse.h"
#include "settings.h"
#include "http.h"
#include "player.h"
#include "update.h"

#ifndef __vita__
static int host_demo(void);
#endif

typedef enum { S_LOGIN, S_SECTIONS, S_ITEMS, S_PLAY } screen_t;

int main(void) {
#ifdef __vita__
  DBG_INIT();
  sceIoMkdir("ux0:data/plex-client", 0777);
  http_init();

  settings_t st;
  settings_load(&st);
  char url[512], hls[1024];
  static char body[32768];
  plex_pin_t pin = { 0 };
  browse_item_t items[BROWSE_MAX_ITEMS];
  int n_items = 0, cursor = 0;
  screen_t s = st.token[0] ? S_SECTIONS : S_LOGIN;
  int need_fetch = 1;
  browse_item_t sections[BROWSE_MAX_ITEMS];
  int n_sec = 0, sec_idx = 0;

  SceCtrlData pad, old = { 0 };
  memset(&old, 0, sizeof(old));

  // Auto-update check, once per launch. Skipped silently when offline.
  {
    char dl[512], tag[32];
    int up = update_check(dl, sizeof(dl), tag, sizeof(tag));
    if (up > 0) {
      int choice = -1;
      while (choice < 0) {
        DBG_CLEAR();
        DBG_PRINT("Update %s found (this: %s)\n\nX = download+install\nO = skip\n",
          tag, APP_VERSION);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        int pressed = pad.buttons & ~old.buttons;
        old = pad;
        if (pressed & SCE_CTRL_CROSS) choice = 1;
        if (pressed & SCE_CTRL_CIRCLE) choice = 0;
        sceKernelDelayThread(50000);
      }
      if (choice == 1) {
        DBG_CLEAR();
        DBG_PRINT("Downloading update...\n");
        if (update_download(dl, NULL) == 0) {
          DBG_PRINT("Installing - app will exit,\nrelaunch when done.\n");
          sceKernelDelayThread(2000000);
          update_install(); // exits on success
          DBG_PRINT("Install failed (%s).\nContinuing.\n", UPDATE_VPK_PATH);
          sceKernelDelayThread(2000000);
        } else {
          DBG_PRINT("Download failed. Continuing.\n");
          sceKernelDelayThread(2000000);
        }
      }
    }
  }

  for (;;) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    int pressed = pad.buttons & ~old.buttons;
    old = pad;
    if ((pressed & SCE_CTRL_UP) && cursor > 0) cursor--;
    if (pressed & SCE_CTRL_DOWN) cursor++;

    DBG_CLEAR();
    if (s == S_LOGIN) {
      DBG_PRINT("PLEX for Vita - login\n\n");
      if (!pin.pin_id) {
        DBG_PRINT("Press X to get link code\n");
        if (pressed & SCE_CTRL_CROSS) {
          plex_pin_create_url(url, sizeof(url));
          if (http_post_pins(url, st.client_id, body, sizeof(body)) == 0 &&
              plex_parse_pin_create(body, &pin) == 0)
            settings_save(&st);
          else
            DBG_PRINT("net error, retry\n");
        }
      } else {
        DBG_PRINT("1. Go to plex.tv/link\n2. Enter: %s\n\n", pin.code);
        DBG_PRINT("X = done  O = new code\n");
        if (pressed & SCE_CTRL_CROSS) {
          plex_pin_poll_url(pin.pin_id, url, sizeof(url));
          if (http_get(url, st.client_id, "application/json",
                body, sizeof(body)) == 0 &&
              plex_parse_auth_token(body, st.token, sizeof(st.token)) == 0) {
            settings_save(&st);
            s = S_SECTIONS;
            need_fetch = 1;
          } else {
            DBG_PRINT("not approved yet\n");
          }
        }
        if (pressed & SCE_CTRL_CIRCLE) memset(&pin, 0, sizeof(pin));
      }
    } else if (s == S_SECTIONS) {
      if (need_fetch) {
        plex_build_sections_url(st.server, st.token, url, sizeof(url));
        if (http_get(url, st.client_id, "text/xml", body, sizeof(body)) == 0)
          n_sec = plex_parse_items(body, "Directory", sections, 64);
        need_fetch = 0;
        cursor = 0;
      }
      DBG_PRINT("Libraries (O=logout):\n\n");
      for (int i = 0; i < n_sec && i < 20; i++)
        DBG_PRINT("%c %s\n", i == cursor ? '>' : ' ', sections[i].title);
      if ((pressed & SCE_CTRL_CROSS) && n_sec > 0) {
        sec_idx = cursor;
        s = S_ITEMS;
        need_fetch = 1;
        cursor = 0;
      }
      if (pressed & SCE_CTRL_CIRCLE) {
        st.token[0] = 0;
        settings_save(&st);
        s = S_LOGIN;
        memset(&pin, 0, sizeof(pin));
      }
    } else if (s == S_ITEMS) {
      if (need_fetch) {
        plex_build_items_url(st.server, st.token,
          sections[sec_idx].key, url, sizeof(url));
        if (http_get(url, st.client_id, "text/xml",
              body, sizeof(body)) == 0)
          n_items = plex_parse_items(body, "Video", items, 64);
        if (!n_items)
          n_items = plex_parse_items(body, "Directory", items, 64);
        need_fetch = 0;
        cursor = 0;
      }
      DBG_PRINT("%s (O=back):\n\n", sections[sec_idx].title);
      for (int i = 0; i < n_items && i < 20; i++)
        DBG_PRINT("%c %s\n", i == cursor ? '>' : ' ', items[i].title);
      if ((pressed & SCE_CTRL_CROSS) && n_items > 0) {
        plex_build_vita_transcode_url(st.server, st.token,
          items[cursor].key, hls, sizeof(hls));
        player_play_hls(hls);
        player_run_blocking();
        s = S_ITEMS;
      }
      if (pressed & SCE_CTRL_CIRCLE) { s = S_SECTIONS; need_fetch = 1; }
    } else {
      DBG_PRINT("Playing (active=%d):\n%s\n\nO = stop/back\n",
        player_active(), hls);
      if (pressed & SCE_CTRL_CIRCLE) {
        player_stop();
        s = S_ITEMS;
      }
    }
    if (pad.buttons & SCE_CTRL_START) break;
    sceKernelDelayThread(33000);
  }
  player_stop();
  sceKernelExitProcess(0);
  return 0;
#else
  return host_demo();
#endif
}

#ifndef __vita__
// Host self-test: auth + browse + Vita transcode URL.
#include <assert.h>
static int host_demo(void) {
  plex_pin_t pin;
  assert(plex_parse_pin_create(
    "{\"id\":123456,\"code\":\"ABCD-1234\"}", &pin) == 0);
  char tok[128];
  assert(plex_parse_auth_token(
    "{\"authToken\":\"tok123\"}", tok, sizeof(tok)) == 0);

  const char *xml =
    "<MediaContainer>"
    "<Directory title=\"Movies\" key=\"1\"/>"
    "<Directory title=\"TV\" key=\"2\"/>"
    "</MediaContainer>";
  browse_item_t it[8];
  int n = plex_parse_items(xml, "Directory", it, 8);
  assert(n == 2);

  const char *vxml = "<Video title=\"Ep1\" key=\"/library/metadata/99\"/>";
  n = plex_parse_items(vxml, "Video", it, 8);
  assert(n == 1);

  char url[1024];
  plex_build_vita_transcode_url(
    "http://192.168.1.10:32400", tok, it[0].key, url, sizeof(url));
  assert(strstr(url, "960x544") && strstr(url, "tok123"));

  char iu[512];
  plex_build_items_url("http://s:32400", tok, "1", iu, sizeof(iu));
  assert(strstr(iu, "/library/sections/1/all"));

  printf("host self-test OK: pin=%s items=%d\n", pin.code, n);
  return 0;
}
#endif
