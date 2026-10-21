/* Validate a DDST stream captured by mock-sink.
 * usage: check-ddst FILE PAGES JOBS COPIES PAPERWIDTH ROWS BLACK_FROM WHITE_FROM
 *   - exactly PAGES x PAGESTATUS=START/END, JOBS x @PJL EOJ and 2*JOBS UEL, stream ends with UEL;
 *     PAGES "any" (a canceled job): START == END, at most that many images, last command @PJL EOJ;
 *   - every page has COPIES=COPIES;
 *   - every IMAGELEN equals the bytes up to the following "@PJL SET DOTCOUNT=";
 *   - every DOTCOUNT == (black pixels of the decoded JBIG) / 100;
 *   - every PAPERWIDTH == PAPERWIDTH and the decoded JBIG is PAPERWIDTH wide;
 *   - on raster rows [0, ROWS): columns [BLACK_FROM, WHITE_FROM) black, [WHITE_FROM, PAPERWIDTH) white;
 *     rows [ROWS, height) (bottom padding) all white.
 * MIT License. */
#include <jbig.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char UEL[] = "\x1b%-12345X";
static int failures;

#define FAIL(...) do { fprintf(stderr, "check-ddst: FAIL: " __VA_ARGS__); fputc('\n', stderr); failures++; } while (0)

static unsigned rows_in;

/* Returns the number of black pixels, or -1 if the image could not be decoded. */
static long long check_image(int page, const unsigned char *d, size_t len, unsigned want_w, unsigned black_from, unsigned white_from) {
  struct jbg_dec_state s;
  size_t cnt = 0;
  jbg_dec_init(&s);
  int rc = jbg_dec_in(&s, (unsigned char *)d, len, &cnt);
  if (rc != JBG_EOK) {
    FAIL("page %d: JBIG decode: %s", page, jbg_strerror(rc));
    jbg_dec_free(&s);
    return -1;
  }
  unsigned long w = jbg_dec_getwidth(&s), h = jbg_dec_getheight(&s);
  const unsigned char *img = jbg_dec_getimage(&s, 0);
  size_t bpl = (w + 7) / 8;
  if (w != want_w)
    FAIL("page %d: JBIG width %lu != %u", page, w, want_w);
  unsigned long bad_black = 0, bad_white = 0;
  long long dots = 0;
  for (size_t b = 0; b < bpl * h; b++)
    dots += __builtin_popcount(img[b]);
  for (unsigned long y = 0; y < h && w == want_w; y++) {
    const unsigned char *row = img + y * bpl;
    if (y >= rows_in) {
      for (size_t b = 0; b < bpl; b++)
        if (row[b])
          bad_white++;
      continue;
    }
    for (unsigned x = black_from; x < w; x++) {
      int black = (row[x / 8] >> (7 - x % 8)) & 1;
      if (x < white_from && !black)
        bad_black++;
      if (x >= white_from && black)
        bad_white++;
    }
  }
  printf("check-ddst: page %d: JBIG %lux%lu, %zu bytes, %lld black; cols %u..%u black: %lu misses; cols %u..%lu + rows %u.. white: %lu misses\n",
         page, w, h, len, dots, black_from, white_from - 1, bad_black, white_from, w - 1, rows_in, bad_white);
  if (bad_black)
    FAIL("page %d: %lu non-black pixels in the right black band (content clipped/shifted?)", page, bad_black);
  if (bad_white)
    FAIL("page %d: %lu black pixels in the padding area (padding must be white)", page, bad_white);
  jbg_dec_free(&s);
  return dots;
}

