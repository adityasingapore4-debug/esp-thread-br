#include "universal_node_gateway_http_transport.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_br_web.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "control_frame_parse.h"
#include "device_event_delivery.h"
#include "universal_node_gateway.h"
#include "universal_node_gateway_envelope.h"
#include "universal_node_gateway_rx.h"
#include "mesh_session_control.h"

#define HTTP_GATEWAY_PATH "/un/gateway"
#define HTTP_GATEWAY_EVENTS_PATH "/un/gateway/events"
#define HTTP_GATEWAY_DIAGNOSTICS_PATH "/un/gateway/diagnostics"
#define HTTP_GATEWAY_SESSION_START_PATH "/un/gateway/session/start"
#define HTTP_GATEWAY_CONTENT_TYPE "application/octet-stream"
#define HTTP_GATEWAY_ENVELOPE_MAX_LENGTH \
    (UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE + UNIVERSAL_NODE_GATEWAY_ENVELOPE_MAX_PAYLOAD)
#define HTTP_GATEWAY_QUEUE_CAPACITY 8u

static const char *TAG = "un_gateway_http";

static esp_err_t gateway_session_start_handler(httpd_req_t *req)
{
    char query[32], node_text[16], *end = NULL;
    unsigned long node;
    if (httpd_req_get_url_query_len(req) == 0u ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "target_node_id", node_text, sizeof(node_text)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "target_node_id required");
    }
    node = strtoul(node_text, &end, 0);
    if (end == node_text || *end != '\0' || node == 0u || node >= 0xffffu) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid target_node_id");
    }
    const bool start_accepted = mesh_session_control_start((uint16_t)node);
    ESP_LOGI(TAG, "GATEWAY_SESSION_START target=0x%04X result=%s",
             (unsigned)node, start_accepted ? "true" : "false");
    if (!start_accepted) {
        ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, "503 Service Unavailable"), TAG,
                            "set session-start failure status failed");
        return httpd_resp_send(req, "session start not admitted", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, "", 0);
}

typedef struct {
    uint16_t length;
    uint8_t bytes[HTTP_GATEWAY_ENVELOPE_MAX_LENGTH];
} http_gateway_queue_entry_t;

static http_gateway_queue_entry_t s_queue[HTTP_GATEWAY_QUEUE_CAPACITY];
static uint8_t s_queue_head;
static uint8_t s_queue_count;
static portMUX_TYPE s_queue_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;

static bool queue_push(universal_node_gateway_envelope_type_t type,
                       const uint8_t *payload, uint16_t payload_length)
{
    http_gateway_queue_entry_t entry;
    uint16_t length;

    if (!universal_node_gateway_envelope_encode(type, payload, payload_length,
                                                entry.bytes, sizeof(entry.bytes), &length)) {
        return false;
    }
    entry.length = length;

    portENTER_CRITICAL(&s_queue_lock);
    if (s_queue_count == HTTP_GATEWAY_QUEUE_CAPACITY) {
        portEXIT_CRITICAL(&s_queue_lock);
        ESP_LOGW(TAG, "egress queue full; dropping newest envelope type=%u", (unsigned)type);
        return false;
    }
    s_queue[(s_queue_head + s_queue_count) % HTTP_GATEWAY_QUEUE_CAPACITY] = entry;
    ++s_queue_count;
    portEXIT_CRITICAL(&s_queue_lock);
    return true;
}

static bool queue_pop(http_gateway_queue_entry_t *out)
{
    if (out == NULL) return false;

    portENTER_CRITICAL(&s_queue_lock);
    if (s_queue_count == 0u) {
        portEXIT_CRITICAL(&s_queue_lock);
        return false;
    }
    *out = s_queue[s_queue_head];
    s_queue_head = (uint8_t)((s_queue_head + 1u) % HTTP_GATEWAY_QUEUE_CAPACITY);
    --s_queue_count;
    portEXIT_CRITICAL(&s_queue_lock);
    return true;
}

static bool content_type_is_binary(httpd_req_t *req)
{
    const size_t length = httpd_req_get_hdr_value_len(req, "Content-Type");
    char value[sizeof(HTTP_GATEWAY_CONTENT_TYPE)];

    if (length != strlen(HTTP_GATEWAY_CONTENT_TYPE) || length >= sizeof(value) ||
        httpd_req_get_hdr_value_str(req, "Content-Type", value, sizeof(value)) != ESP_OK) {
        return false;
    }
    return strcmp(value, HTTP_GATEWAY_CONTENT_TYPE) == 0;
}

static bool receive_body(httpd_req_t *req, uint8_t out[HTTP_GATEWAY_ENVELOPE_MAX_LENGTH])
{
    int received;
    size_t offset = 0u;

    if (req->content_len <= 0 || req->content_len > HTTP_GATEWAY_ENVELOPE_MAX_LENGTH) {
        return false;
    }
    while (offset < (size_t)req->content_len) {
        received = httpd_req_recv(req, (char *)out + offset, req->content_len - offset);
        if (received <= 0) return false;
        offset += (size_t)received;
    }
    return offset == (size_t)req->content_len;
}

