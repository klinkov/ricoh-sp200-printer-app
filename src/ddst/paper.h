/* PWG media name -> PJL paper name + 600 dpi geometry. MIT License. */
#ifndef DDST_PAPER_H
#define DDST_PAPER_H

typedef struct {
  const char *pwg_name;
  const char *pjl_name;
  unsigned    width_px_600;
  unsigned    height_px_600;
} ddst_paper_t;

/* NULL if the size is not supported by the printer. */
const ddst_paper_t *ddst_paper_lookup(const char *pwg_media_size_name);
/* Table access for enumeration: returns entry i or NULL past the end. */
const ddst_paper_t *ddst_paper_at(unsigned i);

#endif
