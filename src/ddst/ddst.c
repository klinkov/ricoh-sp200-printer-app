/* libddst: RICOH DDST stream writer (PJL frames + JBIG1). MIT License. */
#define _POSIX_C_SOURCE 200809L
#include "ddst.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jbig.h>

#define DDST_UEL "\x1b%-12345X"

struct ddst_job {
  ddst_write_fn write;
  void         *ctx;
  ddst_dotcount_mode_t dotcount_mode;
  bool          write_failed;  /* sticky: any write error during the job */
  bool          page_open;
  ddst_page_info_t page;
  size_t        bpl;           /* bytes per bitmap line */
  uint8_t      *bitmap;        /* bpl * height_px, 1 = black */
  unsigned      rows;          /* rows written to current page */
  char          error[128];
};

static void set_error(ddst_job_t *job, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(job->error, sizeof(job->error), fmt, ap);
  va_end(ap);
}

static bool emit(ddst_job_t *job, const void *buf, size_t len) {
  if (job->write_failed) return false;
  if (len == 0) return true;
  if (job->write(job->ctx, buf, len) != len) {
    job->write_failed = true;
    if (!job->error[0]) set_error(job, "write failed"); /* keep the first error */
    return false;
  }
  return true;
}

static bool emit_str(ddst_job_t *job, const char *s) { return emit(job, s, strlen(s)); }

/* NB: DDST_UEL contains '%' - never pass it as a printf format. */
static bool emitf(ddst_job_t *job, const char *fmt, ...) {
  char line[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= sizeof(line)) {
    set_error(job, "PJL line too long");
    return false;
  }
  return emit(job, line, (size_t)n);
}

/* PJL values must not break the line: drop CR/LF and other control chars. */
static void sanitize(char *dst, size_t dstlen, const char *src) {
  size_t j = 0;
  for (; *src && j + 1 < dstlen; src++) {
    unsigned char c = (unsigned char)*src;
    if (c >= 0x20 && c != 0x7f) dst[j++] = (char)c;
  }
  dst[j] = '\0';
}

static void free_page(ddst_job_t *job) {
  free(job->bitmap);
  job->bitmap = NULL;
  job->page_open = false;
  job->rows = 0;
}

ddst_job_t *ddst_job_start(const ddst_job_info_t *info, ddst_write_fn write, void *ctx) {
  if (!write) return NULL;
  ddst_job_t *job = calloc(1, sizeof(*job));
  if (!job) return NULL;
  job->write = write;
  job->ctx = ctx;
  job->dotcount_mode = info ? info->dotcount_mode : DDST_DOTCOUNT_DIV100;

  char fname[64], uname[64], ts[32];
  sanitize(fname, sizeof(fname), (info && info->filename) ? info->filename : "job");
  sanitize(uname, sizeof(uname), (info && info->username) ? info->username : "user");
  time_t t = (info && info->timestamp) ? info->timestamp : time(NULL);
  struct tm tm;
  if (!localtime_r(&t, &tm) || strftime(ts, sizeof(ts), "%Y/%m/%d %H:%M:%S", &tm) == 0)
    snprintf(ts, sizeof(ts), "1970/01/01 00:00:00");

  bool ok = emit_str(job, DDST_UEL "@PJL\r\n") &&
            emitf(job, "@PJL SET TIMESTAMP=%s\r\n", ts) &&
            emitf(job, "@PJL SET FILENAME=%s\r\n", fname) &&
            emitf(job, "@PJL SET COMPRESS=JBIG\r\n") &&
            emitf(job, "@PJL SET USERNAME=%s\r\n", uname) &&
            emitf(job, "@PJL SET COVER=OFF\r\n") &&
            emitf(job, "@PJL SET HOLD=OFF\r\n");
  if (!ok) {
    free(job);
    return NULL;
  }
  return job;
}

#define DDST_MAX_WIDTH_PX  9000   /* 600 dpi * 15 in, with margin */
#define DDST_MAX_HEIGHT_PX 13000
#define DDST_MAX_COPIES    999