int main(int argc, char **argv) {
  if (argc != 9) {
    fprintf(stderr, "usage: check-ddst FILE PAGES JOBS COPIES PAPERWIDTH ROWS BLACK_FROM WHITE_FROM\n");
    return 2;
  }
  bool any_pages = !strcmp(argv[2], "any");
  int want_pages = atoi(argv[2]), want_jobs = atoi(argv[3]), want_copies = atoi(argv[4]);
  unsigned want_w = (unsigned)atoi(argv[5]), black_from = (unsigned)atoi(argv[7]), white_from = (unsigned)atoi(argv[8]);
  rows_in = (unsigned)atoi(argv[6]);
  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    perror(argv[1]);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *buf = malloc((size_t)size + 1);
  if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
    perror("read");
    return 1;
  }
  fclose(f);

  int starts = 0, ends = 0, eojs = 0, uels = 0, images = 0;
  long long last_dots = -2; /* black pixels of the last image, waiting for its DOTCOUNT */
  char last_cmd[256] = "";
  size_t i = 0, n = (size_t)size;
  while (i < n) {
    if (n - i >= 9 && !memcmp(buf + i, UEL, 9)) {
      uels++;
      i += 9;
      continue;
    }
    unsigned char *eol = NULL;
    for (size_t j = i; j + 1 < n; j++)
      if (buf[j] == '\r' && buf[j + 1] == '\n') {
        eol = buf + j;
        break;
      }
    if (!eol || n - i < 4 || memcmp(buf + i, "@PJL", 4)) {
      FAIL("unexpected bytes at offset %zu", i);
      break;
    }
    char line[256];
    size_t ll = (size_t)(eol - (buf + i));
    if (ll >= sizeof(line))
      ll = sizeof(line) - 1;
    memcpy(line, buf + i, ll);
    line[ll] = 0;
    i = (size_t)(eol - buf) + 2;
    if (strcmp(line, "@PJL"))
      snprintf(last_cmd, sizeof(last_cmd), "%s", line);

    if (!strcmp(line, "@PJL SET PAGESTATUS=START"))
      starts++;
    else if (!strcmp(line, "@PJL SET PAGESTATUS=END"))
      ends++;
    else if (!strcmp(line, "@PJL EOJ"))
      eojs++;
    else if (!strncmp(line, "@PJL SET DOTCOUNT=", 18)) {
      long long dc = strtoll(line + 18, NULL, 10);
      if (last_dots == -2)
        FAIL("%s without a preceding image", line);
      else if (last_dots >= 0 && dc != last_dots / 100)
        FAIL("page %d: %s, decoded image has %lld black pixels (want DOTCOUNT=%lld)", images, line, last_dots,
             last_dots / 100);
      last_dots = -2;
    }
    else if (!strncmp(line, "@PJL SET COPIES=", 16)) {
      if (atoi(line + 16) != want_copies)
        FAIL("%s (want %d)", line, want_copies);
    } else if (!strncmp(line, "@PJL SET PAPERWIDTH=", 20)) {
      if ((unsigned)atoi(line + 20) != want_w)
        FAIL("%s (want %u)", line, want_w);
    } else if (!strncmp(line, "@PJL SET IMAGELEN=", 18)) {
      size_t len = (size_t)strtoul(line + 18, NULL, 10);
      static const char dc[] = "@PJL SET DOTCOUNT=";
      if (len > n - i || n - i - len < sizeof(dc) - 1 || memcmp(buf + i + len, dc, sizeof(dc) - 1)) {
        FAIL("IMAGELEN=%zu at offset %zu is not followed by DOTCOUNT", len, i);
        break;
      }
      images++;
      last_dots = check_image(images, buf + i, len, want_w, black_from, white_from);
      i += len;
    }
  }
  printf("check-ddst: %ld bytes, %d x PAGESTATUS=START, %d x END, %d x EOJ, %d x UEL, %d images\n", size, starts,
         ends, eojs, uels, images);
  if (any_pages) {
    if (starts != ends || images > starts)
      FAIL("unbalanced pages: %d START, %d END, %d images", starts, ends, images);
    if (strcmp(last_cmd, "@PJL EOJ"))
      FAIL("last PJL command is \"%s\", want @PJL EOJ", last_cmd);
  } else if (starts != want_pages || ends != want_pages || images != want_pages)
    FAIL("want %d pages", want_pages);
  if (eojs != want_jobs)
    FAIL("want exactly %d x @PJL EOJ, got %d", want_jobs, eojs);
  if (uels != 2 * want_jobs)
    FAIL("want %d x UEL, got %d", 2 * want_jobs, uels);
  if (n < 9 || memcmp(buf + n - 9, UEL, 9))
    FAIL("stream does not end with UEL");
  free(buf);
  if (failures)
    return 1;
  printf("check-ddst: OK\n");
  return 0;
}
