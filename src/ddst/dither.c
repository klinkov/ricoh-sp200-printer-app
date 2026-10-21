/* 8-bit gray -> 1bpp conversion. MIT License. */
#include "dither.h"

#include <stdlib.h>
#include <string.h>

struct ddst_dither {
  unsigned width;
  unsigned row;     /* row counter: even rows left->right, odd rows right->left */
  int     *cur;     /* error for current row, in 1/16 gray units, index x+1 */
  int     *next;    /* error for next row */
};

ddst_dither_t *ddst_dither_new(unsigned width) {
  if (width == 0) return NULL;
  ddst_dither_t *d = calloc(1, sizeof(*d));
  if (!d) return NULL;
  d->width = width;
  d->cur = calloc((size_t)width + 2, sizeof(int));
  d->next = calloc((size_t)width + 2, sizeof(int));
  if (!d->cur || !d->next) { ddst_dither_free(d); return NULL; }
  return d;
}

void ddst_dither_free(ddst_dither_t *d) {
  if (!d) return;
  free(d->cur);
  free(d->next);
  free(d);
}

void ddst_dither_row(ddst_dither_t *d, const uint8_t *gray8, uint8_t *out_1bpp) {
  if (!d || !gray8 || !out_1bpp) return;
  unsigned w = d->width;
  memset(out_1bpp, 0, ((size_t)w + 7) / 8);
  memset(d->next, 0, ((size_t)w + 2) * sizeof(int));
  int ltr = (d->row & 1) == 0;
  int step = ltr ? 1 : -1;
  for (unsigned i = 0; i < w; i++) {
    unsigned x = ltr ? i : w - 1 - i;
    int *e = d->cur + x + 1;          /* e[0] is this pixel; e[step] is ahead */
    int acc = e[0];
    int v = (int)gray8[x] + (acc >= 0 ? (acc + 8) / 16 : -((-acc + 8) / 16));
    int q;
    if (v < 128) {
      out_1bpp[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
      q = 0;
    } else {
      q = 255;
    }
    int err = v - q;
    /* Floyd-Steinberg weights, mirrored on right->left rows */
    e[step] += err * 7;
    int *n = d->next + x + 1;
    n[-step] += err * 3;
    n[0]     += err * 5;
    n[step]  += err * 1;
  }
  /* errors that fell off the edges are dropped */
  int *t = d->cur; d->cur = d->next; d->next = t;
  d->cur[0] = d->cur[w + 1] = 0;
  d->row++;
}

void ddst_threshold_row(const uint8_t *gray8, unsigned width, uint8_t *out_1bpp) {
  if (!gray8 || !out_1bpp) return;
  memset(out_1bpp, 0, ((size_t)width + 7) / 8);
  for (unsigned x = 0; x < width; x++)
    if (gray8[x] < 128) out_1bpp[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
}
