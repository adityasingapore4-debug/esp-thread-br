#include "universal_node_gateway_p4_https.h"
#include "universal_node_gateway_p4_http.h"
#include "p4_https_identity.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(P4_HTTPS_HOST_TEST) && defined(ESP_PLATFORM)
#error "Host mock admission is forbidden in ESP-IDF builds"
#endif

typedef struct {
    gateway_p4_http_connection_t connection;
    gateway_p4_http_request_t request;
    int fd;
    uint64_t request_deadline;
    uint64_t pending_deadline;
    bool admitted;
} connection_t;

static _Atomic(httpd_handle_t) owner;
static atomic_bool accepting;
static connection_t *connections[4];
static esp_timer_handle_t deadline_timer;
static atomic_bool tick_queued;
static StaticSemaphore_t owner_lock_storage;
static SemaphoreHandle_t owner_lock;

static void wipe(void *pointer, size_t length)
{
    volatile uint8_t *bytes = pointer;
    while (length--) *bytes++ = 0;
}

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000;
}

static void destroy(void *pointer)
{
    connection_t *connection = pointer;
    for (size_t i = 0; i < 4; ++i) {
        if (connections[i] == connection) connections[i] = NULL;
    }
    gateway_p4_http_close(&connection->connection);
    wipe(connection, sizeof(*connection));
    free(connection);
}

static bool send_all(httpd_handle_t handle, int fd, const uint8_t *data, size_t length)
{
    while (length) {
        int sent = httpd_socket_send(handle, fd, (const char *)data, length, 0);
        if (sent <= 0 || (size_t)sent > length) return false;
        data += sent;
        length -= (size_t)sent;
    }
    return true;
}

static esp_err_t handler(httpd_req_t *request)
{
    int fd = httpd_req_to_sockfd(request);
    connection_t *connection = httpd_sess_get_ctx(request->handle, fd);
    gateway_p4_http_response_t response = {.status = 503};
    uint8_t body[P4_HTTP_BODY_MAX] = {0};
    uint8_t wire[1024] = {0};
    size_t received = 0;
    bool keep = false;

    /* No first-value header API can manufacture raw-header admission. Until a
     * supported pre-normalization boundary exists, product start is disabled
     * and admitted remains false. Host tests inject admission into their own
     * substituted session context; there is no product setter or flag. */
    if (!connection || !connection->admitted || !accepting || owner != request->handle) goto reply;
    if (request->content_len > sizeof(body)) {
        response.status = 413;
        goto reply;
    }
    if (request->content_len != connection->request.body_length) {
        response.status = 400;
        goto reply;
    }
    while (received < request->content_len) {
        if (now_ms() >= connection->request_deadline) {
            response.status = 400;
            goto reply;
        }
        int count = httpd_req_recv(request, (char *)body + received, request->content_len - received);
        if (count <= 0 || (size_t)count > request->content_len - received) {
            response.status = 400;
            goto reply;
        }
        received += (size_t)count;
    }
    uint64_t now = now_ms();
    if (now >= connection->request_deadline) {
        response.status = 400;
        goto reply;
    }
    bool proof = received && body[0] == 0x3f;
    uint64_t proof_deadline = connection->pending_deadline;
    if (proof && proof_deadline && now >= proof_deadline) {
        response.status = 403;
        goto reply;
    }
    gateway_p4_http_auth(&connection->connection, &connection->request, body, received, now, &response);
    uint64_t finished = now_ms();
    if (response.status == 200 &&
        (finished >= connection->request_deadline || (proof && proof_deadline && finished >= proof_deadline))) {
        gateway_p4_http_delivery(&response, false);
        response.status = 403;
    }
    if (response.status == 200) {
        /* Delivery guard mirrors, and never extends, ADR-073's canonical
         * deadline. ADR-073 still verifies the actual transaction itself. */
        connection->pending_deadline = proof ? 0 : now + ADR073_AUTH_TRANSACTION_TIMEOUT_MS;
    }
reply:
    if (connection && response.status != 200) {
        gateway_p4_auth_abort(connection->connection.client);
        connection->pending_deadline = 0;
    }
    size_t length = gateway_p4_http_serialize(&response, wire, sizeof(wire));
    bool delivered = length && send_all(request->handle, fd, wire, length);
    keep = delivered && response.status == 200;
    gateway_p4_http_delivery(&response, delivered);
    if (connection) {
        if (!keep) {
            gateway_p4_auth_abort(connection->connection.client);
            connection->pending_deadline = 0;
        }
        connection->admitted = false;
        wipe(&connection->request, sizeof(connection->request));
        connection->request_deadline = now_ms() + 10000;
    }
    wipe(body, sizeof(body));
    wipe(wire, sizeof(wire));
    return keep ? ESP_OK : ESP_FAIL;
}

