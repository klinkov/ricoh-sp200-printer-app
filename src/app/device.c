/* ricoh-socket:// device scheme. MIT License.
 *
 * Why not PAPPL's socket://: (1) its write has no timeout, (2) PAPPL keeps the device open while
 * jobs are queued, so after the printer drops the connection every following job fails on the same
 * dead socket, (3) the last <= 8 KB go out in papplDeviceFlush(), whose result nobody sees.
 * Here: open = connect with a 10 s timeout (failure -> PAPPL pauses the queue and retries every
 * 5 s); write = write everything, poll 30 s per stall, a failure marks the socket dead; the driver
 * ends every job with ricoh_socket_end_job() (half-close + wait for the printer to close, which
 * also catches a reset after the tail was buffered) and checks papplDeviceGetStatus(); the next
 * job's first write reconnects once. */
#include "device.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define RS_DEFAULT_PORT     9100
#define RS_CONNECT_TIMEOUT  10000 /* ms */
#define RS_WRITE_TIMEOUT    30000 /* ms without progress */
#define RS_EOF_TIMEOUT      10000 /* ms to wait for the printer to close after our half-close */

typedef struct {
  int  fd;   /* -1: not connected (before the first write of the next job) */
  bool dead; /* an I/O error happened; cleared by a successful reconnect */
  char host[256];
  int  port;
} rsock_t;

static long long now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* poll one fd, restarting on EINTR with the remaining time; returns poll()'s result. */
static int poll_fd(int fd, short events, int timeout_ms, short *revents) {
  long long end = now_ms() + timeout_ms;
  for (;;) {
    struct pollfd p = {fd, events, 0};
    long long left = end - now_ms();
    int n = poll(&p, 1, left > 0 ? (int)left : 0);
    if (n < 0 && errno == EINTR)
      continue;
    *revents = p.revents;
    return n;
  }
}

static bool rs_connect(pappl_device_t *device, rsock_t *s) {
  char port[16];
  struct addrinfo hints = {0}, *res = NULL;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port, sizeof(port), "%d", s->port);
  int rc = getaddrinfo(s->host, port, &hints, &res);
  if (rc) {
    papplDeviceError(device, "Unable to resolve '%s': %s", s->host, gai_strerror(rc));
    return false;
  }
  int err = 0;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
      err = errno;
      continue;
    }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0 || errno == EINPROGRESS || errno == EINTR) {
      short rev = 0;
      int n = poll_fd(fd, POLLOUT, RS_CONNECT_TIMEOUT, &rev);
      socklen_t len = sizeof(err);
      err = 0;
      if (n == 0)
        err = ETIMEDOUT;
      else if (n < 0)
        err = errno;
      else if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len))
        err = errno;
      if (!err) {
        freeaddrinfo(res);
        s->fd = fd;
        s->dead = false;
        return true;
      }
    } else {
      err = errno;
    }
    close(fd);
  }
  freeaddrinfo(res);
  papplDeviceError(device, "Unable to connect to %s:%d: %s", s->host, s->port, strerror(err ? err : ECONNREFUSED));
  return false;
}

