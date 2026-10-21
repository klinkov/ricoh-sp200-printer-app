/* ricoh-sp200 PAPPL driver: PWG raster -> 1bpp -> libddst -> device. MIT License. */
#include "driver.h"
#include "device.h"
#include "../ddst/ddst.h"
#include "../ddst/dither.h"
#include "../ddst/paper.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RICOH_RES        600
#define RICOH_MARGIN     423 /* hundredths of mm (1/6 in), all sides */
#define RICOH_MAX_COPIES 999
#define RICOH_SIZE_TOLERANCE_MM 2.0
/* Collated copies are built in memory; past this size the job falls back to printer copies.
 * RICOH_COLLATE_MAX_BYTES overrides it (the integration test uses a small value). */
#define RICOH_COLLATE_MAX ((size_t)128 << 20)

static const char COPIES1[] = "@PJL SET COPIES=1\r\n";

pappl_pr_driver_t ricoh_drivers[] = {
  {RICOH_DRIVER_NAME, "RICOH SP 200 series (SP 202SN)", "MFG:RICOH;MDL:SP 202SN;CMD:PJL;", NULL},
};
const int ricoh_num_drivers = (int)(sizeof(ricoh_drivers) / sizeof(ricoh_drivers[0]));

/* Per-job state (papplJobSetData). */
typedef struct {
  pappl_job_t         *job;
  pappl_device_t      *device;
  ddst_job_t          *ddst;
  bool                 failed;      /* write/encode error: job is aborted, ddst already freed */
  bool                 ricoh_socket; /* device is ricoh-socket:// (has ricoh_socket_end_job) */
  /* collated copies: the whole DDST job is built in memory with COPIES=1 and replayed */
  bool                 collate;
  unsigned             copies;
  unsigned char       *mem;
  size_t               mem_len, mem_cap, mem_max;
  size_t              *copies_off;  /* offsets of "@PJL SET COPIES=1" lines in mem, one per page */
  unsigned             num_pages, cap_pages;
  /* current page */
  const ddst_paper_t  *paper;
  bool                 page_open;
  bool                 threshold;   /* print-content-optimize=text -> plain threshold */
  unsigned             src_width;   /* columns taken from the raster: min(cupsWidth, width_px) */
  ddst_dither_t       *dither;
  unsigned char       *gray;        /* src_width bytes (inverted copy for CUPS_CSPACE_K) */
  unsigned char       *row;         /* (width_px+7)/8 bytes, padding columns stay 0 = white */
} rjob_t;

static void job_error(pappl_job_t *job, pappl_jreason_t reason, const char *fmt, ...)
  __attribute__((format(printf, 3, 4)));

static void job_error(pappl_job_t *job, pappl_jreason_t reason, const char *fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "%s", msg);
  papplJobSetMessage(job, "%s", msg);
  papplJobSetReasons(job, reason, PAPPL_JREASON_NONE);
}

/* Write everything to the device; true on success. */
static bool device_write_all(rjob_t *r, const unsigned char *p, size_t len) {
  while (len > 0) {
    ssize_t n = papplDeviceWrite(r->device, p, len);
    if (n <= 0)
      return false;
    p += (size_t)n;
    len -= (size_t)n;
  }
  return true;
}

/* ddst_write_fn: must write all and return exactly len; anything else aborts the ddst job. */
static size_t ddst_write_cb(void *ctx, const void *buf, size_t len) {
  rjob_t *r = ctx;
  if (r->collate) {
    if (r->mem_len + len > r->mem_cap) {
      size_t cap = r->mem_cap ? r->mem_cap : (size_t)1 << 20;
      while (cap < r->mem_len + len)
        cap *= 2;
      unsigned char *p = realloc(r->mem, cap);
      if (!p) {
        papplLogJob(r->job, PAPPL_LOGLEVEL_ERROR, "Out of memory buffering collated copies.");
        return 0;
      }
      r->mem = p;
      r->mem_cap = cap;
    }
    memcpy(r->mem + r->mem_len, buf, len);
    r->mem_len += len;
    return len;
  }
  if (!device_write_all(r, buf, len)) {
    papplLogJob(r->job, PAPPL_LOGLEVEL_ERROR, "Device write failed (%lu bytes).", (unsigned long)len);
    return 0;
  }
  return len;
}

