// Vita Plex Client — login + browse + play.
// X = confirm/select, O = back, up/down = move, START = quit.

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/ime_dialog.h>
#include <psp2/apputil.h>
#include <psp2/gxm.h>
#include <psp2/display.h>
#include <psp2/sysmodule.h>
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
#define UI_COLS 60 // 960px / 16px glyphs at the global 2x font

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

// Left-aligned list row with truncation: centered long titles looked
// overlapped/ragged in browse photos. Cap at UI_COLS - 6 chars.
static void ui_row(int selected, const char *title) {
  char t[64];
  snprintf(t, sizeof(t), "%s", title);
  if ((int)strlen(t) > UI_COLS - 6) {
    t[UI_COLS - 9] = 0;
    strncat(t, "...", sizeof(t) - strlen(t) - 1);
  }
  if (selected) DBG_PRINT(C_ORANGE_FG);
  DBG_PRINT("  %c %s\n", selected ? '>' : ' ', t);
  if (selected) DBG_PRINT(C_FG);
}

// File log for on-device diagnosis: ux0:data/plex-client/debug.log,
// truncated at every boot. User copies it off via VitaShell USB/FTP.
static SceUID logfd = -1;
static void log_msg(const char *fmt, ...) {
  if (logfd < 0) return;
  char tmp[512];
  unsigned t = sceKernelGetProcessTimeLow() / 1000; // ms since boot
  int n = snprintf(tmp, sizeof(tmp), "[%u.%03u] ", t / 1000, t % 1000);
  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(tmp + n, sizeof(tmp) - n - 2, fmt, ap);
  va_end(ap);
  tmp[n++] = '\n';
  sceIoWrite(logfd, tmp, n);
}

// System keyboard prompt for pasting/typing the 20-char Plex token.
// Used when plex.tv TLS is unreachable (old Vita SSL stack): the token
// comes from Plex Web on a PC, everything after runs over plain LAN HTTP.
// Built-in token keyboard. The system IME needs libgxm initialized, and
// GXM init on this device either fails (small param buffer: 0x805B0017),
// starves video memory for display buffers (0x80024309), or stalls the
// driver outright - every combination wedged or failed across v01.08-01.16.
// This needs only sceCtrl (proven working: X responds), so it cannot hang.
// Global 2x font: the stock 8x8 glyphs are unreadably small on the
// 960x544 panel (login/browse photos). Double each pixel into a static
// 8KB buffer (no heap: malloc failure silently kept 1x in v01.18) and
// install 16x16 dims. Idempotent: call after every psvDebugScreenInit21,
// including the player's restore, which resets the font to 1x.
static unsigned char ui_glyphs[16 * 16 * 256 / 8];
static PsvDebugScreenFont ui_fontstruct;
static void ui_font_2x(void) {
  PsvDebugScreenFont *src = psvDebugScreenGetFont();
  if (src->width == 16) return; // already installed
  if (src->width != 8) return; // unknown base font: leave it alone
  ui_fontstruct.width = 16;
  ui_fontstruct.height = 16;
  ui_fontstruct.first = src->first;
  ui_fontstruct.last = src->last;
  ui_fontstruct.size_w = 16;
  ui_fontstruct.size_h = 16;
  ui_fontstruct.glyphs = ui_glyphs;
  memset(ui_glyphs, 0, sizeof(ui_glyphs));
  for (int g = src->first; g <= src->last; g++)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        int sbit = (g - src->first) * 64 + y * 8 + x;
        int s = (src->glyphs[sbit / 8] >> (7 - (sbit % 8))) & 1;
        if (!s) continue;
        for (int dy = 0; dy < 2; dy++)
          for (int dx = 0; dx < 2; dx++) {
            int tbit = (g - src->first) * 256 + (2 * y + dy) * 16 + (2 * x + dx);
            ui_glyphs[tbit / 8] |= (unsigned char)(1 << (7 - (tbit % 8)));
          }
      }
  psvDebugScreenSetFont(&ui_fontstruct);
}

