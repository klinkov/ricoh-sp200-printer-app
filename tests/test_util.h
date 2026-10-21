/* Minimal test harness: no framework. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tu_failures = 0;
static int tu_current_failed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
      fprintf(stderr, "  %s:%d: ASSERT failed: %s\n", __FILE__, __LINE__, #cond); \
      tu_current_failed = 1; return; \
    } } while (0)

#define ASSERT_EQ_U(a, b) do { \
    unsigned long long tu_a = (unsigned long long)(a), tu_b = (unsigned long long)(b); \
    if (tu_a != tu_b) { \
      fprintf(stderr, "  %s:%d: %s == %llu, expected %s == %llu\n", __FILE__, __LINE__, #a, tu_a, #b, tu_b); \
      tu_current_failed = 1; return; \
    } } while (0)

#define ASSERT_MEM(got, got_len, exp, exp_len) do { \
    size_t tu_gl = (got_len), tu_el = (exp_len); \
    if (tu_gl != tu_el || memcmp((got), (exp), tu_el) != 0) { \
      fprintf(stderr, "  %s:%d: memory mismatch (got %zu bytes, expected %zu)\n", __FILE__, __LINE__, tu_gl, tu_el); \
      tu_dump("got", (const unsigned char *)(got), tu_gl); \
      tu_dump("exp", (const unsigned char *)(exp), tu_el); \
      tu_current_failed = 1; return; \
    } } while (0)

static inline void tu_dump(const char *label, const unsigned char *p, size_t n) {
  fprintf(stderr, "    %s: \"", label);
  for (size_t i = 0; i < n && i < 400; i++) {
    if (p[i] >= 0x20 && p[i] < 0x7f && p[i] != '\\') fputc(p[i], stderr);
    else fprintf(stderr, "\\x%02x", p[i]);
  }
  fprintf(stderr, "%s\"\n", n > 400 ? "..." : "");
}

#define RUN(fn) do { \
    tu_current_failed = 0; fn(); \
    if (tu_current_failed) { tu_failures++; fprintf(stderr, "FAIL %s\n", #fn); } \
    else printf("ok   %s\n", #fn); \
  } while (0)

#define TEST_EXIT() do { \
    if (tu_failures) { fprintf(stderr, "%d test(s) FAILED\n", tu_failures); return 1; } \
    printf("all passed\n"); return 0; } while (0)

/* Growable output buffer used as a ddst_write_fn sink. */
typedef struct {
  unsigned char *data;
  size_t len, cap;
  long fail_after;   /* <0: never fail; else fail once total would exceed this many bytes */
} tu_buf_t;

static inline size_t tu_buf_write(void *ctx, const void *buf, size_t len) {
  tu_buf_t *b = (tu_buf_t *)ctx;
  if (b->fail_after >= 0 && b->len + len > (size_t)b->fail_after) return 0;
  if (b->len + len > b->cap) {
    size_t nc = b->cap ? b->cap * 2 : 4096;
    while (nc < b->len + len) nc *= 2;
    unsigned char *nd = realloc(b->data, nc);
    if (!nd) return 0;
    b->data = nd; b->cap = nc;
  }
  memcpy(b->data + b->len, buf, len);
  b->len += len;
  return len;
}

static inline void tu_buf_init(tu_buf_t *b) { memset(b, 0, sizeof(*b)); b->fail_after = -1; }
static inline void tu_buf_free(tu_buf_t *b) { free(b->data); memset(b, 0, sizeof(*b)); }

#endif
