#pragma once
#include <stdbool.h>
#include "esp_https_server.h"
/* Dedicated P4 owner only. All APIs and canonical services must be serialized;
 * borrowed authentication/session services must outlive the server. No product
 * startup caller exists. Start requires the protected P4 identity provider.
 * Stop completes TLS shutdown before releasing private-key owner buffers.
 * Product start currently returns ESP_ERR_NOT_SUPPORTED: original-header
 * admission needs a supported integration that preserves HTTPS callbacks. */
esp_err_t gateway_p4_https_start(void);
esp_err_t gateway_p4_https_stop(void);
