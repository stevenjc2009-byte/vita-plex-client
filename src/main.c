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
// NOTE: "\033[2J" only wipes pixels — the cursor stays where it was,
// so every frame printed lower and lower ("waterfall" bug). "\033[H"
// homes the cursor, which is what makes clear-then-redraw work.
#define DBG_CLEAR() psvDebugScreenPuts("\033[2J\033[H")
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

#ifdef __vita__
// Plex brand colors: near-black background, #E5A00D orange accent.
#define C_BG "\033[48;2;26;26;26m"
#define C_FG "\033[38;2;245;245;245m"
#define C_ORANGE_BG "\033[48;2;229;160;13m"
#define C_ORANGE_FG "\033[38;2;229;160;13m"
#define C_BLACK_FG "\033[38;2;0;0;0m"
#define UI_COLS 100

static void ui_theme(void) { DBG_PRINT(C_BG C_FG); }

// Full-width orange bar, then back to the dark theme.
static void ui_bar(const char *label) {
  char line[128];
  snprintf(line, sizeof(line), "  PLEX for Vita v%s  |  %s",
    APP_VERSION, label);
  int pad = UI_COLS - (int)strlen(line);
  if (pad < 0) pad = 0;
  DBG_PRINT(C_ORANGE_BG C_BLACK_FG "%s%*s" C_BG C_FG "\n\n", line, pad, "");
}

static void ui_center(const char *s) {
  int pad = (UI_COLS - (int)strlen(s)) / 2;
  if (pad < 1) pad = 1;
  DBG_PRINT("%*s%s\n", pad, "", s);
}

static void ui_blank(void) { DBG_PRINT("\n"); }

// Persistent status line, shown every frame until replaced.
static void ui_status(const char *s) {
  if (!s || !s[0]) return;
  DBG_PRINT("\n" C_ORANGE_FG "  %s" C_FG "\n", s);
}

static void ui_footer(const char *s) {
  DBG_PRINT("\n");
  ui_center(s);
}
#endif

