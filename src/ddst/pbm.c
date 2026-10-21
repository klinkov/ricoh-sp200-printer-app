/* Minimal P4 (raw PBM) reader. MIT License. */
#include "pbm.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

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
  if (fgetc(f) == 'P' && fgetc(f) == '4' && read_uint(f, &ww) && read_uint(f, &hh) &&
      ww > 0 && hh > 0) {
    size_t n = ((size_t)ww + 7) / 8 * hh;
    buf = malloc(n);
    if (buf && fread(buf, 1, n, f) == n) ok = true;
  }
  fclose(f);
  if (!ok) { free(buf); return false; }
  *w = ww; *h = hh; *rows = buf;
  return true;
}
