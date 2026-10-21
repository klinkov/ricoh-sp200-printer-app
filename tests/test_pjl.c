#define _POSIX_C_SOURCE 200809L
#include "test_util.h"
#include "../src/ddst/ddst.h"

#include <time.h>

#define UEL "\x1b%-12345X"

static const char PREAMBLE[] =
  UEL "@PJL\r\n"
  "@PJL SET TIMESTAMP=2026/10/21 12:34:56\r\n"
  "@PJL SET FILENAME=t.pdf\r\n"
  "@PJL SET COMPRESS=JBIG\r\n"
  "@PJL SET USERNAME=shade\r\n"
  "@PJL SET COVER=OFF\r\n"
  "@PJL SET HOLD=OFF\r\n";

/* 2026-10-21 12:34:56 UTC */
#define TS ((time_t)1792586096)

static ddst_job_info_t job_info(ddst_dotcount_mode_t mode) {
  ddst_job_info_t ji = { "t.pdf", "shade", TS, mode };
  return ji;
}

static ddst_page_info_t page_16x8(void) {
  ddst_page_info_t pi = { "A4", 16, 8, 600, 2, DDST_SOURCE_MANUAL };
  return pi;
}

/* Returns pointer to the value after "@PJL SET <key>=" in buf, or NULL. */
static const char *find_value(const tu_buf_t *b, const char *key) {
  char pat[64];
  snprintf(pat, sizeof(pat), "@PJL SET %s=", key);
  size_t pl = strlen(pat);
  for (size_t i = 0; i + pl <= b->len; i++)
    if (memcmp(b->data + i, pat, pl) == 0) return (const char *)b->data + i + pl;
  return NULL;
}

static void test_job_preamble_exact(void) {
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT_MEM(b.data, b.len, PREAMBLE, sizeof(PREAMBLE) - 1);
  ddst_job_abort(job);
  tu_buf_free(&b);
}

static void test_page_frame_exact(void) {
  static const char head[] =
    "@PJL SET PAGESTATUS=START\r\n"
    "@PJL SET COPIES=2\r\n"
    "@PJL SET MEDIASOURCE=MANUAL\r\n"
    "@PJL SET MEDIATYPE=PLAINRECYCLE\r\n"
    "@PJL SET PAPER=A4\r\n"
    "@PJL SET PAPERWIDTH=16\r\n"
    "@PJL SET PAPERLENGTH=8\r\n"
    "@PJL SET RESOLUTION=600\r\n"
    "@PJL SET IMAGELEN=";
  static const char tail[] =
    "@PJL SET DOTCOUNT=0\r\n"
    "@PJL SET PAGESTATUS=END\r\n";
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ddst_page_info_t pi = page_16x8();
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  size_t start = b.len;
  ASSERT(ddst_page_start(job, &pi));
  uint8_t row[2] = { 0, 0 };
  for (int i = 0; i < 8; i++) ASSERT(ddst_page_write_row(job, row));
  ASSERT(ddst_page_end(job));
  const unsigned char *p = b.data + start;
  size_t n = b.len - start;
  size_t hl = sizeof(head) - 1, tl = sizeof(tail) - 1;
  ASSERT(n > hl + tl);
  ASSERT_MEM(p, hl, head, hl);
  /* IMAGELEN=<N>\r\n, then exactly N bytes of JBIG data, then the tail */
  char *end = NULL;
  unsigned long imagelen = strtoul((const char *)p + hl, &end, 10);
  ASSERT(end && end[0] == '\r' && end[1] == '\n');
  size_t data_off = (size_t)((const unsigned char *)end + 2 - p);
  ASSERT_EQ_U(n, data_off + imagelen + tl);
  ASSERT_MEM(p + data_off + imagelen, tl, tail, tl);
  ddst_job_abort(job);
  tu_buf_free(&b);
}

static void test_job_end_exact(void) {
  static const char tail[] = "@PJL EOJ\r\n" UEL;
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ddst_page_info_t pi = page_16x8();
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  ASSERT(ddst_page_end(job));
  ASSERT(ddst_job_end(job));
  size_t tl = sizeof(tail) - 1;
  ASSERT(b.len >= tl);
  ASSERT_MEM(b.data + b.len - tl, tl, tail, tl);
  tu_buf_free(&b);
}

