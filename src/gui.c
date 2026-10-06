// Poster-grid browser: real pictures + highlight-box navigation.
// Own heap framebuffer presented with sceDisplaySetFrameBuf (same trick
// as player.c) - no GXM init, which wedges this device. Posters come
// from the LAN server over plain HTTP (token in query, no TLS needed).
// JPEGs decoded with stb_image (JPEG-only build to protect the 128KB
// code-segment budget). Missing/failed art falls back to a title tile.

#ifdef __vita__

#include "gui.h"
#include "http.h"
#include <stdint.h>
#include "debugScreen.h"

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Own append-log (main.c owns debug.log truncated at boot; we append).
static SceUID g_log = -1;
static void gui_log(const char *fmt, ...) {
  if (g_log < 0) {
    g_log = sceIoOpen("ux0:data/plex-client/debug.log",
      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (g_log < 0) return;
  }
  char tmp[256];
  int n = 0;
  va_list ap;
  va_start(ap, fmt);
  n = vsnprintf(tmp, sizeof(tmp) - 2, fmt, ap);
  va_end(ap);
  tmp[n++] = '\n';
  sceIoWrite(g_log, tmp, (SceSize)n);
}

#define FB_W 960
#define FB_H 544

#define C_BG    0xFF1A1A1Au
#define C_TILE  0xFF2D2D2Du
#define C_ORANGE 0xFF0DA0E5u // A8B8G8R8: E5A00D
#define C_WHITE 0xFFF5F5F5u
#define C_BLACK 0xFF000000u
#define C_GREY  0xFFAAAAAAu

// Grid geometry: 5 cols x 2 rows of 150x200 posters.
#define COLS 5
#define ROWS 2
#define PAGE (COLS * ROWS)
#define PW 150
#define PH 200
#define CELLW (FB_W / COLS)
#define TOP 64

static unsigned int *g_fb = NULL;

static void present(void) {
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  fb.base = g_fb;
  fb.pitch = FB_W;
  fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
  fb.width = FB_W;
  fb.height = FB_H;
  sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

static void rect(int x, int y, int w, int h, unsigned col) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > FB_W) w = FB_W - x;
  if (y + h > FB_H) h = FB_H - y;
  if (w <= 0 || h <= 0) return;
  for (int r = 0; r < h; r++) {
    unsigned *dst = g_fb + (y + r) * FB_W + x;
    for (int c = 0; c < w; c++) dst[c] = col;
  }
}

// Text via the installed debugScreen font (16px after ui_font_2x).
static void draw_char(int ch, int x, int y, unsigned col) {
  PsvDebugScreenFont *f = psvDebugScreenGetFont();
  if (!f || !f->glyphs) return;
  if (ch < f->first || ch > f->last) ch = '?';
  int gw = f->width, gh = f->height;
  for (int r = 0; r < gh; r++)
    for (int c = 0; c < gw; c++) {
      int bit = (ch - f->first) * gw * gh + r * gw + c;
      if ((f->glyphs[bit / 8] >> (7 - (bit % 8))) & 1) {
        int px = x + c, py = y + r;
        if (px >= 0 && px < FB_W && py >= 0 && py < FB_H)
          g_fb[py * FB_W + px] = col;
      }
    }
}

static void draw_text(const char *s, int x, int y, unsigned col) {
  PsvDebugScreenFont *f = psvDebugScreenGetFont();
  int gw = (f && f->width) ? f->width : 16;
  for (int i = 0; s[i]; i++)
    draw_char(s[i], x + i * gw, y, col);
}

static void draw_text_trunc(const char *s, int x, int y, int maxch,
    unsigned col) {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s", s);
  if ((int)strlen(tmp) > maxch) {
    tmp[maxch - 3] = 0;
    strncat(tmp, "...", sizeof(tmp) - strlen(tmp) - 1);
  }
  draw_text(tmp, x, y, col);
}

// Nearest-neighbor RGB888 -> framebuffer stretch blit.
static void blit_rgb(const unsigned char *src, int sw, int sh,
    int dx, int dy, int dw, int dh) {
  if (!src || sw <= 0 || sh <= 0) return;
  for (int y = 0; y < dh; y++) {
    int py = dy + y;
    if (py < 0 || py >= FB_H) continue;
    int sy = y * sh / dh;
    for (int x = 0; x < dw; x++) {
      int px = dx + x;
      if (px < 0 || px >= FB_W) continue;
      int sx = x * sw / dw;
      const unsigned char *p = src + (sy * sw + sx) * 3;
      g_fb[py * FB_W + px] =
        (unsigned)p[0] | ((unsigned)p[1] << 8) |
        ((unsigned)p[2] << 16) | 0xFF000000u;
    }
  }
}

// Thumb URL -> local cache path. Small size: keeps LAN fetch ~fast.
static void art_path(const char *thumb, char *out, unsigned out_len) {
  char safe[128];
  snprintf(safe, sizeof(safe), "%s", thumb);
  for (int i = 0; safe[i]; i++)
    if (safe[i] == '/') safe[i] = '_';
  snprintf(out, out_len, "ux0:data/plex-client/art/%s.jpg", safe);
}

static int file_exists(const char *path) {
  SceIoStat st;
  return sceIoGetstat(path, &st) == 0;
}

static void fetch_thumb(const char *server, const char *token,
    const char *thumb) {
  char path[256], url[512];
  art_path(thumb, path, sizeof(path));
  if (file_exists(path)) return;
  snprintf(url, sizeof(url), "%s%s?X-Plex-Token=%s&width=160&height=240",
    server, thumb, token);
  int rc = http_download(url, path, NULL);
  gui_log("art rc=%d http=%d err=0x%X %s", rc, http_last_status(),
    http_last_error(), thumb);
}

