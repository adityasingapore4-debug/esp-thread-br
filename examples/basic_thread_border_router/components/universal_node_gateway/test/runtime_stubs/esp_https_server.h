#pragma once
/* HOST_UNIT substitutions only. Deliberately no transport override API. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#include "esp_err.h"
#ifndef ESP_ERR_NO_MEM
#define ESP_ERR_NO_MEM 0x101
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif
#define HTTP_POST 3
#define HTTPD_SSL_TRANSPORT_SECURE 1
#define HTTPD_SOCK_ERR_TIMEOUT -3
typedef void *httpd_handle_t;
typedef void (*httpd_free_ctx_fn_t)(void *);
typedef struct {httpd_handle_t handle;size_t content_len;int test_fd;} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);} httpd_uri_t;
typedef struct {
 esp_err_t (*open_fn)(httpd_handle_t,int);
 void (*close_fn)(httpd_handle_t,int);
 void *global_transport_ctx;
 void (*global_transport_ctx_free_fn)(void *);
 size_t max_open_sockets,max_req_hdr_len,stack_size;
 unsigned recv_wait_timeout,send_wait_timeout;
} httpd_config_t;
typedef struct {
 httpd_config_t httpd;
 int transport_mode;
 uint16_t port_secure;
 uint32_t tls_handshake_timeout_ms;
 const uint8_t *servercert,*prvtkey_pem;
 size_t servercert_len,prvtkey_len;
} httpd_ssl_config_t;
#define HTTPD_SSL_CONFIG_DEFAULT() { .transport_mode=HTTPD_SSL_TRANSPORT_SECURE }
esp_err_t httpd_ssl_start(httpd_handle_t *,httpd_ssl_config_t *);
esp_err_t httpd_ssl_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
void *httpd_sess_get_ctx(httpd_handle_t,int);
void httpd_sess_set_ctx(httpd_handle_t,int,void *,httpd_free_ctx_fn_t);
esp_err_t httpd_sess_trigger_close(httpd_handle_t,int);
int httpd_req_to_sockfd(httpd_req_t *);
int httpd_req_recv(httpd_req_t *,char *,size_t);
int httpd_socket_send(httpd_handle_t,int,const char *,size_t,int);
esp_err_t httpd_queue_work(httpd_handle_t,void (*)(void *),void *);
