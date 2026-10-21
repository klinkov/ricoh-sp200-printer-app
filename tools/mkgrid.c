/* mkgrid: P4 test grid on stdout. Frame 10 mm from edge, 10 mm grid (2 px lines),
 * centre cross, filled 10x10 mm square at top-left inside the frame. MIT License. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DPI 600
#define MM(x) ((unsigned)((x) * DPI / 25.4 + 0.5))

static unsigned W = 4961, H = 7016;
static uint8_t *img;
static unsigned stride;

static void px(unsigned x, unsigned y) {
  if (x < W && y < H) img[(size_t)y * stride + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}
static void hline(unsigned y, unsigned x0, unsigned x1) {
  for (unsigned t = 0; t < 2; t++) for (unsigned x = x0; x <= x1; x++) px(x, y + t);
}
static void vline(unsigned x, unsigned y0, unsigned y1) {
  for (unsigned t = 0; t < 2; t++) for (unsigned y = y0; y <= y1; y++) px(x + t, y);
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-w") && i + 1 < argc) W = (unsigned)strtoul(argv[++i], NULL, 10);
    else if (!strcmp(argv[i], "-h") && i + 1 < argc) H = (unsigned)strtoul(argv[++i], NULL, 10);
    else { fprintf(stderr, "usage: mkgrid [-w W -h H] > out.pbm\n"); return 2; }
  }
  unsigned m = MM(10), g = MM(10);
  if (W < 2 * m + 8 || H < 2 * m + 8) { fprintf(stderr, "mkgrid: geometry too small\n"); return 2; }
  stride = (W + 7) / 8;
  img = calloc((size_t)stride, H);
  if (!img) return 1;
  unsigned x0 = m, y0 = m, x1 = W - m - 2, y1 = H - m - 2;
  /* grid, 10 mm pitch from the frame's top-left corner */
  for (unsigned x = x0 + g; x < x1; x += g) vline(x, y0, y1);
  for (unsigned y = y0 + g; y < y1; y += g) hline(y, x0, x1);
  /* frame (4 px) */
  for (unsigned t = 0; t < 2; t++) {
    hline(y0 + t * 2, x0, x1 + 1); hline(y1 - t * 2, x0, x1 + 1);
    vline(x0 + t * 2, y0, y1 + 1); vline(x1 - t * 2, y0, y1 + 1);
  }
  /* centre cross, 20 mm arms */
  unsigned cx = W / 2, cy = H / 2, arm = MM(20);
  for (unsigned t = 0; t < 4; t++) {
    hline(cy - 2 + t, cx - arm, cx + arm); vline(cx - 2 + t, cy - arm, cy + arm);
  }
  /* orientation square: solid 10x10 mm, inside frame */
  for (unsigned y = y0 + 4; y < y0 + 4 + g; y++)
    for (unsigned x = x0 + 4; x < x0 + 4 + g; x++) px(x, y);

  printf("P4\n%u %u\n", W, H);
  fwrite(img, stride, H, stdout);
  free(img);
  return fflush(stdout) ? 1 : 0;
}
