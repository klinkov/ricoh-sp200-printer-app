/* Minimal P4 (raw PBM) reader. MIT License. */
#include "pbm.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define PBM_MAX_BYTES ((size_t)64 << 20) /* total bitmap size cap: 64 MiB */

/* Reads one header integer, skipping whitespace and '#' comments. */
static bool read_uint(FILE *f, unsigned *out) {
  int c;
  for (;;) {
    c = fgetc(f);
    if (c == '#') {
      while ((c = fgetc(f)) != EOF && c != '\n') {}
      continue;
    }
    if (c == EOF) return false;
    if (!isspace(c)) break;
  }
  if (!isdigit(c)) return false;
  unsigned long v = 0;
  while (c != EOF && isdigit(c)) {
    v = v * 10 + (unsigned long)(c - '0');
    if (v > 1000000UL) return false;
    c = fgetc(f);
  }
  /* exactly one whitespace char terminates the last header field */
  if (c != EOF && !isspace(c)) return false;
  *out = (unsigned)v;
  return true;
}

bool pbm_read(const char *path, unsigned *w, unsigned *h, uint8_t **rows) {
  if (!path || !w || !h || !rows) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  bool ok = false;
  uint8_t *buf = NULL;
  unsigned ww, hh;
  int m0 = fgetc(f), m1 = fgetc(f), m2 = fgetc(f);
  if (m0 == 'P' && m1 == '4' && m2 != EOF && isspace(m2) && read_uint(f, &ww) &&
      read_uint(f, &hh) && ww > 0 && hh > 0 &&
      (((size_t)ww + 7) / 8) * hh <= PBM_MAX_BYTES) {
    size_t n = ((size_t)ww + 7) / 8 * hh;
    buf = malloc(n);
    if (buf && fread(buf, 1, n, f) == n) ok = true;
  }
  fclose(f);
  if (!ok) { free(buf); return false; }
  *w = ww; *h = hh; *rows = buf;
  return true;
}