int main(void) {
#ifdef __vita__
  DBG_INIT();
  sceIoMkdir("ux0:data/plex-client", 0777);
  http_init();
  ui_theme();

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
  // Network calls block, so they run as one-shot "pending" actions:
  // the button press only arms the action, the next frame paints a
  // "Working..." screen first, and only then does the blocking call
  // run. The user always sees feedback, never a frozen frame.
  typedef enum {
    ACT_NONE, ACT_PIN_CREATE, ACT_PIN_POLL,
    ACT_FETCH_SEC, ACT_FETCH_ITEMS
  } act_t;
  act_t pending = ACT_NONE;
  static char status[256] = { 0 };

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
        ui_bar("Update available");
        ui_center("A newer build is ready:");
        char line[64];
        snprintf(line, sizeof(line), "%s  ->  %s", APP_VERSION, tag);
        ui_center(line);
        ui_blank();
        ui_center("[ X ] Download + install      [ O ] Skip");
        sceCtrlPeekBufferPositive(0, &pad, 1);
        int pressed = pad.buttons & ~old.buttons;
        old = pad;
        if (pressed & SCE_CTRL_CROSS) choice = 1;
        if (pressed & SCE_CTRL_CIRCLE) choice = 0;
        sceKernelDelayThread(50000);
      }
      if (choice == 1) {
        DBG_CLEAR();
        ui_bar("Update available");
        ui_center("Downloading update...");
        if (update_download(dl, NULL) == 0) {
          DBG_CLEAR();
          ui_bar("Update available");
          ui_center("Installing - app will exit,");
          ui_center("relaunch when done.");
          sceKernelDelayThread(2000000);
          update_install(); // exits on success
          DBG_CLEAR();
          ui_bar("Update available");
          ui_center("Install failed. Continuing.");
          sceKernelDelayThread(2000000);
        } else {
          DBG_CLEAR();
          ui_bar("Update available");
          ui_center("Download failed. Continuing.");
          sceKernelDelayThread(2000000);
        }
      }
    }
  }

  for (;;) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    int pressed = pad.buttons & ~old.buttons;
    old = pad;
    if (pad.buttons & SCE_CTRL_START) break;

    // ---- input: arm actions, never block here ----
    if (pending == ACT_NONE) {
      if ((pressed & SCE_CTRL_UP) && cursor > 0) cursor--;
      if (pressed & SCE_CTRL_DOWN) cursor++;
      if (s == S_LOGIN) {
        if (!pin.pin_id) {
          if (pressed & SCE_CTRL_CROSS) pending = ACT_PIN_CREATE;
        } else {
          if (pressed & SCE_CTRL_CROSS) pending = ACT_PIN_POLL;
          if (pressed & SCE_CTRL_CIRCLE) {
            memset(&pin, 0, sizeof(pin));
            status[0] = 0;
          }
        }
      } else if (s == S_SECTIONS) {
        if ((pressed & SCE_CTRL_CROSS) && n_sec > 0) {
          if (cursor >= n_sec) cursor = n_sec - 1;
          sec_idx = cursor;
          s = S_ITEMS;
          need_fetch = 1;
          cursor = 0;
          status[0] = 0;
        }
        if (pressed & SCE_CTRL_CIRCLE) {
          st.token[0] = 0;
          settings_save(&st);
          s = S_LOGIN;
          memset(&pin, 0, sizeof(pin));
          status[0] = 0;
        }
      } else if (s == S_ITEMS) {
        if ((pressed & SCE_CTRL_CROSS) && n_items > 0) {
          if (cursor >= n_items) cursor = n_items - 1;
          plex_build_vita_transcode_url(st.server, st.token,
            items[cursor].key, hls, sizeof(hls));
          player_play_hls(hls);
          player_run_blocking();
          ui_theme(); // player used its own framebuffer
        }
        if (pressed & SCE_CTRL_CIRCLE) {
          s = S_SECTIONS;
          need_fetch = 1;
          status[0] = 0;
        }
      } else {
        if (pressed & SCE_CTRL_CIRCLE) {
          player_stop();
          s = S_ITEMS;
        }
      }
    }

    // ---- pending network action: paint "Working..." first ----
    if (pending != ACT_NONE) {
      DBG_CLEAR();
      ui_bar("Please wait");
      ui_blank();
      ui_center("Working...");
      if (pending == ACT_PIN_CREATE)
        ui_center("Contacting plex.tv for a link code");
      else if (pending == ACT_PIN_POLL)
        ui_center("Checking plex.tv/link approval");
      else
        ui_center("Talking to your Plex server");
      ui_blank();
      ui_center("This can take up to 10 seconds.");

      switch (pending) {
      case ACT_PIN_CREATE:
        plex_pin_create_url(url, sizeof(url));
        if (http_post_pins(url, st.client_id, body, sizeof(body)) == 0 &&
            plex_parse_pin_create(body, &pin) == 0) {
          settings_save(&st);
          snprintf(status, sizeof(status),
            "Code ready - enter it at plex.tv/link, then press X");
        } else {
          snprintf(status, sizeof(status),
            "Network error - check Vita Wi-Fi, then press X to retry");
        }
        break;
      case ACT_PIN_POLL:
        plex_pin_poll_url(pin.pin_id, url, sizeof(url));
        if (http_get(url, st.client_id, "application/json",
              body, sizeof(body)) == 0 &&
            plex_parse_auth_token(body, st.token, sizeof(st.token)) == 0) {
          settings_save(&st);
          s = S_SECTIONS;
          need_fetch = 1;
          cursor = 0;
          snprintf(status, sizeof(status), "Signed in!");
        } else {
          snprintf(status, sizeof(status),
            "Not approved yet - enter the code at plex.tv/link, then X");
        }
        break;
      case ACT_FETCH_SEC:
        plex_build_sections_url(st.server, st.token, url, sizeof(url));
        if (http_get(url, st.client_id, "text/xml", body, sizeof(body)) == 0) {
          n_sec = plex_parse_items(body, "Directory", sections, 64);
          if (!n_sec)
            snprintf(status, sizeof(status), "Signed in, but no libraries found");
          else
            status[0] = 0;
        } else {
          snprintf(status, sizeof(status),
            "Server unreachable (%.60s) - O to logout, START quits",
            st.server);
        }
        need_fetch = 0;
        cursor = 0;
        break;
      case ACT_FETCH_ITEMS:
        plex_build_items_url(st.server, st.token,
          sections[sec_idx].key, url, sizeof(url));
        if (http_get(url, st.client_id, "text/xml",
              body, sizeof(body)) == 0)
          n_items = plex_parse_items(body, "Video", items, 64);
        if (!n_items)
          n_items = plex_parse_items(body, "Directory", items, 64);
        if (!n_items)
          snprintf(status, sizeof(status), "This library is empty");
        else
          status[0] = 0;
        need_fetch = 0;
        cursor = 0;
        break;
      default:
        break;
      }
      pending = ACT_NONE;
      sceKernelDelayThread(33000);
      continue;
    }

    // ---- render ----
    DBG_CLEAR();
    if (s == S_LOGIN) {
      ui_bar("Sign in");
      ui_center("P L E X");
      ui_blank();
      if (!pin.pin_id) {
        ui_center("Link this Vita to your Plex account:");
        ui_blank();
        ui_center("1.  Press X to get a 4-letter link code");
        ui_center("2.  On another device, go to plex.tv/link");
        ui_center("3.  Enter the code, come back, press X");
        ui_blank();
        ui_center("[ X ]  Get link code");
      } else {
        ui_center("On another device, go to plex.tv/link");
        ui_center("and enter this code:");
        ui_blank();
        char code[64];
        snprintf(code, sizeof(code), "  %s  ", pin.code);
        DBG_PRINT(C_ORANGE_FG);
        ui_center(code);
        DBG_PRINT(C_FG);
        ui_blank();
        ui_center("[ X ]  I entered the code      [ O ]  New code");
      }
      ui_status(status);
      ui_footer("START quits");
    } else if (s == S_SECTIONS) {
      if (need_fetch) pending = ACT_FETCH_SEC;
      ui_bar("Libraries");
      ui_blank();
      if (cursor >= n_sec && n_sec > 0) cursor = n_sec - 1;
      for (int i = 0; i < n_sec && i < 20; i++) {
        char line[160];
        snprintf(line, sizeof(line), "%c  %s", i == cursor ? '>' : ' ',
          sections[i].title);
        if (i == cursor) DBG_PRINT(C_ORANGE_FG);
        ui_center(line);
        if (i == cursor) DBG_PRINT(C_FG);
      }
      if (!n_sec) ui_center("(loading...)");
      ui_status(status);
      ui_footer("Up/Down move   X open   O logout   START quits");
    } else if (s == S_ITEMS) {
      if (need_fetch) pending = ACT_FETCH_ITEMS;
      ui_bar(sections[sec_idx].title);
      ui_blank();
      if (cursor >= n_items && n_items > 0) cursor = n_items - 1;
      for (int i = 0; i < n_items && i < 20; i++) {
        char line[160];
        snprintf(line, sizeof(line), "%c  %s", i == cursor ? '>' : ' ',
          items[i].title);
        if (i == cursor) DBG_PRINT(C_ORANGE_FG);
        ui_center(line);
        if (i == cursor) DBG_PRINT(C_FG);
      }
      if (!n_items) ui_center("(loading...)");
      ui_status(status);
      ui_footer("Up/Down move   X play   O back   START quits");
    } else {
      ui_bar("Now playing");
      ui_blank();
      ui_center(hls);
      ui_status(status);
      ui_footer("O stop/back   START quits");
    }
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
