#define _POSIX_C_SOURCE 200809L
#include "test_util.h"
#include "../src/ddst/ddst.h"
#include "../src/ddst/pbm.h"

#include <stdint.h>

#ifndef GOLDEN_DIR
#define GOLDEN_DIR "tests/golden"
#endif

static unsigned char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  size_t cap = 1 << 16, n = 0;
  unsigned char *buf = malloc(cap);
  size_t r;
  while (buf && (r = fread(buf + n, 1, cap - n, f)) > 0) {
    n += r;
    if (n == cap) { cap *= 2; unsigned char *nb = realloc(buf, cap); if (!nb) { free(buf); buf = NULL; } else buf = nb; }
  }
  fclose(f);
  *len = n;
  return buf;
}

static const unsigned char *memfind(const unsigned char *h, size_t hl, const char *needle) {
  size_t nl = strlen(needle);
  for (size_t i = 0; i + nl <= hl; i++)
    if (memcmp(h + i, needle, nl) == 0) return h + i;
  return NULL;
}

/* Encodes rows as one A4 page; extracts JBIG payload + DOTCOUNT. Returns false on error. */
static bool encode_page(unsigned w, unsigned h, const uint8_t *rows, tu_buf_t *out,
                        const unsigned char **jbig, size_t *jbig_len, unsigned long *dotcount) {
  ddst_job_info_t ji = { "g.pbm", "test", 1792586096, DDST_DOTCOUNT_RAW };
  ddst_page_info_t pi = { "A4", w, h, 600, 1, DDST_SOURCE_TRAY1 };
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, out);
  if (!job) return false;
  size_t bpl = (w + 7) / 8;
  bool ok = ddst_page_start(job, &pi);
  for (unsigned y = 0; ok && y < h; y++) ok = ddst_page_write_row(job, rows + (size_t)y * bpl);
  ok = ok && ddst_page_end(job);
  ok = ddst_job_end(job) && ok;
  if (!ok) return false;
  const unsigned char *p = memfind(out->data, out->len, "@PJL SET IMAGELEN=");
  if (!p) return false;
  char *end;
  unsigned long n = strtoul((const char *)p + 18, &end, 10);
  if (end[0] != '\r' || end[1] != '\n') return false;
  *jbig = (const unsigned char *)end + 2;
  *jbig_len = n;
  const unsigned char *dc = *jbig + n;
  if ((size_t)(dc - out->data) > out->len || memcmp(dc, "@PJL SET DOTCOUNT=", 18) != 0) return false;
  *dotcount = strtoul((const char *)dc + 18, NULL, 10);
  return true;
}

static unsigned long popcount_rows(unsigned w, unsigned h, const uint8_t *rows) {
  size_t bpl = (w + 7) / 8;
  unsigned long n = 0;
  for (unsigned y = 0; y < h; y++)
    for (unsigned x = 0; x < w; x++)
      n += (rows[(size_t)y * bpl + x / 8] >> (7 - x % 8)) & 1;
  return n;
}

static void check_golden(const char *name) {
  char pbm[256], jbg[256];
  snprintf(pbm, sizeof(pbm), "%s/%s.pbm", GOLDEN_DIR, name);
  snprintf(jbg, sizeof(jbg), "%s/%s.jbg", GOLDEN_DIR, name);
  unsigned w, h;
  uint8_t *rows = NULL;
  ASSERT(pbm_read(pbm, &w, &h, &rows));
  size_t ref_len = 0;
  unsigned char *ref = read_file(jbg, &ref_len);
  ASSERT(ref != NULL && ref_len > 20);
  tu_buf_t b; tu_buf_init(&b);
  const unsigned char *data; size_t len; unsigned long dots;
  ASSERT(encode_page(w, h, rows, &b, &data, &len, &dots));
  ASSERT_MEM(data, len, ref, ref_len);
  ASSERT_EQ_U(dots, popcount_rows(w, h, rows));
  tu_buf_free(&b);
  free(ref);
  free(rows);
}

static void test_golden_text(void) { check_golden("text"); }
static void test_golden_photo(void) { check_golden("photo"); }
static void test_golden_odd_width(void) { check_golden("odd-width"); }

static void test_odd_width_padding_bits_zero(void) {
  unsigned w, h;
  uint8_t *rows = NULL;
  ASSERT(pbm_read(GOLDEN_DIR "/odd-width.pbm", &w, &h, &rows));
  ASSERT(w % 8 != 0);
  size_t bpl = (w + 7) / 8;
  unsigned long clean_dots = popcount_rows(w, h, rows);
  uint8_t pad = (uint8_t)(0xFF >> (w % 8));
  for (unsigned y = 0; y < h; y++) rows[(size_t)y * bpl + bpl - 1] |= pad; /* garbage in padding */
  size_t ref_len = 0;
  unsigned char *ref = read_file(GOLDEN_DIR "/odd-width.jbg", &ref_len);
  ASSERT(ref != NULL);
  tu_buf_t b; tu_buf_init(&b);
  const unsigned char *data; size_t len; unsigned long dots;
  ASSERT(encode_page(w, h, rows, &b, &data, &len, &dots));
  ASSERT_MEM(data, len, ref, ref_len);
  ASSERT_EQ_U(dots, clean_dots);
  tu_buf_free(&b);
  free(ref);
  free(rows);
}

static void test_pbm_read_rejects_garbage(void) {
  unsigned w, h;
  uint8_t *rows = NULL;
  ASSERT(!pbm_read(GOLDEN_DIR "/make-golden.sh", &w, &h, &rows));
  ASSERT(!pbm_read(GOLDEN_DIR "/does-not-exist.pbm", &w, &h, &rows));
}

int main(void) {
  RUN(test_golden_text);
  RUN(test_golden_photo);
  RUN(test_golden_odd_width);
  RUN(test_odd_width_padding_bits_zero);
  RUN(test_pbm_read_rejects_garbage);
  TEST_EXIT();
}