static esp_err_t send_empty_status(httpd_req_t *req, const char *status)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, status), TAG, "set HTTP status failed");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t send_status_response(httpd_req_t *req)
{
    universal_node_gateway_status_t status;
    uint8_t payload[UNIVERSAL_NODE_GATEWAY_STATUS_PAYLOAD_SIZE];
    uint8_t frame[HTTP_GATEWAY_ENVELOPE_MAX_LENGTH];
    uint16_t frame_length;

    if (!universal_node_gateway_get_status(&status) ||
        !universal_node_gateway_status_encode(&status, payload) ||
        !universal_node_gateway_envelope_encode(UNIVERSAL_NODE_GATEWAY_ENVELOPE_STATUS,
                                                payload, sizeof(payload), frame,
                                                sizeof(frame), &frame_length)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Gateway status unavailable");
    }
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, HTTP_GATEWAY_CONTENT_TYPE), TAG,
                        "set binary response type failed");
    return httpd_resp_send(req, (const char *)frame, frame_length);
}

static esp_err_t gateway_post_handler(httpd_req_t *req)
{
    uint8_t frame[HTTP_GATEWAY_ENVELOPE_MAX_LENGTH];
    universal_node_gateway_envelope_view_t envelope;
    control_frame_parsed_t control_frame;

    if (!content_type_is_binary(req)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content-Type must be application/octet-stream");
    }
    if (req->content_len > HTTP_GATEWAY_ENVELOPE_MAX_LENGTH) {
        return httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "M10 envelope too large");
    }
    if (!receive_body(req, frame) ||
        !universal_node_gateway_envelope_decode(frame, (uint16_t)req->content_len, &envelope)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid M10 envelope");
    }

    if (envelope.type == UNIVERSAL_NODE_GATEWAY_ENVELOPE_STATUS) {
        if (envelope.payload_length != 0u) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "STATUS request payload must be empty");
        }
        return send_status_response(req);
    }
    if (envelope.type != UNIVERSAL_NODE_GATEWAY_ENVELOPE_UN_REQUEST ||
        !control_frame_parse(envelope.payload, envelope.payload_length, &control_frame)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unsupported Gateway ingress envelope");
    }
    if (universal_node_gateway_submit_frame(control_frame.target, envelope.payload,
                                            envelope.payload_length) != UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED) {
        ESP_RETURN_ON_ERROR(httpd_resp_set_status(req, "503 Service Unavailable"), TAG,
                            "set HTTP status failed");
        return httpd_resp_send(req, "Gateway request not admitted",
                               HTTPD_RESP_USE_STRLEN);
    }
    return send_empty_status(req, "202 Accepted");
}

static esp_err_t gateway_events_get_handler(httpd_req_t *req)
{
    http_gateway_queue_entry_t entry;

    if (!queue_pop(&entry)) return send_empty_status(req, "204 No Content");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, HTTP_GATEWAY_CONTENT_TYPE), TAG,
                        "set binary response type failed");
    return httpd_resp_send(req, (const char *)entry.bytes, entry.length);
}

static bool diagnostics_node_id(httpd_req_t *req, uint16_t *out_node_id)
{
    char query[32];
    char node_id_text[16];
    char *end = NULL;
    unsigned long parsed;
    const size_t query_length = httpd_req_get_url_query_len(req);

    if (out_node_id == NULL || query_length == 0u || query_length >= sizeof(query) ||
        httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "node_id", node_id_text, sizeof(node_id_text)) != ESP_OK ||
        node_id_text[0] == '\0') {
        return false;
    }
    parsed = strtoul(node_id_text, &end, 0);
    if (end == node_id_text || *end != '\0' || parsed == 0u || parsed >= 0xffffu) {
        return false;
    }
    *out_node_id = (uint16_t)parsed;
    return true;
}

static void endpoint_hex(const uint8_t endpoint[THREAD_ENDPOINT_IPV6_SIZE], char out[33])
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < THREAD_ENDPOINT_IPV6_SIZE; ++i) {
        out[i * 2u] = hex[endpoint[i] >> 4u];
        out[i * 2u + 1u] = hex[endpoint[i] & 0x0fu];
    }
    out[32] = '\0';
}