static void free_page(rjob_t *r) {
  ddst_dither_free(r->dither);
  r->dither = NULL;
  free(r->gray);
  r->gray = NULL;
  free(r->row);
  r->row = NULL;
  r->page_open = false;
}

static void free_rjob(rjob_t *r) {
  if (!r)
    return;
  free_page(r);
  if (r->ddst)
    ddst_job_abort(r->ddst);
  free(r->mem);
  free(r->copies_off);
  papplJobSetData(r->job, NULL);
  free(r);
}

/* A libddst call failed: report, abort the ddst job (frees it), mark job failed. */
static bool fail_ddst(rjob_t *r, const char *what) {
  job_error(r->job, PAPPL_JREASON_ABORTED_BY_SYSTEM, "%s failed: %s", what,
            r->ddst ? (ddst_last_error(r->ddst) ? ddst_last_error(r->ddst) : "unknown error") : "no job");
  if (r->ddst)
    ddst_job_abort(r->ddst);
  r->ddst = NULL;
  r->failed = true;
  free_page(r);
  return false;
}

static ddst_source_t map_source(const char *source) {
  /* PWG media-source -> MEDIASOURCE. Only "manual" (and the PWG bypass/envelope feeders that a
   * client could send for the single-sheet slot) select MANUAL; everything else (tray-1, auto,
   * main, empty) is the cassette. */
  if (source && (!strcmp(source, "manual") || !strcmp(source, "by-pass-tray") || !strcmp(source, "envelope")))
    return DDST_SOURCE_MANUAL;
  return DDST_SOURCE_TRAY1;
}

/* multiple-document-handling: absent means collated (PAPPL's default, multiple-document-handling-
 * default=separate-documents-collated-copies); only the explicit uncollated value lets the
 * printer make the copies. */
static bool want_collated(pappl_job_t *job, int copies) {
  if (copies <= 1)
    return false;
  ipp_attribute_t *a = papplJobGetAttribute(job, "multiple-document-handling");
  const char *v = a ? ippGetString(a, 0, NULL) : NULL;
  return !v || strcmp(v, "separate-documents-uncollated-copies");
}

/* papplJobFilterImage (PNG/JPEG) calls rstartpage..rendpage once per copy itself; only raster
 * documents (one pass over the pages) leave the copies to the driver. */
static bool driver_makes_copies(pappl_job_t *job) {
  const char *f = papplJobGetFormat(job);
  return f && (!strcmp(f, "image/pwg-raster") || !strcmp(f, "image/urf"));
}

static size_t collate_max(void) {
  const char *v = getenv("RICOH_COLLATE_MAX_BYTES");
  char *end;
  unsigned long long n = v ? strtoull(v, &end, 10) : 0;
  return v && *v && !*end && n > 0 ? (size_t)n : RICOH_COLLATE_MAX;
}

/* Collate buffer is over the limit (checked between pages, so it holds whole pages only): send
 * it now with COPIES=n patched into every page and let the printer make the remaining copies. */
static bool collate_fallback(rjob_t *r) {
  char line[32];
  size_t pos = 0;
  bool ok = true;
  papplLogJob(r->job, PAPPL_LOGLEVEL_WARN,
              "Collated copies: buffer reached %lu bytes (limit %lu) after %u pages; switching to printer "
              "copies (COPIES=%u, uncollated).",
              (unsigned long)r->mem_len, (unsigned long)r->mem_max, r->num_pages, r->copies);
  papplJobSetMessage(r->job, "Job too large for collated copies; printing %u uncollated copies.", r->copies);
  snprintf(line, sizeof(line), "@PJL SET COPIES=%u\r\n", r->copies);
  for (unsigned i = 0; ok && i < r->num_pages; i++) {
    ok = device_write_all(r, r->mem + pos, r->copies_off[i] - pos) &&
         device_write_all(r, (const unsigned char *)line, strlen(line));
    pos = r->copies_off[i] + sizeof(COPIES1) - 1;
  }
  ok = ok && device_write_all(r, r->mem + pos, r->mem_len - pos);
  free(r->mem);
  r->mem = NULL;
  r->mem_len = r->mem_cap = 0;
  r->collate = false;
  if (!ok)
    papplLogJob(r->job, PAPPL_LOGLEVEL_ERROR, "Device write failed sending the buffered pages.");
  return ok;
}

