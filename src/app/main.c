/* ricoh-sp200-printer-app: PAPPL printer application for RICOH SP 200 series. MIT License. */
#include "driver.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME    "ricoh-sp200-printer-app"
#define APP_VERSION "0.1.0"
#define APP_FOOTER  "RICOH SP 200 series printer application " APP_VERSION " (MIT License)"
#define APP_PORT    8000

static pappl_loglevel_t parse_loglevel(const char *v) {
  if (!v)
    return PAPPL_LOGLEVEL_INFO;
  if (!strcmp(v, "debug"))
    return PAPPL_LOGLEVEL_DEBUG;
  if (!strcmp(v, "warn"))
    return PAPPL_LOGLEVEL_WARN;
  if (!strcmp(v, "error"))
    return PAPPL_LOGLEVEL_ERROR;
  if (!strcmp(v, "fatal"))
    return PAPPL_LOGLEVEL_FATAL;
  return PAPPL_LOGLEVEL_INFO;
}

/* PAPPL 1.4.9: papplMainloop does not open a TCP listener itself, and the web UI crashes
 * without a footer (docs/pappl-build.md) - both handled here. */
static pappl_system_t *system_cb(int num_options, cups_option_t *options, void *data) {
  const char *port = cupsGetOption("server-port", num_options, options);
  const char *logfile = cupsGetOption("log-file", num_options, options);
  const char *loglevel = cupsGetOption("log-level", num_options, options);
  const char *spool = cupsGetOption("spool-directory", num_options, options);
  (void)data;

  pappl_system_t *s = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE | PAPPL_SOPTIONS_WEB_LOG,
                                        APP_NAME, port ? atoi(port) : APP_PORT, "_print,_universal", spool,
                                        logfile ? logfile : "-", parse_loglevel(loglevel), NULL, false);
  if (!s)
    return NULL;
  papplSystemAddListeners(s, cupsGetOption("listen-hostname", num_options, options));
  papplSystemSetHostName(s, NULL);
  papplSystemSetFooterHTML(s, APP_FOOTER);
  papplSystemSetPrinterDrivers(s, ricoh_num_drivers, ricoh_drivers, ricoh_autoadd_cb, NULL, ricoh_driver_cb, NULL);
  return s;
}

int main(int argc, char *argv[]) {
  /* A printer that drops the socket must fail the write, not kill the service. */
  signal(SIGPIPE, SIG_IGN);
  return papplMainloop(argc, argv, APP_VERSION, APP_FOOTER, ricoh_num_drivers, ricoh_drivers, ricoh_autoadd_cb,
                       ricoh_driver_cb, NULL, NULL, system_cb, NULL, NULL);
}
