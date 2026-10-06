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
#include <psp2/kernel/sysmem.h>
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

static SceUID g_fbid = -1;
static unsigned int *g_fb = NULL;
static int g_shared = 0; // 1: drawing into debugScreen's scanout buffer

// In-RAM decode cache for the current page: repainting used to re-open
// and re-decode up to 10 JPEGs from ux0 on EVERY cursor move, which is
// why scrolling looked like a full page refresh. Decode once per page
// entry; repaints blit from RAM. 10 x 150x200x3 = 900KB heap.
static unsigned char *pg_img[PAGE];
static int pg_cached = -1; // page number currently cached

static void page_free(void) {
  for (int k = 0; k < PAGE; k++) {
    free(pg_img[k]);
    pg_img[k] = NULL;
  }
  pg_cached = -1;
}

// Decode one cached JPEG into a fixed PW x PH x RGB888 buffer.
static void page_decode_cell(int cell, const char *path) {
  free(pg_img[cell]);
  pg_img[cell] = NULL;
  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (fd < 0) return;
  int sz = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
  sceIoLseek(fd, 0, SCE_SEEK_SET);
  if (sz <= 0 || sz >= 2 * 1024 * 1024) {
    gui_log("art bad size sz=%d %.60s", sz, path);
    sceIoClose(fd);
    return;
  }
  unsigned char *buf = malloc((unsigned)sz);
  if (buf && sceIoRead(fd, buf, (SceSize)sz) == sz) {
    int w = 0, h = 0;
    unsigned char *img = stbi_load_from_memory(buf, sz, &w, &h, NULL, 3);
    if (img && w > 0 && h > 0) {
      unsigned char *dst = malloc(PW * PH * 3);
      if (dst) {
        for (int y = 0; y < PH; y++) {
          int sy = y * h / PH;
          for (int x = 0; x < PW; x++) {
            int sx = x * w / PW;
            memcpy(dst + (y * PW + x) * 3,
              img + (sy * w + sx) * 3, 3);
          }
        }
        pg_img[cell] = dst;
      }
      stbi_image_free(img);
    } else {
      gui_log("art decode FAIL sz=%d %.60s", sz, path);
    }
  }
  free(buf);
  sceIoClose(fd);
}

// Boot-time framebuffer grab: CDRAM fragments as net/ssl/player blocks
// come and go, so claim our 2MB while the heap is pristine. Failure
// here (logged) beats a silent kick-back at library-entry time.
void gui_fb_early(void) {
  if (g_fb) return;
  g_fbid = sceKernelAllocMemBlock("plex_gui",
    SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
    FB_W * FB_H * sizeof(*g_fb), NULL);
  if (g_fbid >= 0) {
    if (sceKernelGetMemBlockBase(g_fbid, (void **)&g_fb) < 0) {
      gui_log("gui fb base FAIL block=0x%X", g_fbid);
      sceKernelFreeMemBlock(g_fbid);
      g_fbid = -1;
      g_fb = NULL;
    } else {
      gui_log("gui fb early block=0x%X base=%p", g_fbid, g_fb);
    }
  } else {
    gui_log("gui fb early ALLOC FAIL block=0x%X", g_fbid);
  }
  if (!g_fb) {
    // Last resort: draw into debugScreen's own scanout buffer (same
    // 960 pitch, already on display). Kills the alloc-fail kick-back.
    g_fb = (unsigned int *)psvDebugScreenGetBase();
    if (g_fb) {
      g_shared = 1;
      gui_log("gui fb SHARED base=%p", g_fb);
    }
  }
}

