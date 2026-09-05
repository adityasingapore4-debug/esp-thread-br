/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start border router web server, which provides REST APIs and GUI
 *
 * @param[in] base_path is the virtual file path of web server
 */
void esp_br_web_start(char *base_path);

/* Register a bounded application-owned URI before the BR web server starts.
 * The BR server retains lifecycle and socket ownership. */
esp_err_t esp_br_web_register_uri_handler(const httpd_uri_t *uri);

#ifdef __cplusplus
}
#endif
