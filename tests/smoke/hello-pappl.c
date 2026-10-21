#include <pappl/pappl.h>

static pappl_system_t *
system_cb(int num_options, cups_option_t *options, void *data)
{
  const char *p = cupsGetOption("server-port", num_options, options);
  pappl_system_t *s = papplSystemCreate(PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE, "hello-pappl",
      p ? atoi(p) : 8010, NULL, NULL, NULL, PAPPL_LOGLEVEL_INFO, NULL, false);
  (void)data;
  if (s)
  {
    papplSystemAddListeners(s, NULL);
    papplSystemSetHostName(s, NULL);
    papplSystemSetFooterHTML(s, "hello-pappl smoke test");
  }
  return s;
}

int main(int argc, char *argv[])
{
  return papplMainloop(argc, argv, "1.0", NULL, 0, NULL, NULL, NULL, NULL, NULL, system_cb, NULL, NULL);
}
