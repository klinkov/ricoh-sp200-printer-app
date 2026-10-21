/* 8-bit gray -> 1bpp conversion. MIT License. */
#ifndef DDST_DITHER_H
#define DDST_DITHER_H

#include <stdint.h>

typedef struct ddst_dither ddst_dither_t;

/* Floyd-Steinberg error diffusion, serpentine scan, threshold 128. NULL on OOM / width 0. */
ddst_dither_t *ddst_dither_new(unsigned width);
/* gray8: width bytes, 0 = black, 255 = white. out_1bpp: (width+7)/8 bytes, MSB first,
 * 1 = black; padding bits are 0. */
void ddst_dither_row(ddst_dither_t *d, const uint8_t *gray8, uint8_t *out_1bpp);
void ddst_dither_free(ddst_dither_t *d);

/* Plain threshold: gray < 128 -> black. For text/line art. */
void ddst_threshold_row(const uint8_t *gray8, unsigned width, uint8_t *out_1bpp);

#endif
