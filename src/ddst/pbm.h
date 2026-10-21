/* Minimal P4 (raw PBM) reader. MIT License. */
#ifndef DDST_PBM_H
#define DDST_PBM_H

#include <stdbool.h>
#include <stdint.h>

/* Reads a P4 file. On success *rows is malloc'ed: h rows of (w+7)/8 bytes,
 * MSB first, 1 = black. Caller frees. */
bool pbm_read(const char *path, unsigned *w, unsigned *h, uint8_t **rows);

#endif
