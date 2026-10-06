// Host test: prove ScaleFont2x produces an exact 2x doubling of the real
// 8x8 debugScreen font (dims 16x16 + every pixel quadrupled). Same algorithm
// as debugScreen.c; extracted verbatim so it compiles without Vita headers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PsvDebugScreenFont {
  unsigned char *glyphs, width, height, first, last, size_w, size_h;
} PsvDebugScreenFont;

#include "../src/debugScreenFont.c"

// ---- verbatim copy of psvDebugScreenScaleFont2x from src/debugScreen.c ----
PsvDebugScreenFont *test_ScaleFont2x(PsvDebugScreenFont *source_font) {
  PsvDebugScreenFont *target_font;
  size_t size;
  size_t align;
  int glyph;
  int row;
  int col;
  int count;
  unsigned char *source_bitmap;
  unsigned char source_mask;
  unsigned char *target_bitmap, *target_bitmap2;
  unsigned char target_mask, target_mask2;
  int target_next_row_bytes, target_next_row_bits;
  unsigned char pixel;

  if (!source_font) return NULL;

  target_font = (PsvDebugScreenFont *)malloc(sizeof(PsvDebugScreenFont));
  memset(target_font, 0, sizeof(PsvDebugScreenFont));
  target_font->width = 2 * source_font->width;
  target_font->height = 2 * source_font->height;
  target_font->first = source_font->first;
  target_font->last = source_font->last;
  target_font->size_w = 2 * source_font->size_w;
  target_font->size_h = 2 * source_font->size_h;

  size = target_font->width * target_font->height * (target_font->last - target_font->first + 1);
  if (size <= 0) {
    free(target_font);
    return NULL;
  }
  align = size % 8;
  size /= 8;
  if (align) size++;

  target_font->glyphs = (unsigned char *)malloc(size);
  memset(target_font->glyphs, 0, size);

  source_bitmap = source_font->glyphs;
  source_mask = 1 << 7;
  target_bitmap = target_font->glyphs;
  target_mask = 1 << 7;
  target_next_row_bytes = target_font->width / 8;
  target_next_row_bits = target_font->width % 8;
  for (glyph = source_font->first; glyph <= source_font->last; glyph++) {
    for (row = source_font->height; row > 0; row--) {
      target_bitmap2 = target_bitmap + target_next_row_bytes;
      target_mask2 = target_mask;
      for (col = target_next_row_bits; col > 0; col--, target_mask2 >>= 1) {
        if (!target_mask2) { target_bitmap2++; target_mask2 = 1 << 7; }
      }
      for (col = source_font->width; col > 0; col--, source_mask >>= 1) {
        if (!source_mask) { source_bitmap++; source_mask = 1 << 7; }
        pixel = *source_bitmap & source_mask;
        for (count = 2; count > 0; count--) {
          if (!target_mask) { target_bitmap++; target_mask = 1 << 7; }
          if (pixel) *target_bitmap |= target_mask;
          target_mask >>= 1;
          if (!target_mask2) { target_bitmap2++; target_mask2 = 1 << 7; }
          if (pixel) *target_bitmap2 |= target_mask2;
          target_mask2 >>= 1;
        }
      }
      target_bitmap = target_bitmap2;
      target_mask = target_mask2;
    }
  }

  return target_font;
}
// ---- end verbatim copy ----

static int src_px(PsvDebugScreenFont *f, int g, int x, int y) {
  int bit = (g - f->first) * f->width * f->height + y * f->width + x;
  return (f->glyphs[bit / 8] >> (7 - (bit % 8))) & 1;
}

int main(void) {
  printf("src: %dx%d adv %dx%d\n", psvDebugScreenFont.width,
    psvDebugScreenFont.height, psvDebugScreenFont.size_w,
    psvDebugScreenFont.size_h);
  PsvDebugScreenFont *t = test_ScaleFont2x(&psvDebugScreenFont);
  if (!t) { printf("RESULT: FAIL (NULL)\n"); return 1; }
  printf("dst: %dx%d adv %dx%d\n", t->width, t->height, t->size_w, t->size_h);
  if (t->width != 16 || t->height != 16 || t->size_w != 16 || t->size_h != 16) {
    printf("RESULT: FAIL (dims)\n");
    return 1;
  }
  int bad = 0;
  for (int g = 0; g < 256; g++)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        int s = src_px(&psvDebugScreenFont, g, x, y);
        if (src_px(t, g, 2 * x, 2 * y) != s ||
            src_px(t, g, 2 * x + 1, 2 * y) != s ||
            src_px(t, g, 2 * x, 2 * y + 1) != s ||
            src_px(t, g, 2 * x + 1, 2 * y + 1) != s)
          bad++;
      }
  printf("mismatched pixels: %d\n", bad);
  printf("RESULT: %s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