static bool rs_open(pappl_device_t *device, const char *device_uri, const char *name) {
  char scheme[32], userpass[64], resource[256];
  rsock_t *s = calloc(1, sizeof(*s));
  (void)name;
  if (!s) {
    papplDeviceError(device, "Out of memory.");
    return false;
  }
  s->fd = -1;
  if (httpSeparateURI(HTTP_URI_CODING_ALL, device_uri, scheme, sizeof(scheme), userpass, sizeof(userpass), s->host,
                      sizeof(s->host), &s->port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK ||
      !s->host[0]) {
    papplDeviceError(device, "Bad device URI '%s'.", device_uri);
    free(s);
    return false;
  }
  if (s->port <= 0)
    s->port = RS_DEFAULT_PORT;
  if (!rs_connect(device, s)) {
    free(s);
    return false;
  }
  papplDeviceSetData(device, s);
  return true;
}

static void rs_close_fd(rsock_t *s) {
  if (s->fd >= 0)
    close(s->fd);
  s->fd = -1;
}

static void rs_close(pappl_device_t *device) {
  rsock_t *s = papplDeviceGetData(device);
  if (!s)
    return;
  rs_close_fd(s);
  free(s);
  papplDeviceSetData(device, NULL);
}

static ssize_t rs_write(pappl_device_t *device, const void *buffer, size_t bytes) {
  rsock_t *s = papplDeviceGetData(device);
  if (!s)
    return -1;
  if (s->fd < 0) { /* first write of a job after ricoh_socket_end_job: one reconnect attempt */
    if (!rs_connect(device, s)) {
      s->dead = true;
      return -1;
    }
  } else if (s->dead) {
    return -1; /* rest of a failed job: drop it, never send it on a new connection */
  }
  const unsigned char *p = buffer;
  size_t left = bytes;
  while (left > 0) {
    ssize_t n = write(s->fd, p, left);
    if (n > 0) {
      p += n;
      left -= (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      short rev = 0;
      int r = poll_fd(s->fd, POLLOUT, RS_WRITE_TIMEOUT, &rev);
      if (r > 0)
        continue;
      papplDeviceError(device, "Printer %s:%d stopped accepting data (%s).", s->host, s->port,
                       r == 0 ? "timeout" : strerror(errno));
    } else {
      papplDeviceError(device, "Unable to write to printer %s:%d: %s", s->host, s->port,
                       n < 0 ? strerror(errno) : "connection closed");
    }
    s->dead = true;
    return -1;
  }
  return (ssize_t)bytes;
}

static ssize_t rs_read(pappl_device_t *device, void *buffer, size_t bytes) {
  rsock_t *s = papplDeviceGetData(device);
  short rev = 0;
  if (!s || s->fd < 0 || s->dead)
    return -1;
  if (poll_fd(s->fd, POLLIN, 1000, &rev) <= 0)
    return 0;
  ssize_t n = read(s->fd, buffer, bytes);
  if (n < 0 && (errno == EAGAIN || errno == EINTR))
    return 0;
  return n;
}

static pappl_preason_t rs_status(pappl_device_t *device) {
  rsock_t *s = papplDeviceGetData(device);
  return !s || s->dead ? PAPPL_PREASON_OTHER : PAPPL_PREASON_NONE;
}

bool ricoh_socket_end_job(pappl_device_t *device) {
  rsock_t *s = papplDeviceGetData(device);
  if (!s)
    return false;
  if (s->fd < 0 || s->dead) {
    rs_close_fd(s);
    return !s->dead;
  }
  /* Half-close and read (discarding any back-channel data) until the printer closes its side: a
   * reset here means it dropped data that write() had already accepted. */
  shutdown(s->fd, SHUT_WR);
  long long end = now_ms() + RS_EOF_TIMEOUT;
  char buf[1024];
  for (;;) {
    long long left = end - now_ms();
    short rev = 0;
    if (left <= 0)
      break; /* printer keeps the connection open: everything was accepted, treat as success */
    int r = poll_fd(s->fd, POLLIN, (int)left, &rev);
    if (r == 0)
      break;
    if (r < 0) {
      papplDeviceError(device, "poll failed: %s", strerror(errno));
      s->dead = true;
      break;
    }
    ssize_t n = read(s->fd, buf, sizeof(buf));
    if (n > 0)
      continue;
    if (n == 0)
      break; /* orderly close */
    if (errno == EINTR || errno == EAGAIN)
      continue;
    papplDeviceError(device, "Printer %s:%d reset the connection at the end of the job: %s", s->host, s->port,
                     strerror(errno));
    s->dead = true;
    break;
  }
  rs_close_fd(s);
  return !s->dead;
}

void ricoh_socket_register(void) {
  papplDeviceAddScheme(RICOH_SOCKET_SCHEME, PAPPL_DEVTYPE_CUSTOM_NETWORK, /*list_cb*/ NULL, rs_open, rs_close, rs_read,
                       rs_write, rs_status, /*id_cb*/ NULL);
}
