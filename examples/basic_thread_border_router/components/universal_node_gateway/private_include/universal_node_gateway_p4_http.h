#pragma once
#include "universal_node_gateway_p4_auth.h"
#define P4_HTTP_HEADER_MAX 4096u
#define P4_HTTP_BODY_MAX 532u
#define P4_HTTP_REQUEST_MAX (P4_HTTP_HEADER_MAX + P4_HTTP_BODY_MAX)
typedef struct {
    gateway_p4_auth_client_t client;
    uint64_t starts[4];
    unsigned start_count;
} gateway_p4_http_connection_t;
typedef struct {
    size_t header_length, body_length;
    char authorization[51];
    bool has_authorization;
} gateway_p4_http_request_t;
typedef struct {
    int status;
    uint8_t body[P4_HTTP_BODY_MAX];
    size_t body_length;
    p4_access_token_t issued;
    char token[44];
} gateway_p4_http_response_t;
/* Serialized by dedicated server owner. Global admission survives connection churn. */
void gateway_p4_http_reset(void);
bool gateway_p4_http_open(gateway_p4_http_connection_t *connection);
void gateway_p4_http_close(gateway_p4_http_connection_t *connection);
/* Raw original header bytes, before HTTP library normalization. 200 means valid
 * headers, NOT authenticated. length must end exactly at CRLF CRLF. */
int gateway_p4_http_headers(const uint8_t *raw,size_t length,gateway_p4_http_request_t *request);
void gateway_p4_http_auth(gateway_p4_http_connection_t *connection,
    const gateway_p4_http_request_t *request,const uint8_t *body,size_t length,
    uint64_t now_ms,gateway_p4_http_response_t *response);
/* Future protected-operation seam; grants no product dispatch. */
int gateway_p4_http_validate(const gateway_p4_http_request_t *request,uint64_t now_ms);
bool gateway_p4_http_token_decode(const char *text,size_t length,p4_access_token_t *token);
void gateway_p4_http_token_encode(const p4_access_token_t *token,char text[44]);
/* Complete bounded HTTP/1.1 response, including empty failures and no-store. */
size_t gateway_p4_http_serialize(const gateway_p4_http_response_t *response,uint8_t *out,size_t capacity);
void gateway_p4_http_delivery(gateway_p4_http_response_t *response,bool delivered);

/* After raw admission, conceal bearer bytes from generic HTTP parser debug
 * logging. The validated request retains the owner-local original value. */
void gateway_p4_http_hide_authorization(uint8_t *raw,size_t header_length);