static bool rstartjob_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device) {
  rjob_t *r = calloc(1, sizeof(*r));
  if (!r) {
    job_error(job, PAPPL_JREASON_ABORTED_BY_SYSTEM, "Out of memory.");
    return false;
  }
  r->job = job;
  r->device = device;
  const char *uri = papplPrinterGetDeviceURI(papplJobGetPrinter(job));
  r->ricoh_socket = uri && !strncmp(uri, RICOH_SOCKET_PREFIX, strlen(RICOH_SOCKET_PREFIX));
  int copies = 1;
  if (driver_makes_copies(job))
    copies = options->copies < 1 ? 1 : options->copies > RICOH_MAX_COPIES ? RICOH_MAX_COPIES : options->copies;
  r->copies = (unsigned)copies;
  r->collate = want_collated(job, copies);
  r->mem_max = collate_max();
  papplJobSetData(job, r);

  ddst_job_info_t info = {
    .filename = papplJobGetName(job),
    .username = papplJobGetUsername(job),
    .timestamp = 0,
    .dotcount_mode = DDST_DOTCOUNT_DIV100,
  };
  papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Starting DDST job: format=%s copies=%u (%s).", papplJobGetFormat(job),
              r->copies, r->copies <= 1 ? "single copy" : r->collate ? "collated, replayed by service" : "printer copies");
  r->ddst = ddst_job_start(&info, ddst_write_cb, r);
  if (!r->ddst) {
    job_error(job, PAPPL_JREASON_ABORTED_BY_SYSTEM, "Unable to start DDST job (write error or out of memory).");
    papplDeviceFlush(device); /* rendjob will not run: reset the device for the next job */
    if (r->ricoh_socket)
      ricoh_socket_end_job(device);
    free_rjob(r);
    return false;
  }
  return true;
}

