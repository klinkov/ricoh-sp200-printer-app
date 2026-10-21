/* libddst: RICOH DDST stream writer (PJL frames + JBIG1). MIT License. */
#ifndef DDST_H
#define DDST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t (*ddst_write_fn)(void *ctx, const void *buf, size_t len); /* returns len on success */

typedef enum { DDST_SOURCE_TRAY1 = 0, DDST_SOURCE_MANUAL = 1 } ddst_source_t;
typedef enum { DDST_DOTCOUNT_DIV100 = 0, DDST_DOTCOUNT_RAW = 1 } ddst_dotcount_mode_t;

typedef struct {
  const char *filename;   /* NULL -> "job" */
  const char *username;   /* NULL -> "user" */
  time_t      timestamp;  /* 0 -> time(NULL); formatted in local time */
  ddst_dotcount_mode_t dotcount_mode;
} ddst_job_info_t;

typedef struct {
  const char   *paper;               /* PJL paper name: "A4", "LETTER", ... */
  unsigned      width_px, height_px; /* PAPERWIDTH/PAPERLENGTH and bitmap geometry */
  unsigned      resolution;          /* 600 */
  unsigned      copies;              /* >= 1 */
  ddst_source_t source;
} ddst_page_info_t;

typedef struct ddst_job ddst_job_t;

ddst_job_t *ddst_job_start(const ddst_job_info_t *info, ddst_write_fn write, void *ctx);
bool        ddst_page_start(ddst_job_t *job, const ddst_page_info_t *page);
bool        ddst_page_write_row(ddst_job_t *job, const uint8_t *row); /* packed 1bpp, MSB first, 1=black */
bool        ddst_page_end(ddst_job_t *job);
bool        ddst_job_end(ddst_job_t *job);   /* frees job */
void        ddst_job_abort(ddst_job_t *job); /* frees job */
const char *ddst_last_error(const ddst_job_t *job);
unsigned    ddst_page_rows_written(const ddst_job_t *job);

#ifdef __cplusplus
}
#endif

#endif
