#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the bounded M10 HTTP carrier before the BR web server starts. */
esp_err_t universal_node_gateway_http_transport_init(void);

#ifdef __cplusplus
}
#endif