/* abort the ddst stream from rstartpage (before ddst_page_start) */
static bool page_fail(rjob_t *r, pappl_jreason_t reason, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static bool page_fail(rjob_t *r, pappl_jreason_t reason, const char *fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  job_error(r->job, reason, "%s", msg);
  free_page(r);
  ddst_job_abort(r->ddst);
  r->ddst = NULL;
  r->failed = true;
  return false;
}

static double px_to_mm(unsigned px) { return px * 25.4 / RICOH_RES; }

/* Paper for this page: media.size_name if the raster is within 2 mm of it, else the supported paper
 * whose size matches the raster (a client may send A5 pages on an A4 job); NULL if neither. */
static const ddst_paper_t *page_paper(rjob_t *r, const pappl_pr_options_t *options) {
  const cups_page_header2_t *h = &options->header;
  const ddst_paper_t *paper = ddst_paper_lookup(options->media.size_name);
  double w = px_to_mm(h->cupsWidth), l = px_to_mm(h->cupsHeight);
  if (paper && fabs(w - px_to_mm(paper->width_px_600)) <= RICOH_SIZE_TOLERANCE_MM &&
      fabs(l - px_to_mm(paper->height_px_600)) <= RICOH_SIZE_TOLERANCE_MM)
    return paper;
  pwg_media_t *pwg = pwgMediaForSize((int)lround(w * 100), (int)lround(l * 100));
  const ddst_paper_t *alt = pwg ? ddst_paper_lookup(pwg->pwg) : NULL;
  if (alt) {
    papplLogJob(r->job, PAPPL_LOGLEVEL_INFO, "Raster page is %.1fx%.1f mm, not media \"%s\": using %s.", w, l,
                options->media.size_name, alt->pwg_name);
    return alt;
  }
  return NULL;
}

/* Print-content-optimize: text and auto -> plain threshold (sharp text, no dither noise in flat
 * areas); photo, graphic, text-and-graphic -> Floyd-Steinberg. Auto on a PNG/JPEG document is a
 * picture, so it is dithered too (PAPPL makes the same choice for its own 1-bit dither). */
static bool use_threshold(pappl_job_t *job, const pappl_pr_options_t *options) {
  if (options->print_content_optimize == PAPPL_CONTENT_TEXT)
    return true;
  if (options->print_content_optimize != PAPPL_CONTENT_AUTO)
    return false;
  const char *f = papplJobGetFormat(job);
  return !(f && (!strcmp(f, "image/png") || !strcmp(f, "image/jpeg")));
}

static size_t find_copies1(const unsigned char *p, size_t from, size_t to) {
  size_t n = sizeof(COPIES1) - 1;
  for (size_t i = from; i + n <= to; i++)
    if (!memcmp(p + i, COPIES1, n))
      return i;
  return (size_t)-1;
}

static bool rstartpage_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page) {
  rjob_t *r = papplJobGetData(job);
  (void)device;
  if (!r || r->failed || !r->ddst)
    return false;

  const cups_page_header2_t *h = &options->header;
  if (h->HWResolution[0] != RICOH_RES || h->HWResolution[1] != RICOH_RES)
    return page_fail(r, PAPPL_JREASON_DOCUMENT_FORMAT_ERROR, "Unsupported raster resolution %ux%u (need %d dpi).",
                     h->HWResolution[0], h->HWResolution[1], RICOH_RES);
  /* raster_types is SGRAY_8 only (see ricoh_driver_cb), so anything else is a broken stream. */
  if (h->cupsBitsPerPixel != 8)
    return page_fail(r, PAPPL_JREASON_DOCUMENT_FORMAT_ERROR, "Unsupported raster depth %u bpp (need 8-bit gray).",
                     h->cupsBitsPerPixel);
  const ddst_paper_t *paper = page_paper(r, options);
  if (!paper)
    return page_fail(r, PAPPL_JREASON_DOCUMENT_UNPRINTABLE_ERROR,
                     "Raster page size %.1fx%.1f mm matches neither media \"%s\" nor any supported paper.",
                     px_to_mm(h->cupsWidth), px_to_mm(h->cupsHeight), options->media.size_name);

  if (r->collate && r->mem_len >= r->mem_max && !collate_fallback(r))
    return page_fail(r, PAPPL_JREASON_ABORTED_BY_SYSTEM, "Device write failed.");

  r->paper = paper;
  r->src_width = h->cupsWidth < paper->width_px_600 ? h->cupsWidth : paper->width_px_600;
  r->threshold = use_threshold(job, options);
  papplLogJob(job, PAPPL_LOGLEVEL_INFO,
              "Page %u: media=%s source=%s raster %ux%ux%u -> PAPER=%s %ux%u (%s%s).", page + 1,
              options->media.size_name, options->media.source[0] ? options->media.source : "(none)",
              h->cupsWidth, h->cupsHeight, h->cupsBitsPerPixel, paper->pjl_name, paper->width_px_600,
              paper->height_px_600,
              h->cupsWidth < paper->width_px_600 ? "padded right" : h->cupsWidth > paper->width_px_600 ? "clipped right" : "exact width",
              r->threshold ? ", threshold" : ", dither");

  r->row = calloc(1, (paper->width_px_600 + 7) / 8);
  r->gray = malloc(r->src_width ? r->src_width : 1);
  if (!r->threshold && r->src_width)
    r->dither = ddst_dither_new(r->src_width);
  if (!r->row || !r->gray || (!r->threshold && r->src_width && !r->dither))
    return page_fail(r, PAPPL_JREASON_ABORTED_BY_SYSTEM, "Out of memory.");

  ddst_page_info_t pi = {
    .paper = paper->pjl_name,
    .width_px = paper->width_px_600,
    .height_px = paper->height_px_600,
    .resolution = RICOH_RES,
    .copies = r->collate ? 1 : r->copies,
    .source = map_source(options->media.source),
  };
  size_t before = r->mem_len;
  if (!ddst_page_start(r->ddst, &pi))
    return fail_ddst(r, "ddst_page_start");
  if (r->collate) { /* remember where COPIES=1 is, for collate_fallback */
    size_t off = find_copies1(r->mem, before, r->mem_len);
    if (off == (size_t)-1)
      return fail_ddst(r, "locating COPIES in the collate buffer");
    if (r->num_pages == r->cap_pages) {
      unsigned cap = r->cap_pages ? 2 * r->cap_pages : 64;
      size_t *p = realloc(r->copies_off, cap * sizeof(*p));
      if (!p)
        return fail_ddst(r, "recording page offset (out of memory)");
      r->copies_off = p;
      r->cap_pages = cap;
    }
    r->copies_off[r->num_pages++] = off;
  }
  r->page_open = true;
  return true;
}