static void present(void) {
  if (g_shared) return; // buffer already on display: loop waits vblank
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

// URL-encode a thumb path for use as a query value.
static void url_encode(const char *in, char *out, unsigned out_len) {
  static const char *hex = "0123456789ABCDEF";
  unsigned o = 0;
  for (int i = 0; in[i] && o + 4 < out_len; i++) {
    char c = in[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      out[o++] = c;
    } else {
      out[o++] = '%';
      out[o++] = hex[(c >> 4) & 15];
      out[o++] = hex[c & 15];
    }
  }
  out[o] = 0;
}

static void fetch_thumb(const char *server, const char *token,
    const char *thumb) {
  char path[256], url[768], enc[384];
  art_path(thumb, path, sizeof(path));
  if (file_exists(path)) {
    // v01.23 cached full-size originals (5MB+): too heavy, refetch small.
    SceIoStat st;
    if (sceIoGetstat(path, &st) == 0 && (int)st.st_size > 1024 * 1024)
      sceIoRemove(path);
    else
      return;
  }
  // Ask the server to transcode small: raw thumbs can be 5MB+ JPEGs
  // (one measured 4858592 bytes), slow to fetch and heavy to decode.
  url_encode(thumb, enc, sizeof(enc));
  snprintf(url, sizeof(url),
    "%s/photo/:/transcode?width=160&height=240&minSize=1&url=%s&X-Plex-Token=%s",
    server, enc, token);
  int rc = http_download(url, path, NULL);
  gui_log("art rc=%d http=%d err=0x%X %s", rc, http_last_status(),
    http_last_error(), thumb);
}

int gui_browse(const char *title, const browse_item_t *items, int n,
    const char *server, const char *token, const char *notice) {
  if (!g_fb) {
    // Scanout needs CDRAM (physically contiguous): malloc'd heap
    // shows as a white screen (v01.24 photo). Same recipe debugScreen
    // itself uses. ~2MB fits; 16MB GXM blocks did NOT (01.08-16).
    // Lazy fallback if the boot-time grab never ran. gui_fb_early
    // never leaves g_fb NULL unless debugScreen itself is gone.
    gui_fb_early();
    gui_log("gui fb lazy block=0x%X base=%p", g_fbid, g_fb);
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
    if (pressed & SCE_CTRL_START) { page_free(); return -2; }
    if (pressed & SCE_CTRL_CIRCLE) { page_free(); return -1; }
    if (pressed & SCE_CTRL_LEFT) { cursor--; dirty = 1; }
    if (pressed & SCE_CTRL_RIGHT) { cursor++; dirty = 1; }
    if (pressed & SCE_CTRL_UP) { cursor -= COLS; dirty = 1; }
    if (pressed & SCE_CTRL_DOWN) { cursor += COLS; dirty = 1; }
    if (cursor < 0) cursor = 0;
    if (cursor >= n) cursor = n - 1;
    int newpage = cursor / PAGE;
    if (newpage != page) { page = newpage; dirty = 1; }
    if ((pressed & SCE_CTRL_CROSS) && n > 0) { page_free(); return cursor; }

    // Fetch this page's posters once (blocking, small thumbs), then
    // decode them into the RAM cache once. Repaints never touch
    // the network or the filesystem.
    if (art_page != page) {
      for (int i = page * PAGE; i < (page + 1) * PAGE && i < n; i++)
        if (items[i].thumb[0])
          fetch_thumb(server, token, items[i].thumb);
      page_free();
      int ok = 0;
      for (int i = page * PAGE; i < (page + 1) * PAGE && i < n; i++) {
        if (!items[i].thumb[0]) continue;
        char path[256];
        art_path(items[i].thumb, path, sizeof(path));
        page_decode_cell(i - page * PAGE, path);
        if (pg_img[i - page * PAGE]) ok++;
      }
      pg_cached = page;
      gui_log("gui page=%d decoded %d from cache dir", page, ok);
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
        // Poster (from the RAM decode cache) or fallback tile.
        int drawn = 0;
        if (pg_cached == page && pg_img[cell]) {
          art_try++;
          blit_rgb(pg_img[cell], PW, PH, px, py, PW, PH);
          drawn = 1;
          art_ok++;
        } else if (items[i].thumb[0]) {
          art_try++;
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
      if (notice && notice[0])
        draw_text_trunc(notice, 16, FB_H - 56, 52, C_ORANGE);
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
  const char *server, const char *token, const char *notice) {
  (void)title; (void)items; (void)n; (void)server; (void)token;
  (void)notice;
  return -1; // host stub
}

#endif
