/* Deterministic P4 test-image generator for golden JBIG tests.
 * Usage: gen-pbm text|photo|odd-width > out.pbm */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng = 0x12345678u;
static uint32_t xorshift(void) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return rng;
}

static void setpx(uint8_t *row, unsigned x) { row[x >> 3] |= (uint8_t)(0x80 >> (x & 7)); }

/* Text-like: lines of "glyphs" (small rectangles with holes), margins, paragraph gaps. */
static int px_text(unsigned x, unsigned y, unsigned w) {
  const unsigned margin = 120, line_h = 60, glyph_w = 28, glyph_h = 36;
  if (x < margin || x >= w - margin) return 0;
  unsigned line = y / line_h, ly = y % line_h;
  if (line % 12 == 11) return 0;                 /* paragraph gap */
  if (ly >= glyph_h) return 0;
  unsigned gx = (x - margin) / glyph_w, lx = (x - margin) % glyph_w;
  uint32_t h = (gx * 2654435761u) ^ (line * 40503u);
  h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  if ((h & 7) == 0) return 0;                    /* word space */
  if (lx >= 22) return 0;                        /* inter-glyph gap */
  unsigned shape = (h >> 4) & 3;
  int stroke = lx < 4 || ly < 4 || (shape & 1 ? lx >= 18 : ly >= 32) || (shape == 2 && ly >= 16 && ly < 20);
  return stroke;
}

int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: gen-pbm text|photo|odd-width\n"); return 2; }
  unsigned w, h;
  int kind;
  if (!strcmp(argv[1], "text"))           { w = 1200; h = 4500; kind = 0; }
  else if (!strcmp(argv[1], "photo"))     { w = 640;  h = 480;  kind = 1; }
  else if (!strcmp(argv[1], "odd-width")) { w = 4961; h = 400;  kind = 2; }
  else { fprintf(stderr, "unknown kind\n"); return 2; }

  size_t bpl = (w + 7) / 8;
  uint8_t *row = malloc(bpl);
  if (!row) return 1;
  printf("P4\n# ricoh-sp200 golden %s\n%u %u\n", argv[1], w, h);
  for (unsigned y = 0; y < h; y++) {
    memset(row, 0, bpl);
    for (unsigned x = 0; x < w; x++) {
      int on = 0;
      if (kind == 0) on = px_text(x, y, w);
      else if (kind == 1) {
        /* gradient threshold against pseudo-random noise: density varies left->right */
        uint32_t r = xorshift() & 0xFF;
        unsigned level = (x * 255u) / (w - 1);
        unsigned ring = ((x - w / 2) * (x - w / 2) + (y - h / 2) * (y - h / 2)) / 900;
        on = r < ((level + (ring & 1) * 64) & 0xFF);
      } else {
        /* sparse grid + border touching the last (odd) column */
        on = (x % 97 == 0) || (y % 50 == 0) || x == w - 1 || (y > 100 && y < 140 && x > w - 300);
      }
      if (on) setpx(row, x);
    }
    fwrite(row, 1, bpl, stdout);
  }
  free(row);
  return 0;
}