static unsigned long dotcount_for(ddst_dotcount_mode_t mode) {
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(mode);
  ddst_page_info_t pi = page_16x8();
  unsigned long v = (unsigned long)-1;
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  if (!job) return v;
  uint8_t black[2] = { 0xFF, 0xFF };
  if (ddst_page_start(job, &pi) &&
      ddst_page_write_row(job, black) && ddst_page_write_row(job, black) &&
      ddst_page_write_row(job, black) && ddst_page_end(job)) {
    const char *s = find_value(&b, "DOTCOUNT");
    if (s) v = strtoul(s, NULL, 10);
  }
  ddst_job_abort(job);
  tu_buf_free(&b);
  return v;
}

static void test_dotcount_div100_and_raw(void) {
  ASSERT_EQ_U(dotcount_for(DDST_DOTCOUNT_DIV100), 0);
  ASSERT_EQ_U(dotcount_for(DDST_DOTCOUNT_RAW), 48);
}

static void test_page_end_without_start_fails(void) {
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  size_t before = b.len;
  ASSERT(!ddst_page_end(job));
  ASSERT(ddst_last_error(job) != NULL && ddst_last_error(job)[0] != '\0');
  ASSERT_EQ_U(b.len, before);
  uint8_t row[2] = { 0, 0 };
  ASSERT(!ddst_page_write_row(job, row));
  ddst_job_abort(job);
  tu_buf_free(&b);
}

static void test_zero_height_page_fails(void) {
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ddst_page_info_t pi = page_16x8();
  pi.height_px = 0;
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  size_t before = b.len;
  ASSERT(!ddst_page_start(job, &pi));
  ASSERT(ddst_last_error(job)[0] != '\0');
  ASSERT_EQ_U(b.len, before);
  ddst_job_abort(job);
  tu_buf_free(&b);
}

static void test_short_page_pads_white(void) {
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_RAW);
  ddst_page_info_t pi = page_16x8();
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  uint8_t row[2] = { 0x80, 0x01 };
  ASSERT(ddst_page_write_row(job, row));
  ASSERT(ddst_page_write_row(job, row));
  ASSERT_EQ_U(ddst_page_rows_written(job), 2);
  ASSERT(ddst_page_end(job));
  const char *s = find_value(&b, "DOTCOUNT");
  ASSERT(s != NULL);
  ASSERT_EQ_U(strtoul(s, NULL, 10), 4);
  ddst_job_abort(job);
  tu_buf_free(&b);
}

static void test_too_many_rows_fails(void) {
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_RAW);
  ddst_page_info_t pi = page_16x8();
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  uint8_t row[2] = { 0, 0 };
  for (int i = 0; i < 8; i++) ASSERT(ddst_page_write_row(job, row));
  ASSERT(!ddst_page_write_row(job, row));
  ddst_job_abort(job);
  tu_buf_free(&b);
}

static void test_write_error_propagates(void) {
  /* failure on the very first write -> job_start NULL */
  tu_buf_t b; tu_buf_init(&b);
  b.fail_after = 0;
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ASSERT(ddst_job_start(&ji, tu_buf_write, &b) == NULL);
  tu_buf_free(&b);

  /* preamble passes, page fails -> page_end false, job_end false */
  /* measure where the page header ends, then fail just after it */
  ddst_page_info_t pi = page_16x8();
  tu_buf_init(&b);
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  long hdr_end = (long)b.len;
  ddst_job_abort(job);
  tu_buf_free(&b);

  tu_buf_init(&b);
  b.fail_after = hdr_end + 5;
  job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  ASSERT(!ddst_page_end(job));
  ASSERT(ddst_last_error(job)[0] != '\0');
  ASSERT(!ddst_job_end(job));
  tu_buf_free(&b);

  /* failure inside the JBIG data: IMAGELEN line passes, payload write fails */
  tu_buf_init(&b);
  job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  uint8_t row[2] = { 0, 0 };
  for (int i = 0; i < 8; i++) ASSERT(ddst_page_write_row(job, row));
  ASSERT(ddst_page_end(job));
  const char *iv = find_value(&b, "IMAGELEN");
  ASSERT(iv != NULL);
  char *e = NULL;
  unsigned long ilen = strtoul(iv, &e, 10);
  ASSERT(ilen > 4 && e[0] == '\r');
  long jbig_start = (long)((const unsigned char *)e + 2 - b.data);
  ddst_job_abort(job);
  tu_buf_free(&b);

  tu_buf_init(&b);
  b.fail_after = jbig_start + (long)ilen / 2;
  job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  for (int i = 0; i < 8; i++) ASSERT(ddst_page_write_row(job, row));
  ASSERT(!ddst_page_end(job));
  size_t len_after = b.len;
  ASSERT(!ddst_job_end(job));
  ASSERT_EQ_U(b.len, len_after);

  /* abort after a sticky write error appends nothing */
  tu_buf_free(&b);
  tu_buf_init(&b);
  b.fail_after = jbig_start + (long)ilen / 2;
  job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  for (int i = 0; i < 8; i++) ASSERT(ddst_page_write_row(job, row));
  ASSERT(!ddst_page_end(job));
  len_after = b.len;
  ddst_job_abort(job);
  ASSERT_EQ_U(b.len, len_after);
  tu_buf_free(&b);
}

