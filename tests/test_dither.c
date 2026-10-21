#include "test_util.h"
#include "../src/ddst/dither.h"

#include <stdint.h>

static unsigned count_black(const uint8_t *p, unsigned width) {
  unsigned n = 0;
  for (unsigned x = 0; x < width; x++) n += (p[x / 8] >> (7 - x % 8)) & 1;
  return n;
}

static void test_threshold_row(void) {
  const uint8_t gray[8] = { 0, 127, 128, 255, 0, 0, 0, 0 };
  uint8_t out[1] = { 0xAA };
  ddst_threshold_row(gray, 8, out);
  ASSERT_EQ_U(out[0], 0xCF); /* 1,1,0,0,1,1,1,1 */
}

static void test_threshold_padding_zero(void) {
  const uint8_t gray[3] = { 0, 0, 0 };
  uint8_t out[1] = { 0xFF };
  ddst_threshold_row(gray, 3, out);
  ASSERT_EQ_U(out[0], 0xE0);
}

static void test_dither_flat_50_percent(void) {
  enum { W = 64, H = 64 };
  uint8_t gray[W], out[W / 8];
  memset(gray, 128, sizeof(gray));
  ddst_dither_t *d = ddst_dither_new(W);
  ASSERT(d != NULL);
  unsigned black = 0;
  for (int y = 0; y < H; y++) {
    ddst_dither_row(d, gray, out);
    black += count_black(out, W);
  }
  ddst_dither_free(d);
  ASSERT(black * 100 >= 45u * W * H && black * 100 <= 55u * W * H);
}

static void test_dither_white_black(void) {
  enum { W = 37 };
  uint8_t gray[W], out[(W + 7) / 8];
  ddst_dither_t *d = ddst_dither_new(W);
  ASSERT(d != NULL);
  memset(gray, 255, sizeof(gray));
  for (int y = 0; y < 5; y++) {
    memset(out, 0xAA, sizeof(out));
    ddst_dither_row(d, gray, out);
    for (unsigned i = 0; i < sizeof(out); i++) ASSERT_EQ_U(out[i], 0);
  }
  memset(gray, 0, sizeof(gray));
  for (int y = 0; y < 5; y++) {
    ddst_dither_row(d, gray, out);
    ASSERT_EQ_U(count_black(out, W), W);
    ASSERT_EQ_U(out[sizeof(out) - 1] & 0x07, 0); /* padding bits white */
  }
  ddst_dither_free(d);
}

static void test_dither_gradient_monotonic(void) {
  /* darker input -> more black pixels */
  enum { W = 128, H = 32 };
  uint8_t gray[W], out[W / 8];
  unsigned prev = W * H + 1;
  for (int level = 0; level <= 255; level += 51) {
    memset(gray, level, sizeof(gray));
    ddst_dither_t *d = ddst_dither_new(W);
    ASSERT(d != NULL);
    unsigned black = 0;
    for (int y = 0; y < H; y++) { ddst_dither_row(d, gray, out); black += count_black(out, W); }
    ddst_dither_free(d);
    ASSERT(black < prev);
    prev = black;
  }
}

int main(void) {
  RUN(test_threshold_row);
  RUN(test_threshold_padding_zero);
  RUN(test_dither_flat_50_percent);
  RUN(test_dither_white_black);
  RUN(test_dither_gradient_monotonic);
  TEST_EXIT();
}