static int kb_prompt_token(char *out, unsigned out_len) {
  static const char *rows[] = {
    "abcdefghij", "klmnopqrst", "uvwxyzABCD", "EFGHIJKLMN",
    "OPQRSTUVWX", "YZ01234567", "89-_"
  };
  static const int NROWS = 7, NCOLS = 10;
  char tok[65];
  unsigned tlen = 0;
  int r = 0, c = 0;
  memset(tok, 0, sizeof(tok));
  SceCtrlData pad, old;
  memset(&old, 0, sizeof(old));
  log_msg("kb enter");
  ui_font_2x(); // keyboard shares the global 2x font: no heap, no fail
  log_msg("kb font %dx%d", psvDebugScreenGetFont()->width,
    psvDebugScreenGetFont()->height);
  for (;;) {
    DBG_CLEAR();
    DBG_PRINT("\n  Enter Plex token\n\n");
    DBG_PRINT("  PC: app.plex.tv, F12 Console,\n");
    DBG_PRINT("  localStorage.myPlexAccessToken\n\n");
    char cur[80];
    snprintf(cur, sizeof(cur), "[ %s%s ]", tok, tlen < 64 ? "_" : "");
    DBG_PRINT(C_ORANGE_FG "  %s\n" C_FG "\n", cur);
    for (int i = 0; i < NROWS; i++) {
      char line[128];
      int o = 0;
      for (int j = 0; j < NCOLS && rows[i][j]; j++) {
        if (i == r && j == c) o += snprintf(line + o, sizeof(line) - o,
          "<%c>", rows[i][j]);
        else o += snprintf(line + o, sizeof(line) - o,
          " %c ", rows[i][j]);
      }
      line[o] = 0;
      if (i == r) DBG_PRINT(C_ORANGE_FG);
      DBG_PRINT("  %s\n", line);
      if (i == r) DBG_PRINT(C_FG);
    }
    DBG_PRINT("\n  D-pad move   X pick   O delete\n");
    DBG_PRINT("  /\\ done   START cancel\n");
    // Wait for one button press, no auto-repeat (token entry is short).
    int pressed = 0;
    while (!pressed) {
      sceCtrlPeekBufferPositive(0, &pad, 1);
      pressed = pad.buttons & ~old.buttons;
      old = pad;
      sceKernelDelayThread(50000);
    }
    if (pressed & SCE_CTRL_START) {
      log_msg("kb cancel");
      return -1;
    }
    // Last row ("89-_") is short: clamp into it, and never emit its
    // padding cells (rows[r][c] would be NUL and truncate the token).
    int rowlen = (int)strlen(rows[r]);
    if (c >= rowlen) c = rowlen - 1;
    if (pressed & SCE_CTRL_UP) { r = (r + NROWS - 1) % NROWS; rowlen = (int)strlen(rows[r]); if (c >= rowlen) c = rowlen - 1; }
    if (pressed & SCE_CTRL_DOWN) { r = (r + 1) % NROWS; rowlen = (int)strlen(rows[r]); if (c >= rowlen) c = rowlen - 1; }
    if (pressed & SCE_CTRL_LEFT) c = (c + NCOLS - 1) % NCOLS;
    if (pressed & SCE_CTRL_RIGHT) c = (c + 1) % NCOLS;
    if ((pressed & SCE_CTRL_CIRCLE) && tlen > 0) tok[--tlen] = 0;
    if ((pressed & SCE_CTRL_CROSS) && rows[r][c] && tlen + 1 < sizeof(tok) &&
        tlen + 1 < out_len)
      { tok[tlen++] = rows[r][c]; tok[tlen] = 0; }
    if (pressed & SCE_CTRL_TRIANGLE) {
      if (!tlen) continue;
      snprintf(out, out_len, "%s", tok);
      log_msg("kb done len=%u", tlen);
      return 0;
    }
  }
}
#endif

