/* PWG media name -> PJL paper name + 600 dpi geometry. MIT License.
 * Pixel sizes: mm * 600 / 25.4 rounded to nearest (inches * 600 exact).
 * Single source of truth; revise after hardware stage 0a. */
#include "paper.h"

#include <stddef.h>
#include <string.h>

static const ddst_paper_t papers[] = {
  { "iso_a4_210x297mm",         "A4",        4961, 7016 },
  { "na_letter_8.5x11in",       "LETTER",    5100, 6600 },
  { "iso_a5_148x210mm",         "A5",        3496, 4961 },
  { "jis_b5_182x257mm",         "B5",        4299, 6071 },
  { "na_legal_8.5x14in",        "LEGAL",     5100, 8400 },
  { "na_executive_7.25x10.5in", "EXECUTIVE", 4350, 6300 },
};

const ddst_paper_t *ddst_paper_lookup(const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; i < sizeof(papers) / sizeof(papers[0]); i++)
    if (strcmp(papers[i].pwg_name, name) == 0) return &papers[i];
  return NULL;
}

const ddst_paper_t *ddst_paper_at(unsigned i) {
  return i < sizeof(papers) / sizeof(papers[0]) ? &papers[i] : NULL;
}