int gui_browse(const char *title, const browse_item_t *items, int n,
    const char *server, const char *token) {
  if (!g_fb) {
    g_fb = malloc(FB_W * FB_H * sizeof(*g_fb));
    if (!g_fb) return -1;
  }
  sceIoMkdir("ux0:data/plex-client/art", 0777);
  gui_log("gui enter n=%d title=%.40s", n, title ? title : "?");
  if (n > 0)
    gui_log("gui item0 title=%.40s thumb=%.80s", items[0].title,
      items[0].thumb);
  // Paint before any blocking thumb fetch: first entry would sit on
  // uninitialized pixels for seconds while 10 posters download.
  rect(0, 0, FB_W, FB_H, C_BG);
  rect(0, 0, FB_W, 48, C_ORANGE);
  draw_text_trunc(title, 16, 16, 40, C_BLACK);
  draw_text("Loading art...", 16, 120, C_WHITE);
  present();

  int cursor = 0, page = 0, dirty = 1, art_page = -1;
  SceCtrlData pad, old;
  memset(&old, 0, sizeof(old));

  for (;;) {
    sceCtrlPeekBufferPositive(0, &pad, 1);
    int pressed = pad.buttons & ~old.buttons;
    old = pad;
    if (pressed & SCE_CTRL_START) return -2;
    if (pressed & SCE_CTRL_CIRCLE) return -1;
    if (pressed & SCE_CTRL_LEFT) { cursor--; dirty = 1; }
    if (pressed & SCE_CTRL_RIGHT) { cursor++; dirty = 1; }
    if (pressed & SCE_CTRL_UP) { cursor -= COLS; dirty = 1; }
    if (pressed & SCE_CTRL_DOWN) { cursor += COLS; dirty = 1; }
    if (cursor < 0) cursor = 0;
    if (cursor >= n) cursor = n - 1;
    int newpage = cursor / PAGE;
    if (newpage != page) { page = newpage; dirty = 1; }
    if ((pressed & SCE_CTRL_CROSS) && n > 0) return cursor;

    // Fetch this page's posters once (blocking, small thumbs).
    if (art_page != page) {
      for (int i = page * PAGE; i < (page + 1) * PAGE && i < n; i++)
        if (items[i].thumb[0])
          fetch_thumb(server, token, items[i].thumb);
      art_page = page;
      dirty = 1;
    }

    if (dirty) {
      dirty = 0;
      int art_ok = 0, art_try = 0;
      rect(0, 0, FB_W, FB_H, C_BG);
      rect(0, 0, FB_W, 48, C_ORANGE);
      draw_text_trunc(title, 16, 16, 40, C_BLACK);
      char pg[32];
      snprintf(pg, sizeof(pg), "p%d", page + 1);
      draw_text(pg, FB_W - 80, 16, C_BLACK);
      if (n <= 0)
        draw_text("Empty library (n=0)", 16, 120, C_WHITE);
      for (int i = page * PAGE; i < (page + 1) * PAGE && i < n; i++) {
        int cell = i - page * PAGE;
        int cx = (cell % COLS) * CELLW;
        int cy = TOP + (cell / COLS) * 240;
        int px = cx + (CELLW - PW) / 2, py = cy;
        int sel = (i == cursor);
        // Poster or fallback tile.
        int drawn = 0;
        if (items[i].thumb[0]) {
          art_try++;
          char path[256];
          art_path(items[i].thumb, path, sizeof(path));
          SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
          if (fd >= 0) {
            int sz = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
            sceIoLseek(fd, 0, SCE_SEEK_SET);
            if (sz > 0 && sz < 2 * 1024 * 1024) {
              unsigned char *buf = malloc((unsigned)sz);
              if (buf && sceIoRead(fd, buf, (SceSize)sz) == sz) {
                int w = 0, h = 0;
                unsigned char *img = stbi_load_from_memory(
                  buf, sz, &w, &h, NULL, 3);
                if (img) {
                  blit_rgb(img, w, h, px, py, PW, PH);
                  stbi_image_free(img);
                  drawn = 1;
                  art_ok++;
                } else {
                  gui_log("art decode FAIL sz=%d %.60s", sz, path);
                }
              }
              free(buf);
            } else {
              gui_log("art bad size sz=%d %.60s", sz, path);
            }
            sceIoClose(fd);
          } else {
            gui_log("art missing %.60s", path);
          }
        }
        if (!drawn) {
          rect(px, py, PW, PH, C_TILE);
          draw_text_trunc(items[i].title, px + 8, py + 80, 9, C_GREY);
        }
        if (sel)
          { rect(px - 3, py - 3, PW + 6, 3, C_ORANGE);
            rect(px - 3, py + PH, PW + 6, 3, C_ORANGE);
            rect(px - 3, py, 3, PH, C_ORANGE);
            rect(px + PW, py, 3, PH, C_ORANGE); }
        draw_text_trunc(items[i].title, px - 8, py + PH + 6, 11, C_WHITE);
      }
      draw_text("X play   O back   START quits", 16, FB_H - 32, C_GREY);
      char ac[32];
      snprintf(ac, sizeof(ac), "art %d/%d", art_ok, art_try);
      draw_text(ac, FB_W - 160, FB_H - 32, C_GREY);
      gui_log("gui page=%d cursor=%d art %d/%d", page, cursor, art_ok,
        art_try);
      present();
    }
    sceDisplayWaitVblankStart();
  }
}

#else

#include "gui.h"
int gui_browse(const char *title, const browse_item_t *items, int n,
    const char *server, const char *token) {
  (void)title; (void)items; (void)n; (void)server; (void)token;
  return -1; // host stub
}

#endif