static esp_err_t opened(httpd_handle_t handle, int fd)
{
    /* HTTPS has already established TLS. Never replace its send, receive,
     * pending, transport-context, or close ownership. IDF ignores this user
     * callback's return, so allocation failure also queues session closure. */
    if (!accepting) {
        (void)httpd_sess_trigger_close(handle, fd);
        return ESP_FAIL;
    }
    size_t slot = 0;
    while (slot < 4 && connections[slot]) ++slot;
    connection_t *connection = slot < 4 ? calloc(1, sizeof(*connection)) : NULL;
    if (!connection || !gateway_p4_http_open(&connection->connection)) {
        free(connection);
        (void)httpd_sess_trigger_close(handle, fd);
        return ESP_ERR_NO_MEM;
    }
    connection->fd = fd;
    connection->request_deadline = now_ms() + 10000;
    connections[slot] = connection;
    httpd_sess_set_ctx(handle, fd, connection, destroy);
    return ESP_OK;
}

static void expire_connections(void *argument)
{
    httpd_handle_t handle = argument;
    atomic_store(&tick_queued, false);
    if (handle != owner) return;
    uint64_t now = now_ms();
    for (size_t i = 0; i < 4; ++i) {
        connection_t *connection = connections[i];
        if (connection &&
            (now >= connection->request_deadline ||
             (connection->pending_deadline && now >= connection->pending_deadline)) &&
            httpd_sess_get_ctx(handle, connection->fd) == connection) {
            gateway_p4_auth_abort(connection->connection.client);
            (void)httpd_sess_trigger_close(handle, connection->fd);
        }
    }
}

static void tick(void *argument)
{
    (void)argument;
    xSemaphoreTake(owner_lock, portMAX_DELAY);
    if (owner && !atomic_exchange(&tick_queued, true)) {
        if (httpd_queue_work(owner, expire_connections, owner) != ESP_OK) atomic_store(&tick_queued, false);
    }
    xSemaphoreGive(owner_lock);
}

static bool raw_admission_available(void)
{
#ifdef P4_HTTPS_HOST_TEST
    extern bool p4_https_test_raw_admission_available(void);
    return p4_https_test_raw_admission_available();
#else
    /* ADR-089 requires original header occurrences. IDF 5.5.4's public request
     * lookups expose first values only. Receive interception is prohibited.
     * Fail before loading any private key or creating a listener. */
    return false;
#endif
}

esp_err_t gateway_p4_https_start(void)
{
#if CONFIG_UN_GATEWAY_DEV_P4_TLS_SMOKE
    /* No P4 auth routes or admission authority are reachable in this
     * isolated development handshake probe. Product mode stays fail-closed. */
    const bool tls_only = true;
#else
    const bool tls_only = false;
#endif
    if (!tls_only && !raw_admission_available()) return ESP_ERR_NOT_SUPPORTED;
    if (!owner_lock) owner_lock = xSemaphoreCreateMutexStatic(&owner_lock_storage);
    if (!owner_lock || owner || deadline_timer) return ESP_ERR_INVALID_STATE;
    httpd_ssl_config_t config;
    if (!p4_https_identity_prepare(&config)) return ESP_ERR_INVALID_STATE;
    config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
    config.tls_handshake_timeout_ms = 10000;
    config.httpd.open_fn = tls_only ? NULL : opened;
    if (tls_only) config.port_secure = 8443;
    config.httpd.max_open_sockets = 4;
    config.httpd.max_req_hdr_len = P4_HTTP_HEADER_MAX;
    config.httpd.recv_wait_timeout = 10;
    config.httpd.send_wait_timeout = 10;
    config.httpd.stack_size = 16384;
    httpd_handle_t handle = NULL;
    accepting = false;
    esp_err_t result = httpd_ssl_start(&handle, &config);
    if (result != ESP_OK) {
        accepting = false;
        p4_https_identity_release();
        return result;
    }
    owner = handle;
    if (tls_only) {
        /* Only TLS establishment is being qualified. No handlers means no
         * authentication, token issuance, bearer or product operations. */
        return ESP_OK;
    }
    httpd_uri_t route = {.uri = "/un/p4/auth", .method = HTTP_POST, .handler = handler};
    result = httpd_register_uri_handler(handle, &route);
    if (result == ESP_OK) {
        esp_timer_create_args_t arguments = {.callback = tick, .name = "p4_deadline"};
        result = esp_timer_create(&arguments, &deadline_timer);
        if (result == ESP_OK) result = esp_timer_start_periodic(deadline_timer, 100000);
    }
    if (result != ESP_OK) (void)gateway_p4_https_stop();
    else accepting = true;
    return result;
}

esp_err_t gateway_p4_https_stop(void)
{
    if (!owner_lock || !owner) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(owner_lock, portMAX_DELAY);
    httpd_handle_t handle = owner;
    accepting = false;
    owner = NULL;
    xSemaphoreGive(owner_lock);
    if (deadline_timer) {
        (void)esp_timer_stop(deadline_timer);
        (void)esp_timer_delete(deadline_timer);
        deadline_timer = NULL;
    }
    esp_err_t result = httpd_ssl_stop(handle);
    if (result == ESP_OK) {
        atomic_store(&tick_queued, false);
        p4_https_identity_release();
    } else {
        /* A live TLS consumer can still require its copied material. Keep
         * identity ownership and the handle; reject new auth until stop retry. */
        owner = handle;
    }
    return result;
}
