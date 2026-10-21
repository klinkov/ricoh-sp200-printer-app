/* Mock raw-9100 printer: accepts TCP connections on 127.0.0.1:PORT and saves non-empty connections
 * to OUTFILE (first), OUTFILE.2, OUTFILE.3, ...; exits after CONNS of them (default 1).
 *   --close-after N  close the first connection after N bytes (a printer dropping the job);
 *   --conns K        number of non-empty connections to record before exiting;
 *   --delay-ms D     sleep D ms after every read of at most 64 KB (a slow printer).
 * A connection is read until the peer closes its side (EOF), then closed: the sender sees an
 * orderly close; with --close-after, unread data makes close() send RST. MIT License. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
  long close_after = -1, delay_ms = 0;
  int conns = 1, ai = 1;
  while (ai + 1 < argc && !strncmp(argv[ai], "--", 2)) {
    if (!strcmp(argv[ai], "--close-after"))
      close_after = atol(argv[ai + 1]);
    else if (!strcmp(argv[ai], "--conns"))
      conns = atoi(argv[ai + 1]);
    else if (!strcmp(argv[ai], "--delay-ms"))
      delay_ms = atol(argv[ai + 1]);
    else
      break;
    ai += 2;
  }
  if (argc - ai != 2 || conns < 1) {
    fprintf(stderr, "usage: mock-sink [--close-after N] [--conns K] [--delay-ms D] PORT OUTFILE\n");
    return 2;
  }
  int port = atoi(argv[ai]);
  const char *out = argv[ai + 1];
  signal(SIGPIPE, SIG_IGN);

  int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(ls, (struct sockaddr *)&a, sizeof(a)) || listen(ls, 4)) {
    perror("mock-sink: bind/listen");
    return 1;
  }
  fprintf(stderr, "mock-sink: listening on 127.0.0.1:%d\n", port);

  int saved = 0;
  while (saved < conns) {
    int c = accept(ls, NULL, NULL);
    if (c < 0)
      continue;
    char name[1024];
    if (saved == 0)
      snprintf(name, sizeof(name), "%s", out);
    else
      snprintf(name, sizeof(name), "%s.%d", out, saved + 1);
    FILE *f = fopen(name, "wb");
    if (!f) {
      perror(name);
      return 1;
    }
    long limit = saved == 0 ? close_after : -1, total = 0;
    char buf[65536];
    for (;;) {
      size_t want = sizeof(buf);
      if (limit >= 0 && (long)want > limit - total)
        want = (size_t)(limit - total);
      if (want == 0)
        break;
      ssize_t n = read(c, buf, want);
      if (n <= 0)
        break;
      fwrite(buf, 1, (size_t)n, f);
      total += n;
      if (delay_ms > 0)
        usleep((useconds_t)(delay_ms * 1000));
    }
    fclose(f);
    close(c);
    fprintf(stderr, "mock-sink: connection %d closed after %ld bytes%s\n", saved + 1, total,
            limit >= 0 && total >= limit ? " (--close-after)" : "");
    if (total > 0)
      saved++;
  }
  close(ls);
  return 0;
}