static bool rwriteline_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned y,
                          const unsigned char *line) {
  rjob_t *r = papplJobGetData(job);
  (void)device;
  if (!r || r->failed || !r->page_open)
    return false;
  if (papplJobIsCanceled(job))
    return true; /* rendjob aborts the stream */
  if (y >= r->paper->height_px_600)
    return true; /* longer than the paper: drop extra rows */

  const cups_page_header2_t *h = &options->header;
  unsigned w = r->src_width;
  size_t rowbytes = (r->paper->width_px_600 + 7) / 8;
  memset(r->row, 0, rowbytes);

  /* 8-bit only: rstartpage rejects other depths. */
  const unsigned char *src = line;
  if (h->cupsColorSpace == CUPS_CSPACE_K) { /* 0 = white: invert to 0 = black */
    for (unsigned x = 0; x < w; x++)
      r->gray[x] = (unsigned char)(255 - line[x]);
    src = r->gray;
  }
  if (r->threshold)
    ddst_threshold_row(src, w, r->row);
  else if (w)
    ddst_dither_row(r->dither, src, r->row);

  if (!ddst_page_write_row(r->ddst, r->row))
    return fail_ddst(r, "ddst_page_write_row");
  return true;
}

static bool rendpage_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device, unsigned page) {
  rjob_t *r = papplJobGetData(job);
  (void)options;
  (void)device;
  if (!r || r->failed || !r->page_open)
    return false;
  if (papplJobIsCanceled(job)) {
    free_page(r); /* page stays open in ddst; rendjob -> ddst_job_abort closes it */
    return true;
  }
  free_page(r);
  if (!ddst_page_end(r->ddst))
    return fail_ddst(r, "ddst_page_end");
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Page %u sent.", page + 1);
  return true;
}

static bool rendjob_cb(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device) {
  rjob_t *r = papplJobGetData(job);
  bool ok;
  (void)options;
  if (!r)
    return false;

  if (r->failed) {
    ok = false;
  } else if (papplJobIsCanceled(job)) {
    /* Cancel policy (spec §10): stop data, close page, EOJ + UEL. */
    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Job canceled, closing DDST stream.");
    /* In collate mode nothing has reached the device yet: the trailer goes to the buffer, which is dropped. */
    ddst_job_abort(r->ddst);
    r->ddst = NULL;
    ok = true;
  } else {
    ok = ddst_job_end(r->ddst);
    r->ddst = NULL;
    if (!ok) {
      job_error(job, PAPPL_JREASON_ABORTED_BY_SYSTEM, "Unable to finish DDST job (write error).");
    } else if (r->collate) {
      for (unsigned c = 0; ok && c < r->copies && !papplJobIsCanceled(job); c++) {
        papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending collated copy %u of %u (%lu bytes).", c + 1, r->copies, (unsigned long)r->mem_len);
        if (!device_write_all(r, r->mem, r->mem_len)) {
          job_error(job, PAPPL_JREASON_ABORTED_BY_SYSTEM, "Device write failed on copy %u.", c + 1);
          ok = false;
        }
      }
    }
  }
  /* The last <= 8 KB leave in papplDeviceFlush, which returns nothing: on ricoh-socket:// the end
   * of job (half-close, wait for the printer) and the device status tell whether they arrived. */
  papplDeviceFlush(device);
  if (r->ricoh_socket) {
    bool sent = ricoh_socket_end_job(device);
    if ((!sent || (papplDeviceGetStatus(device) & PAPPL_PREASON_OTHER)) && ok) {
      job_error(job, PAPPL_JREASON_ABORTED_BY_SYSTEM, "Printer connection failed at the end of the job.");
      ok = false;
    }
  }
  free_rjob(r);
  return ok;
}

static void media_col(pappl_media_col_t *m, const char *size_name, const char *source) {
  memset(m, 0, sizeof(*m));
  pwg_media_t *pwg = pwgMediaForPWG(size_name);
  snprintf(m->size_name, sizeof(m->size_name), "%s", size_name);
  snprintf(m->source, sizeof(m->source), "%s", source);
  snprintf(m->type, sizeof(m->type), "stationery");
  if (pwg) {
    m->size_width = pwg->width;
    m->size_length = pwg->length;
  }
  m->bottom_margin = m->top_margin = m->left_margin = m->right_margin = RICOH_MARGIN;
}

