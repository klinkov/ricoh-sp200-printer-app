/* ricoh-socket:// device scheme: raw 9100 TCP with timeouts and per-job connections. MIT License. */
#ifndef RICOH_DEVICE_H
#define RICOH_DEVICE_H

#include <pappl/pappl.h>

#define RICOH_SOCKET_SCHEME "ricoh-socket"
#define RICOH_SOCKET_PREFIX RICOH_SOCKET_SCHEME "://"

/* Register ricoh-socket://host[:port] (default port 9100). Call once before papplMainloop. */
void ricoh_socket_register(void);

/* End of a print job on a ricoh-socket device (the driver calls it after papplDeviceFlush):
 * half-close, wait up to 10 s for the printer to close its side, close the connection. The next
 * write reconnects. Returns false if the connection failed at any point during the job (then
 * papplDeviceGetStatus also reports PAPPL_PREASON_OTHER until the next successful reconnect). */
bool ricoh_socket_end_job(pappl_device_t *device);

#endif