bool ddst_page_start(ddst_job_t *job, const ddst_page_info_t *page) {
  if (!job) return false;
  job->error[0] = '\0';
  if (job->page_open) { set_error(job, "page already started"); return false; }
  if (!page || !page->paper) { set_error(job, "no paper name"); return false; }
  char paper[32];
  sanitize(paper, sizeof(paper), page->paper);
  if (!paper[0]) { set_error(job, "no paper name"); return false; }
  if (page->width_px == 0 || page->height_px == 0) { set_error(job, "empty page geometry"); return false; }
  if (page->width_px > DDST_MAX_WIDTH_PX || page->height_px > DDST_MAX_HEIGHT_PX) {
    set_error(job, "page too large");
    return false;
  }
  if (page->resolution != 600) { set_error(job, "unsupported resolution"); return false; }
  if ((unsigned)page->source != (unsigned)DDST_SOURCE_TRAY1 &&
      (unsigned)page->source != (unsigned)DDST_SOURCE_MANUAL) {
    set_error(job, "invalid media source");
    return false;
  }
  if (page->copies < 1 || page->copies > DDST_MAX_COPIES) { set_error(job, "copies must be 1..999"); return false; }
  if (job->write_failed) { set_error(job, "earlier write failed"); return false; }

  size_t bpl = ((size_t)page->width_px + 7) / 8;
  job->bitmap = calloc(bpl * page->height_px, 1);
  if (!job->bitmap) { set_error(job, "out of memory"); return false; }
  job->bpl = bpl;
  job->page = *page;
  job->rows = 0;
  job->page_open = true;

  bool ok = emitf(job, "@PJL SET PAGESTATUS=START\r\n") &&
            emitf(job, "@PJL SET COPIES=%u\r\n", page->copies) &&
            emitf(job, "@PJL SET MEDIASOURCE=%s\r\n",
                  page->source == DDST_SOURCE_MANUAL ? "MANUAL" : "TRAY1") &&
            emitf(job, "@PJL SET MEDIATYPE=PLAINRECYCLE\r\n") &&
            emitf(job, "@PJL SET PAPER=%s\r\n", paper) &&
            emitf(job, "@PJL SET PAPERWIDTH=%u\r\n", page->width_px) &&
            emitf(job, "@PJL SET PAPERLENGTH=%u\r\n", page->height_px) &&
            emitf(job, "@PJL SET RESOLUTION=%u\r\n", page->resolution);
  if (!ok) free_page(job); /* caller must now call ddst_job_abort only */
  return ok;
}

bool ddst_page_write_row(ddst_job_t *job, const uint8_t *row) {
  if (!job) return false;
  job->error[0] = '\0';
  if (!job->page_open) { set_error(job, "no page started"); return false; }
  if (!row) { set_error(job, "NULL row"); return false; }
  if (job->rows >= job->page.height_px) { set_error(job, "too many rows"); return false; }
  uint8_t *dst = job->bitmap + (size_t)job->rows * job->bpl;
  memcpy(dst, row, job->bpl);
  unsigned rem = job->page.width_px & 7;
  if (rem) dst[job->bpl - 1] &= (uint8_t)(0xFF << (8 - rem)); /* padding bits must be white */
  job->rows++;
  return true;
}

static unsigned long long popcount_bitmap(const uint8_t *p, size_t n) {
  unsigned long long total = 0;
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    unsigned long long w;
    memcpy(&w, p + i, 8);
    total += (unsigned long long)__builtin_popcountll(w);
  }
  for (; i < n; i++) total += (unsigned long long)__builtin_popcount(p[i]);
  return total;
}

/*
 * JBIG parameters: byte-exact equivalent of `pbmtojbg -p 72 -o 3 -m 0 -q`
 * (derived from jbigkit 2.2 pbmtools/pbmtojbg.c and libjbig/jbig.c):
 *
 *   -p 72  options byte = 72 = 0x48 = JBG_LRLTWO (0x40) | JBG_TPBON (0x08).
 *          (Not HITOLO|VLENGTH: those are 0x08 in the *order* byte / 0x20 in options.)
 *          Replaces pbmtojbg's default TPDON|TPBON|DPON (28). No DELAY_AT/SDRST
 *          since -c/-r are not given.
 *   -o 3   order byte = JBG_ILEAVE | JBG_SMID (also the libjbig default).
 *   -m 0   mx = 0: adaptive-template pixel max horizontal offset 0 (AT disabled).
 *          my is passed as -1 by pbmtojbg -> stays at the jbg_enc_init default 0.
 *   -q     sets d = 0, i.e. pbmtojbg calls jbg_enc_layers(&s, 0): a single
 *          resolution layer, no progressive/differential layers ("sequential").
 *          Without -q pbmtojbg would call jbg_enc_lrlmax(&s, 640, 480) and pick
 *          as many layers as needed to get the lowest layer <= 640x480.
 *   l0     no -s given -> pbmtojbg passes l0 = 0 to jbg_enc_options, which is
 *          ignored, so the stripe height is jbg_set_default_l0() as computed by
 *          jbg_enc_layers(): l0 = height / 35, decremented while l0 > 128,
 *          minimum 2. So 128 for A4@600 (7016/35 = 200 -> 128), but smaller for
 *          short images (e.g. 400 rows -> 11). We reproduce this by calling
 *          jbg_enc_layers(0) and passing l0 = 0, rather than hardcoding 128.
 *   jbg_enc_lrange(-1, -1) in pbmtojbg is a no-op; planes = 1; no comment.
 *
 * Resulting calls:
 *   jbg_enc_init(&s, w, h, 1, &bitmap, cb, ctx);
 *   jbg_enc_layers(&s, 0);
 *   jbg_enc_options(&s, JBG_ILEAVE | JBG_SMID, JBG_LRLTWO | JBG_TPBON, 0, 0, -1);
 *   jbg_enc_out(&s); jbg_enc_free(&s);
 */