bool ricoh_driver_cb(pappl_system_t *system, const char *driver_name, const char *device_uri, const char *device_id,
                     pappl_pr_driver_data_t *d, ipp_t **attrs, void *cbdata) {
  (void)system;
  (void)device_uri;
  (void)device_id;
  (void)attrs;
  (void)cbdata;
  if (!driver_name || strcmp(driver_name, RICOH_DRIVER_NAME))
    return false;

  d->rstartjob_cb = rstartjob_cb;
  d->rstartpage_cb = rstartpage_cb;
  d->rwriteline_cb = rwriteline_cb;
  d->rendpage_cb = rendpage_cb;
  d->rendjob_cb = rendjob_cb;

  d->format = "application/vnd.ricoh-ddst";
  snprintf(d->make_and_model, sizeof(d->make_and_model), "RICOH SP 200 series");
  d->ppm = 22;
  d->kind = PAPPL_KIND_DOCUMENT;
  d->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
  d->color_default = PAPPL_COLOR_MODE_MONOCHROME;
  d->content_default = PAPPL_CONTENT_AUTO;
  d->quality_default = IPP_QUALITY_NORMAL;
  d->scaling_default = PAPPL_SCALING_AUTO;
  /* SGRAY_8 only, no 1-bit path: PAPPL 1.4.9 keeps its own sgray_8 options->header when a client
   * sends 1-bit raster, so the driver could not tell the line format; 8-bit is what macOS sends
   * anyway, and the driver's own threshold/dither replaces PAPPL's 1-bit dither. rstartpage aborts
   * any page whose header is not 8 bpp. */
  d->raster_types = PAPPL_PWG_RASTER_TYPE_SGRAY_8;
  d->duplex = PAPPL_DUPLEX_NONE;
  d->sides_supported = PAPPL_SIDES_ONE_SIDED;
  d->sides_default = PAPPL_SIDES_ONE_SIDED;
  d->num_resolution = 1;
  d->x_resolution[0] = d->y_resolution[0] = RICOH_RES;
  d->x_default = d->y_default = RICOH_RES;
  d->left_right = RICOH_MARGIN;
  d->bottom_top = RICOH_MARGIN;

  d->num_media = 0;
  for (unsigned i = 0; d->num_media < PAPPL_MAX_MEDIA; i++) {
    const ddst_paper_t *p = ddst_paper_at(i);
    if (!p)
      break;
    d->media[d->num_media++] = p->pwg_name;
  }
  d->num_source = 2;
  d->source[0] = "tray-1";
  d->source[1] = "manual";
  d->num_type = 1;
  d->type[0] = "stationery";
  media_col(&d->media_default, "iso_a4_210x297mm", "tray-1");
  media_col(&d->media_ready[0], "iso_a4_210x297mm", "tray-1");
  media_col(&d->media_ready[1], "iso_a4_210x297mm", "manual");
  return true;
}

/* Copy a device-id value without surrounding blanks ("RICOH " in the real ID). */
static const char *id_value(int n, cups_option_t *pairs, const char *k1, const char *k2, char *buf, size_t bufsize) {
  const char *v = cupsGetOption(k1, n, pairs);
  if (!v)
    v = cupsGetOption(k2, n, pairs);
  if (!v)
    return NULL;
  while (isspace((unsigned char)*v))
    v++;
  snprintf(buf, bufsize, "%s", v);
  for (size_t len = strlen(buf); len > 0 && isspace((unsigned char)buf[len - 1]); len--)
    buf[len - 1] = '\0';
  return buf;
}

static bool sp200_model(const char *model) {
  return !strncasecmp(model, "SP 20", 5) || !strncasecmp(model, "SP20", 4);
}

/* Real SP 202SN device-id: "MFG:RICOH ;CMD:GDI;MDL:SP 202SN DDST;CLS:PRINTER;". If the ID names a
 * manufacturer and model, they decide; otherwise fall back to device_info ("RICOH SP 202SN ..."). */
const char *ricoh_autoadd_cb(const char *device_info, const char *device_uri, const char *device_id, void *data) {
  (void)device_uri;
  (void)data;
  if (device_id && *device_id) {
    cups_option_t *pairs = NULL;
    char mfg[128], mdl[128];
    int n = papplDeviceParseID(device_id, &pairs);
    const char *m = id_value(n, pairs, "MFG", "MANUFACTURER", mfg, sizeof(mfg));
    const char *d = id_value(n, pairs, "MDL", "MODEL", mdl, sizeof(mdl));
    bool decided = m && d, match = decided && !strcasecmp(m, "RICOH") && sp200_model(d);
    cupsFreeOptions(n, pairs);
    if (decided)
      return match ? RICOH_DRIVER_NAME : NULL;
  }
  if (device_info && !strncasecmp(device_info, "RICOH SP 20", 11))
    return RICOH_DRIVER_NAME;
  return NULL;
}