int main(void) {
#ifdef __vita__
  DBG_INIT();
  sceIoMkdir("ux0:data/plex-client", 0777);
  logfd = sceIoOpen("ux0:data/plex-client/debug.log",
    SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  log_msg("boot v%s", APP_VERSION);
  sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
  // Required before any system dialog (keyboard included).
  sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
  sceCommonDialogSetConfigParam(&(SceCommonDialogConfigParam){});
  http_init();
  ui_theme();
  ui_font_2x();
  log_msg("ui font %dx%d", psvDebugScreenGetFont()->width,
    psvDebugScreenGetFont()->height);

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
  // Dirty-flag rendering: the loop polls input every frame but only
  // repaints when something changed. Repainting every frame with no
  // vsync tears visibly (flicker on boot and in every menu).
  int dirty = 1;

  SceCtrlData pad, old = { 0 };
  memset(&old, 0, sizeof(old));

  // Auto-update check, once per launch. Skipped silently when offline.
  // Paint first: the check blocks on the network, and with dirty-flag
  // rendering nothing else would appear until it returns (looks frozen).
  {
    char dl[512], tag[32];
    DBG_CLEAR();
    ui_bar("Starting");
    ui_blank();
    ui_center("Checking for updates...");
    int up = update_check(dl, sizeof(dl), tag, sizeof(tag));
    log_msg("update_check=%d tag=%s", up,
      up > 0 ? tag : "-");
    if (up > 0) {
      int choice = -1;
      // Draw once, then poll without repainting (see dirty flag below).
      DBG_CLEAR();
      ui_bar("Update available");
      ui_center("A newer build is ready:");
      char line[64];
      snprintf(line, sizeof(line), "%s  ->  %s", APP_VERSION, tag);
      ui_center(line);
      ui_blank();
      ui_center("[ X ] Download + install      [ O ] Skip");
      while (choice < 0) {
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
    if (pressed) log_msg("btn 0x%X scr %d pend %d", pressed, (int)s,
      (int)pending);
    if (pad.buttons & SCE_CTRL_START) break;

    // ---- input: arm actions, never block here ----
    if (pending == ACT_NONE) {
      if ((pressed & SCE_CTRL_UP) && cursor > 0) cursor--;
      if (pressed & SCE_CTRL_DOWN) cursor++;
      if (s == S_LOGIN) {
        if (!pin.pin_id) {
          if (pressed & SCE_CTRL_CROSS) pending = ACT_PIN_CREATE;
          if (pressed & SCE_CTRL_TRIANGLE) {
            char tok[128];
            if (kb_prompt_token(tok, sizeof(tok)) == 0) {
              snprintf(st.token, sizeof(st.token), "%s", tok);
              settings_save(&st);
              s = S_SECTIONS;
              need_fetch = 1;
              cursor = 0;
              snprintf(status, sizeof(status), "Token saved - loading libraries");
            } else {
              snprintf(status, sizeof(status), "Token entry cancelled");
            }
          }
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
          ui_font_2x(); // player's screen restore resets the font to 1x
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

    // Any button press may have changed state: schedule one repaint.
    if (pressed) dirty = 1;

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
          log_msg("pin create fail HTTP %d err 0x%X ssl 0x%X/0x%X",
            http_last_status(), http_last_error(),
            http_last_ssl_err(), http_last_ssl_detail());
          snprintf(status, sizeof(status),
            "plex.tv fail HTTP %d err 0x%X ssl 0x%X/0x%X - tell me all 4",
            http_last_status(), http_last_error(),
            http_last_ssl_err(), http_last_ssl_detail());
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
            "Not approved yet (HTTP %d err 0x%X) - code at plex.tv/link, X",
            http_last_status(), http_last_error());
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
      dirty = 1; // result changed status/screen: repaint once
      sceKernelDelayThread(33000);
      continue;
    }

    // ---- render (only when dirty) ----
    if (!dirty) {
      sceKernelDelayThread(33000);
      continue;
    }
    dirty = 0;
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
        ui_blank();
        ui_center("No code? PC: app.plex.tv, F12 Console, type");
        ui_center("localStorage.myPlexAccessToken, then:");
        ui_blank();
        ui_center("[ /\\ ]  Enter token manually");
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
      char ver[64];
      snprintf(ver, sizeof(ver), "v%s   START quits", APP_VERSION);
      ui_footer(ver);
    } else if (s == S_SECTIONS) {
      if (need_fetch) pending = ACT_FETCH_SEC;
      ui_bar("Libraries");
      ui_blank();
      if (cursor >= n_sec && n_sec > 0) cursor = n_sec - 1;
      for (int i = 0; i < n_sec && i < 20; i++)
        ui_row(i == cursor, sections[i].title);
      if (!n_sec) ui_center("(loading...)");
      ui_status(status);
      ui_footer("Up/Down move   X open   O logout   START quits");
    } else if (s == S_ITEMS) {
      if (need_fetch) pending = ACT_FETCH_ITEMS;
      ui_bar(sections[sec_idx].title);
      ui_blank();
      if (cursor >= n_items && n_items > 0) cursor = n_items - 1;
      for (int i = 0; i < n_items && i < 20; i++)
        ui_row(i == cursor, items[i].title);
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