static esp_err_t gateway_diagnostics_get_handler(httpd_req_t *req)
{
    uint16_t node_id;
    universal_node_gateway_diagnostics_t diagnostics;
    char response[256];

    if (!diagnostics_node_id(req, &node_id)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "node_id must be a nonzero unicast integer");
    }
    if (!universal_node_gateway_get_diagnostics(node_id, &diagnostics)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Gateway diagnostics unavailable");
    }
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"), TAG,
                        "set diagnostics content type failed");
    if (diagnostics.endpoint_observed) {
        char endpoint[33];
        endpoint_hex(diagnostics.endpoint_thread_ipv6, endpoint);
        snprintf(response, sizeof(response),
                 "{\"node_id\":%u,\"last_submit_valid\":%s,\"last_submit_result\":%u,"
                 "\"endpoint_observed\":true,\"transport_type\":%u,"
                 "\"resolved_destination_type\":%u,\"thread_ipv6\":\"%s\"}",
                 node_id, diagnostics.last_submit_valid ? "true" : "false",
                 (unsigned)diagnostics.last_submit_result,
                 (unsigned)diagnostics.endpoint_transport,
                 (unsigned)diagnostics.endpoint_type, endpoint);
    } else {
        snprintf(response, sizeof(response),
                 "{\"node_id\":%u,\"last_submit_valid\":%s,\"last_submit_result\":%u,"
                 "\"endpoint_observed\":false,\"transport_type\":null,"
                 "\"resolved_destination_type\":null,\"thread_ipv6\":null}",
                 node_id, diagnostics.last_submit_valid ? "true" : "false",
                 (unsigned)diagnostics.last_submit_result);
    }
    return httpd_resp_sendstr(req, response);
}

static void gateway_response_consumer(const universal_node_gateway_completed_response_t *response,
                                      void *context)
{
    (void)context;
    if (response != NULL) {
        (void)queue_push(UNIVERSAL_NODE_GATEWAY_ENVELOPE_UN_RESPONSE,
                         response->frame, response->frame_len);
    }
}

static void gateway_event_consumer(const universal_node_gateway_rx_event_t *event, void *context)
{
    uint8_t logical[DEVICE_EVENT_DELIVERY_MAX_LOGICAL_LENGTH];
    uint16_t logical_length;

    (void)context;
    if (event == NULL || event->classification != GATEWAY_RX_DEVICE_EVENT ||
        device_event_delivery_encode(&event->device_event, logical, sizeof(logical),
                                     &logical_length) != DEVICE_EVENT_DELIVERY_OK) {
        return;
    }
    (void)queue_push(UNIVERSAL_NODE_GATEWAY_ENVELOPE_DEVICE_EVENT, logical, logical_length);
}

static void gateway_diagnostic_consumer(uint16_t source_node_id,
                                        const uint8_t *payload,
                                        uint16_t payload_length, void *context)
{
    uint8_t diagnostic[UNIVERSAL_NODE_GATEWAY_ENVELOPE_MAX_PAYLOAD];

    (void)context;
    diagnostic[0] = (uint8_t)source_node_id;
    diagnostic[1] = (uint8_t)(source_node_id >> 8u);
    memcpy(diagnostic + UNIVERSAL_NODE_GATEWAY_DIAGNOSTIC_SOURCE_SIZE,
           payload, payload_length);
    (void)queue_push(UNIVERSAL_NODE_GATEWAY_ENVELOPE_DIAGNOSTIC, diagnostic,
                     (uint16_t)(UNIVERSAL_NODE_GATEWAY_DIAGNOSTIC_SOURCE_SIZE +
                                payload_length));
}

static const httpd_uri_t s_gateway_post_uri = {
    .uri = HTTP_GATEWAY_PATH,
    .method = HTTP_POST,
    .handler = gateway_post_handler,
};

static const httpd_uri_t s_gateway_events_uri = {
    .uri = HTTP_GATEWAY_EVENTS_PATH,
    .method = HTTP_GET,
    .handler = gateway_events_get_handler,
};

static const httpd_uri_t s_gateway_diagnostics_uri = {
    .uri = HTTP_GATEWAY_DIAGNOSTICS_PATH,
    .method = HTTP_GET,
    .handler = gateway_diagnostics_get_handler,
};

static const httpd_uri_t s_gateway_session_start_uri = {
    .uri = HTTP_GATEWAY_SESSION_START_PATH,
    .method = HTTP_POST,
    .handler = gateway_session_start_handler,
};

esp_err_t universal_node_gateway_http_transport_init(void)
{
    if (s_initialized) return ESP_OK;
    if (!universal_node_gateway_register_response_consumer(gateway_response_consumer, NULL) ||
        !universal_node_gateway_register_rx_consumer(gateway_event_consumer, NULL) ||
        !universal_node_gateway_register_diagnostic_consumer(gateway_diagnostic_consumer, NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (esp_br_web_register_uri_handler(&s_gateway_post_uri) != ESP_OK ||
        esp_br_web_register_uri_handler(&s_gateway_events_uri) != ESP_OK ||
        esp_br_web_register_uri_handler(&s_gateway_diagnostics_uri) != ESP_OK ||
        esp_br_web_register_uri_handler(&s_gateway_session_start_uri) != ESP_OK) {
        return ESP_FAIL;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "P1 engineering HTTP transport registered");
    return ESP_OK;
}
