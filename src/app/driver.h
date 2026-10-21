/* ricoh-sp200 PAPPL driver. MIT License. */
#ifndef RICOH_DRIVER_H
#define RICOH_DRIVER_H

#include <pappl/pappl.h>

#define RICOH_DRIVER_NAME "ricoh-sp200"

extern pappl_pr_driver_t ricoh_drivers[];
extern const int         ricoh_num_drivers;

bool        ricoh_driver_cb(pappl_system_t *system, const char *driver_name, const char *device_uri,
                            const char *device_id, pappl_pr_driver_data_t *data, ipp_t **attrs, void *cbdata);
const char *ricoh_autoadd_cb(const char *device_info, const char *device_uri, const char *device_id, void *data);

#endif