static void test_dotcount_div100_large(void) {
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ddst_page_info_t pi = page_16x8();
  pi.width_px = 32;
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  uint8_t black[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
  for (int i = 0; i < 8; i++) ASSERT(ddst_page_write_row(job, black));
  ASSERT(ddst_page_end(job));
  const char *s = find_value(&b, "DOTCOUNT");
  ASSERT(s != NULL);
  ASSERT_EQ_U(strtoul(s, NULL, 10), 2); /* 256 / 100 */
  ddst_job_abort(job);
  tu_buf_free(&b);
}

static void test_invalid_page_info_rejected(void) {
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  size_t before = b.len;
  ddst_page_info_t pi = page_16x8();
  pi.paper = "\x01\x02";
  ASSERT(!ddst_page_start(job, &pi));
  pi = page_16x8(); pi.width_px = 9001;
  ASSERT(!ddst_page_start(job, &pi));
  pi = page_16x8(); pi.height_px = 13001;
  ASSERT(!ddst_page_start(job, &pi));
  pi = page_16x8(); pi.resolution = 300;
  ASSERT(!ddst_page_start(job, &pi));
  pi = page_16x8(); pi.source = (ddst_source_t)7;
  ASSERT(!ddst_page_start(job, &pi));
  pi = page_16x8(); pi.copies = 0;
  ASSERT(!ddst_page_start(job, &pi));
  pi = page_16x8(); pi.copies = 1000;
  ASSERT(!ddst_page_start(job, &pi));
  ASSERT_EQ_U(b.len, before);
  pi = page_16x8(); pi.copies = 999; pi.width_px = 9000; pi.height_px = 13000;
  ASSERT(ddst_page_start(job, &pi));
  ddst_job_abort(job);
  tu_buf_free(&b);
}

static void test_abort_closes_open_page(void) {
  static const char tail[] = "@PJL SET PAGESTATUS=END\r\n@PJL EOJ\r\n" UEL;
  tu_buf_t b; tu_buf_init(&b);
  ddst_job_info_t ji = job_info(DDST_DOTCOUNT_DIV100);
  ddst_page_info_t pi = page_16x8();
  ddst_job_t *job = ddst_job_start(&ji, tu_buf_write, &b);
  ASSERT(job != NULL);
  ASSERT(ddst_page_start(job, &pi));
  ddst_job_abort(job);
  size_t tl = sizeof(tail) - 1;
  ASSERT(b.len >= tl);
  ASSERT_MEM(b.data + b.len - tl, tl, tail, tl);
  tu_buf_free(&b);
}

int main(void) {
  setenv("TZ", "UTC", 1);
  tzset();
  RUN(test_job_preamble_exact);
  RUN(test_page_frame_exact);
  RUN(test_job_end_exact);
  RUN(test_dotcount_div100_and_raw);
  RUN(test_page_end_without_start_fails);
  RUN(test_zero_height_page_fails);
  RUN(test_short_page_pads_white);
  RUN(test_too_many_rows_fails);
  RUN(test_write_error_propagates);
  RUN(test_dotcount_div100_large);
  RUN(test_invalid_page_info_rejected);
  RUN(test_abort_closes_open_page);
  TEST_EXIT();
}
