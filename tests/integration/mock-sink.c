/* Mock raw-9100 printer: accepts TCP connections on 127.0.0.1:PORT, saves the first non-empty
 * connection to OUTFILE and exits. --close-after N closes the socket after N bytes (simulates a
 * printer dropping the connection mid-job). MIT License. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
  long close_after = -1;
  int ai = 1;
  if (argc > 2 && !strcmp(argv[1], "--close-after")) {
    close_after = atol(argv[2]);
    ai = 3;
  }
  if (argc - ai != 2) {
    fprintf(stderr, "usage: mock-sink [--close-after N] PORT OUTFILE\n");
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

  for (;;) {
    int c = accept(ls, NULL, NULL);
    if (c < 0)
      continue;
    FILE *f = fopen(out, "wb");
    if (!f) {
      perror(out);
      return 1;
    }
    long total = 0;
    char buf[65536];
    for (;;) {
      size_t want = sizeof(buf);
      if (close_after >= 0 && (long)want > close_after - total)
        want = (size_t)(close_after - total);
      if (want == 0)
        break;
      ssize_t n = read(c, buf, want);
      if (n <= 0)
        break;
      fwrite(buf, 1, (size_t)n, f);
      total += n;
    }
    fclose(f);
    close(c); /* with unread data pending this sends RST: sender gets EPIPE/ECONNRESET */
    fprintf(stderr, "mock-sink: connection closed after %ld bytes%s\n", total,
            close_after >= 0 && total >= close_after ? " (--close-after)" : "");
    if (total > 0)
      break;
  }
  close(ls);
  return 0;
}
