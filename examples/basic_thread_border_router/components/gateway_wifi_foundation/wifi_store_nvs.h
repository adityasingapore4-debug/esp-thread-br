#ifndef GATEWAY_WIFI_STORE_NVS_H
#define GATEWAY_WIFI_STORE_NVS_H
#include "wifi_store.h"
#include "nvs.h"
typedef struct { nvs_handle_t handle; bool opened, writable, uncertain; } wifi_nvs_adapter_t;
/* Caller owns nvs_flash_init lifecycle. This adapter never initializes, erases,
 * deinitializes a shared partition, provisions keys or registers a service.
 * Writes are locked off unless explicitly compiled as an isolated synthetic
 * NVS test with WIFI_NVS_ISOLATED_SYNTHETIC_TEST. Never set that for production. */
wifi_store_io_t wifi_nvs_boundary(wifi_nvs_adapter_t *);
void wifi_nvs_close(wifi_nvs_adapter_t *);
#endif
