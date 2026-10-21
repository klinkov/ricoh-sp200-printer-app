#include "test_util.h"
#include "../src/ddst/paper.h"

static void test_paper_lookup_a4(void) {
  const ddst_paper_t *p = ddst_paper_lookup("iso_a4_210x297mm");
  ASSERT(p != NULL);
  ASSERT(strcmp(p->pjl_name, "A4") == 0);
  ASSERT_EQ_U(p->width_px_600, 4961);
  ASSERT_EQ_U(p->height_px_600, 7016);
}

static void test_paper_lookup_unknown(void) {
  ASSERT(ddst_paper_lookup("na_index-4x6_4x6in") == NULL);
  ASSERT(ddst_paper_lookup("") == NULL);
  ASSERT(ddst_paper_lookup(NULL) == NULL);
}

static void test_paper_table_full(void) {
  static const struct { const char *pwg, *pjl; unsigned w, h; } exp[] = {
    { "iso_a4_210x297mm",         "A4",        4961, 7016 },
    { "na_letter_8.5x11in",       "LETTER",    5100, 6600 },
    { "iso_a5_148x210mm",         "A5",        3496, 4961 },
    { "jis_b5_182x257mm",         "B5",        4299, 6071 },
    { "na_legal_8.5x14in",        "LEGAL",     5100, 8400 },
    { "na_executive_7.25x10.5in", "EXECUTIVE", 4350, 6300 },
  };
  unsigned n = 0;
  while (ddst_paper_at(n)) n++;
  ASSERT_EQ_U(n, sizeof(exp) / sizeof(exp[0]));
  for (unsigned i = 0; i < n; i++) {
    const ddst_paper_t *p = ddst_paper_lookup(exp[i].pwg);
    ASSERT(p != NULL);
    ASSERT(strcmp(p->pjl_name, exp[i].pjl) == 0);
    ASSERT_EQ_U(p->width_px_600, exp[i].w);
    ASSERT_EQ_U(p->height_px_600, exp[i].h);
  }
}

int main(void) {
  RUN(test_paper_lookup_a4);
  RUN(test_paper_lookup_unknown);
  RUN(test_paper_table_full);
  TEST_EXIT();
}
