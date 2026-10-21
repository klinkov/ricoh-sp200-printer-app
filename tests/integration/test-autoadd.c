/* Unit test for ricoh_autoadd_cb (driver auto-selection by IEEE-1284 device ID / device info).
 * Prints PASS:/FAIL: lines (counted by run.sh); exit status 1 on any failure. MIT License. */
#include "../../src/app/driver.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *what, const char *info, const char *id, const char *want) {
  const char *got = ricoh_autoadd_cb(info, "ricoh-socket://192.0.2.1", id, NULL);
  bool ok = want ? got && !strcmp(got, want) : got == NULL;
  printf("%s: autoadd: %s -> %s\n", ok ? "PASS" : "FAIL", what, got ? got : "NULL");
  if (!ok)
    failures++;
}

int main(void) {
  expect("real SP 202SN device-id", NULL, "MFG:RICOH ;CMD:GDI;MDL:SP 202SN DDST;CLS:PRINTER;", RICOH_DRIVER_NAME);
  expect("MANUFACTURER/MODEL keys", NULL, "MANUFACTURER:Ricoh;MODEL:SP 204SFN", RICOH_DRIVER_NAME);
  expect("other Ricoh model", "RICOH SP 202SN", "MFG:RICOH;MDL:MP C3003", NULL);
  expect("other manufacturer", NULL, "MFG:HP;MDL:LaserJet SP 200;", NULL);
  expect("no device-id, info fallback", "RICOH SP 202SN DDST", "", RICOH_DRIVER_NAME);
  expect("no device-id, foreign info", "Brother HL-L2350DW", NULL, NULL);
  return failures ? 1 : 0;
}