#define DDST_JBIG_ORDER   (JBG_ILEAVE | JBG_SMID)  /* 3  */
#define DDST_JBIG_OPTIONS (JBG_LRLTWO | JBG_TPBON) /* 72 */
#define DDST_JBIG_MX      0

typedef struct {
  unsigned char *data;
  size_t len, cap;
  bool oom;
} jbig_out_t;

static void jbig_out_cb(unsigned char *start, size_t len, void *file) {
  jbig_out_t *o = (jbig_out_t *)file;
  if (o->oom || len == 0) return;
  if (o->len + len > o->cap) {
    size_t nc = o->cap ? o->cap : 65536;
    while (nc < o->len + len) nc *= 2;
    unsigned char *nd = realloc(o->data, nc);
    if (!nd) { o->oom = true; return; }
    o->data = nd;
    o->cap = nc;
  }
  memcpy(o->data + o->len, start, len);
  o->len += len;
}

/* NB: libjbig calls abort() on its own allocation failure (jbg_enc_init/jbg_enc_out
 * use checked_malloc); only our output-buffer OOM is recoverable. This is why page
 * dimensions are capped in ddst_page_start. */
static bool jbig_encode(ddst_job_t *job, jbig_out_t *out) {
  struct jbg_enc_state s;
  unsigned char *planes[1] = { job->bitmap };
  jbg_enc_init(&s, job->page.width_px, job->page.height_px, 1, planes, jbig_out_cb, out);
  jbg_enc_layers(&s, 0);
  jbg_enc_options(&s, DDST_JBIG_ORDER, DDST_JBIG_OPTIONS, 0, DDST_JBIG_MX, -1);
  jbg_enc_out(&s);
  jbg_enc_free(&s);
  if (out->oom) { set_error(job, "out of memory in JBIG encoder"); return false; }
  if (out->len == 0) { set_error(job, "JBIG encoder produced no data"); return false; }
  return true;
}

bool ddst_page_end(ddst_job_t *job) {
  if (!job) return false;
  job->error[0] = '\0';
  if (!job->page_open) { set_error(job, "ddst_page_end without ddst_page_start"); return false; }

  /* rows not written stay white (calloc); padding bits were cleared on write */
  unsigned long long dots = popcount_bitmap(job->bitmap, job->bpl * job->page.height_px);
  if (job->dotcount_mode == DDST_DOTCOUNT_DIV100) dots /= 100;

  jbig_out_t out = { NULL, 0, 0, false };
  bool ok = !job->write_failed && jbig_encode(job, &out);
  if (ok)
    ok = emitf(job, "@PJL SET IMAGELEN=%zu\r\n", out.len) &&
         emit(job, out.data, out.len) &&
         emitf(job, "@PJL SET DOTCOUNT=%llu\r\n", dots) &&
         emit_str(job, "@PJL SET PAGESTATUS=END\r\n");
  else if (!job->write_failed)
    emit_str(job, "@PJL SET PAGESTATUS=END\r\n"); /* close the page frame */
  if (!ok && !job->error[0]) set_error(job, "write failed");
  free(out.data);
  free_page(job);
  return ok;
}

bool ddst_job_end(ddst_job_t *job) {
  if (!job) return false;
  job->error[0] = '\0';
  bool ok = true;
  if (job->page_open) {
    ok = false;
    emitf(job, "@PJL SET PAGESTATUS=END\r\n");
    free_page(job);
  }
  ok = emit_str(job, "@PJL EOJ\r\n" DDST_UEL) && ok && !job->write_failed;
  free(job);
  return ok;
}

void ddst_job_abort(ddst_job_t *job) {
  if (!job) return;
  if (job->page_open) emitf(job, "@PJL SET PAGESTATUS=END\r\n");
  emit_str(job, "@PJL EOJ\r\n" DDST_UEL);
  free_page(job);
  free(job);
}

const char *ddst_last_error(const ddst_job_t *job) {
  return job ? job->error : "no job";
}

unsigned ddst_page_rows_written(const ddst_job_t *job) {
  return job ? job->rows : 0;
}
