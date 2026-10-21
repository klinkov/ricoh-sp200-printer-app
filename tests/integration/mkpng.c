/* Write an 8-bit grayscale PNG: horizontal gradient black->white (dithered -> large JBIG) with a
 * black square in the top-left quarter.
 * usage: mkpng OUT.png WIDTH HEIGHT. MIT License. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len) {
  unsigned char hdr[8];
  be32(hdr, len);
  memcpy(hdr + 4, type, 4);
  fwrite(hdr, 1, 8, f);
  if (len)
    fwrite(data, 1, len, f);
  uLong crc = crc32(0, hdr + 4, 4);
  if (len)
    crc = crc32(crc, data, len);
  be32(hdr, (uint32_t)crc);
  fwrite(hdr, 1, 4, f);
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: mkpng OUT.png WIDTH HEIGHT\n");
    return 2;
  }
  uint32_t w = (uint32_t)atoi(argv[2]), h = (uint32_t)atoi(argv[3]);
  size_t rawlen = (size_t)(w + 1) * h;
  unsigned char *raw = malloc(rawlen);
  for (uint32_t y = 0; y < h; y++) {
    unsigned char *row = raw + (size_t)y * (w + 1);
    row[0] = 0; /* filter: none */
    for (uint32_t x = 0; x < w; x++)
      row[1 + x] = (x < w / 4 && y < h / 4) ? 0 : (unsigned char)(x * 255 / w);
  }
  uLongf zlen = compressBound((uLong)rawlen);
  unsigned char *z = malloc(zlen);
  if (compress(z, &zlen, raw, (uLong)rawlen) != Z_OK)
    return 1;
  FILE *f = fopen(argv[1], "wb");
  if (!f) {
    perror(argv[1]);
    return 1;
  }
  static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  unsigned char ihdr[13];
  be32(ihdr, w);
  be32(ihdr + 4, h);
  ihdr[8] = 8;  /* bit depth */
  ihdr[9] = 0;  /* grayscale */
  ihdr[10] = ihdr[11] = ihdr[12] = 0;
  fwrite(sig, 1, 8, f);
  chunk(f, "IHDR", ihdr, 13);
  chunk(f, "IDAT", z, (uint32_t)zlen);
  chunk(f, "IEND", NULL, 0);
  fclose(f);
  free(raw);
  free(z);
  return 0;
}
