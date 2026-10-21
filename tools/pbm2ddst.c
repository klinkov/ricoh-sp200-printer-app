/* pbm2ddst: P4 PBM -> DDST stream on stdout. Hardware-test spike tool. MIT License.
 * Page geometry (PAPERWIDTH/PAPERLENGTH) = PBM dimensions; -p only sets the PAPER name. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../src/ddst/ddst.h"
#include "../src/ddst/paper.h"
#include "../src/ddst/pbm.h"

/* Lookup by PJL name (case-insensitive), via ddst_paper_at. */
static const ddst_paper_t *paper_by_pjl(const char *name) {
  for (unsigned i = 0;; i++) {
    const ddst_paper_t *p = ddst_paper_at(i);
    if (!p) return NULL;
    if (strcasecmp(p->pjl_name, name) == 0) return p;
  }
}

static size_t out_write(void *ctx, const void *buf, size_t len) {
  return fwrite(buf, 1, len, (FILE *)ctx);
}

static int usage(void) {
  fprintf(stderr, "usage: pbm2ddst [-p A4|LETTER|A5|B5|LEGAL|EXECUTIVE] [-s tray1|manual] [-c N]\n"
                  "                [-d div100|raw] in.pbm > out.ddst\n");
  return 2;
}

int main(int argc, char **argv) {
  const char *pname = "A4";
  ddst_source_t src = DDST_SOURCE_TRAY1;
  ddst_dotcount_mode_t dm = DDST_DOTCOUNT_DIV100;
  unsigned copies = 1;
  int i = 1;
  for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
    const char *a = argv[i];
    if (i + 1 >= argc) return usage();
    const char *v = argv[++i];
    if (!strcmp(a, "-p")) pname = v;
    else if (!strcmp(a, "-s")) {
      if (!strcasecmp(v, "tray1")) src = DDST_SOURCE_TRAY1;
      else if (!strcasecmp(v, "manual")) src = DDST_SOURCE_MANUAL;
      else return usage();
    } else if (!strcmp(a, "-c")) {
      long n = strtol(v, NULL, 10);
      if (n < 1 || n > 999) return usage();
      copies = (unsigned)n;
    } else if (!strcmp(a, "-d")) {
      if (!strcmp(v, "div100")) dm = DDST_DOTCOUNT_DIV100;
      else if (!strcmp(v, "raw")) dm = DDST_DOTCOUNT_RAW;
      else return usage();
    } else return usage();
  }
  if (i != argc - 1) return usage();

  const ddst_paper_t *paper = paper_by_pjl(pname);
  if (!paper) { fprintf(stderr, "pbm2ddst: unknown paper '%s'\n", pname); return 2; }

  unsigned w, h;
  uint8_t *rows;
  if (!pbm_read(argv[i], &w, &h, &rows)) {
    fprintf(stderr, "pbm2ddst: cannot read P4 file %s\n", argv[i]);
    return 1;
  }
  if (w != paper->width_px_600 || h != paper->height_px_600)
    fprintf(stderr, "pbm2ddst: note: %ux%u differs from table %ux%u for %s; using PBM size\n",
            w, h, paper->width_px_600, paper->height_px_600, paper->pjl_name);

  int rc = 1;
  ddst_job_info_t ji = { .filename = "pbm2ddst", .username = "user", .timestamp = 0, .dotcount_mode = dm };
  ddst_job_t *job = ddst_job_start(&ji, out_write, stdout);
  if (!job) { fprintf(stderr, "pbm2ddst: job start failed\n"); goto out; }
  ddst_page_info_t pi = { .paper = paper->pjl_name, .width_px = w, .height_px = h,
                          .resolution = 600, .copies = copies, .source = src };
  if (!ddst_page_start(job, &pi)) goto fail;
  size_t stride = (w + 7) / 8;
  for (unsigned y = 0; y < h; y++)
    if (!ddst_page_write_row(job, rows + (size_t)y * stride)) goto fail;
  if (!ddst_page_end(job)) goto fail;
  if (!ddst_job_end(job)) { /* frees job */
    fprintf(stderr, "pbm2ddst: job end failed (write error)\n");
    goto out;
  }
  job = NULL;
  rc = fflush(stdout) ? 1 : 0;
  goto out;
fail:
  fprintf(stderr, "pbm2ddst: %s\n", ddst_last_error(job));
  ddst_job_abort(job);
  job = NULL;
out:
  free(rows);
  return rc;
}
