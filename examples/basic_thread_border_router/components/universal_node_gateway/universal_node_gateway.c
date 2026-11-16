#include "universal_node_gateway.h"
#include "universal_node_gateway_rx.h"

#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "control_frame_build.h"
#include "control_frame_parse.h"
#include "request_completion.h"
#include "mesh_auth.h"
#include "mesh_session.h"
#include "mesh_session_control.h"
#include "node_identity.h"
#include "manufacturing_identity.h"
#include "security_root.h"
#include "provisioning_v2.h"
#include "join_existing.h"
#include "ble_bridge.h"
#include "ble_key_manager.h"
#include "ble_pairing_policy.h"
#include "authorization.h"
#include "command_router.h"
#include "device_capability.h"
#include "device_event_bus.h"
#include "device_event_distributor.h"
#include "transport.h"
#include "address_resolver.h"
#include "device_metadata.h"
#include "device_event_delivery.h"
#include "node_registry.h"
#include "rts.h"
#include "synchronization_protocol.h"
#include "universal_node_gateway_pending.h"
#include "response_transfer_receive_core.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"

#include <string.h>

static const char *TAG = "un_gateway";

static bool s_initialized;
static bool s_started;
static bool s_provisioning_carrier_started;
static universal_node_gateway_status_error_t s_last_error =
    UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NOT_INITIALIZED;
static bool s_last_submit_valid;
static universal_node_gateway_submit_result_t s_last_submit_result;
static universal_node_gateway_rx_consumer_t s_rx_consumer;
static void *s_rx_consumer_context;
static universal_node_gateway_response_consumer_t s_response_consumer;
static void *s_response_consumer_context;

/* G1B composes canonical local services only.  It does not register a
 * transport ingress, start Thread, or start Registry Sync runtime. */
static bool universal_node_gateway_local_platform_init(void)
{
    if (!device_event_bus_init() || !rts_init()) {
        return false;
    }

    device_capability_init();
    if (!metadata_init() || !device_event_delivery_init() ||
        !device_event_distributor_init() || !node_registry_init()) {
        return false;
    }

    /* Authorization has no mutable initializer.  Its canonical policy must
     * nevertheless fail closed until later credential adoption. */
    if (authorization_is_allowed(NULL, CMD_METADATA_WRITE)) {
        return false;
    }

    /* The canonical router is linked but has no ingress in G1B. */
    bool (*volatile router_link)(uint8_t *, uint16_t, delivery_context_t,
                                 device_command_source_t) =
        command_router_deliver_from_source;
    (void)router_link;
    return true;
}

static void universal_node_gateway_dispatch_response(
    const universal_node_gateway_rx_event_t *event)
{
    if (!event || event->classification != GATEWAY_RX_RESPONSE ||
        !event->authenticated_sender_valid) {
        return;
    }

    const uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    if (gateway_pending_match(event->request_id, event->response_command,
                              event->authenticated_sender_node_id, now_ms,
                              NULL) != GATEWAY_PENDING_MATCH_VALID) {
        return;
    }

    response_transfer_complete_t complete;
    const response_transfer_rx_result_t transfer_result =
        response_transfer_receive_core(
            &event->control_frame, event->canonical_payload.data,
            event->canonical_payload.length, &complete);
    if (transfer_result != RESPONSE_TRANSFER_RX_COMPLETE) {
        return;
    }

    gateway_pending_entry_t claimed;
    if (gateway_pending_claim(event->request_id, event->response_command,
                              event->authenticated_sender_node_id, now_ms,
                              &claimed) == GATEWAY_PENDING_MATCHED &&
        s_response_consumer != NULL) {
        const universal_node_gateway_completed_response_t response = {
            .request_id = event->request_id,
            .authenticated_remote_node_id = event->authenticated_sender_node_id,
            .response_command = event->response_command,
            .response = complete.response,
            .frame = complete.frame,
            .frame_len = complete.frame_len,
        };
        s_response_consumer(&response, s_response_consumer_context);
    }
    response_transfer_complete_release(&complete);
}

static void universal_node_gateway_dispatch_rx(
    const universal_node_gateway_rx_event_t *event)
{
    if (event && event->classification == GATEWAY_RX_RESPONSE) {
        universal_node_gateway_dispatch_response(event);
    }
}

bool universal_node_gateway_register_rx_consumer(
    universal_node_gateway_rx_consumer_t consumer, void *context)
{
    if (consumer == NULL || s_rx_consumer != NULL) return false;
    s_rx_consumer = consumer;
    s_rx_consumer_context = context;
    return true;
}

bool universal_node_gateway_register_response_consumer(
    universal_node_gateway_response_consumer_t consumer, void *context)
{
    if (consumer == NULL || s_response_consumer != NULL) return false;
    s_response_consumer = consumer;
    s_response_consumer_context = context;
    return true;
}

gateway_rx_ingress_result_t universal_node_gateway_handle_thread_rx(
    const uint8_t *payload, uint16_t payload_len, const uint8_t peer_ipv6[16])
{
    universal_node_gateway_rx_event_t event;
    gateway_rx_ingress_result_t result = GATEWAY_RX_INGRESS_CONSUMED;

    if (universal_node_gateway_classify_rx(payload, payload_len, &event) !=
            GATEWAY_RX_RESULT_CLASSIFIED ||
        event.classification == GATEWAY_RX_INVALID) {
        return GATEWAY_RX_INGRESS_INVALID;
    }

    const mesh_session_control_result_t session_result = event.authenticated_sender_valid ?
        mesh_session_control_handle_frame_from_endpoint(&event.control_frame, peer_ipv6) :
        MESH_SESSION_CONTROL_NOT_SESSION;
    if (session_result == MESH_SESSION_CONTROL_HANDLED) return result;
    if (session_result == MESH_SESSION_CONTROL_REJECTED) return GATEWAY_RX_INGRESS_INVALID;
    if (event.authenticated_sender_valid &&
        (peer_ipv6 == NULL ||
         !address_resolver_observe_authenticated_thread_endpoint(
             event.authenticated_sender_node_id, peer_ipv6))) {
        result = GATEWAY_RX_INGRESS_RESOLVER_OBSERVATION_REJECTED;
    }

    universal_node_gateway_dispatch_rx(&event);
    if (s_rx_consumer != NULL) {
        if (event.classification == GATEWAY_RX_DEVICE_EVENT) {
            ESP_LOGI(TAG, "GATEWAY_EVENT_CONSUMER: SRC=0x%04X LEN=%u",
                     event.device_event.source_node_id,
                     event.device_event.payload_length);
        }
        s_rx_consumer(&event, s_rx_consumer_context);
    }
    return result;
}

gateway_rx_result_t universal_node_gateway_classify_rx(
    const uint8_t *payload, uint16_t payload_len,
    universal_node_gateway_rx_event_t *out)
{
    device_event_udev_parse_result_t udev_result;
    synchronization_usyn_parse_result_t usyn_result;
    mesh_auth_verification_t verification;
    control_frame_parsed_t control_frame;

    if (out == NULL) return GATEWAY_RX_RESULT_INVALID_ARGUMENT;
    *out = (universal_node_gateway_rx_event_t){0};
    if (payload == NULL || payload_len == 0u ||
        payload_len > PROTECTED_WIRE_FRAME_CAPACITY) {
        return GATEWAY_RX_RESULT_INVALID_ARGUMENT;
    }

    udev_result = device_event_delivery_parse_udev(payload, payload_len,
                                                   &out->device_event);
    if (udev_result == DEVICE_EVENT_UDEV_VALID ||
        udev_result == DEVICE_EVENT_UDEV_VALID_UNSUPPORTED_PROFILE) {
        out->classification = GATEWAY_RX_DEVICE_EVENT;
        out->claimed_source_valid = true;
        out->claimed_source_node_id = out->device_event.source_node_id;
        out->supported_profile = udev_result == DEVICE_EVENT_UDEV_VALID;
        return GATEWAY_RX_RESULT_CLASSIFIED;
    }
    if (udev_result != DEVICE_EVENT_UDEV_NOT_UDEV) {
        return GATEWAY_RX_RESULT_CLASSIFIED;
    }

    usyn_result = synchronization_parse_usyn(payload, payload_len,
                                              &out->sync_update);
    if (usyn_result == SYNCHRONIZATION_USYN_VALID) {
        out->classification = GATEWAY_RX_SYNC;
        out->claimed_source_valid = true;
        out->claimed_source_node_id = out->sync_update.origin_node_id;
        return GATEWAY_RX_RESULT_CLASSIFIED;
    }
    if (usyn_result != SYNCHRONIZATION_USYN_NOT_USYN) {
        return GATEWAY_RX_RESULT_CLASSIFIED;
    }

    if (mesh_auth_verify_inbound_result(payload, payload_len, &verification) !=
            MESH_AUTH_VERIFY_AUTHENTICATED ||
        verification.protected_payload_length == 0u ||
        verification.protected_payload_length > CONTROL_FRAME_MAX_SIZE ||
        !mesh_auth_control_frame_consistent(payload,
                                            verification.protected_payload_length,
                                            &verification) ||
        !control_frame_parse(payload, (uint16_t)verification.protected_payload_length,
                             &control_frame)) {
        return GATEWAY_RX_RESULT_CLASSIFIED;
    }

    out->authenticated_sender_valid = true;
    out->authenticated_sender_node_id = verification.authenticated_sender_node_id;
    out->canonical_payload.data = payload;
    out->canonical_payload.length =
        (uint16_t)verification.protected_payload_length;
    out->request_id = control_frame.request_id;
    out->response_command = control_frame.command;
    out->control_frame = control_frame;
    out->classification = (control_frame.flags & CONTROL_FLAG_RESPONSE) != 0u
                              ? GATEWAY_RX_RESPONSE
                              : GATEWAY_RX_CONTROL_FRAME_UNSUPPORTED;
    return GATEWAY_RX_RESULT_CLASSIFIED;
}

static universal_node_gateway_submit_result_t map_transport_result(
    transport_result_t result)
{
    switch (result) {
    case TRANSPORT_RESULT_ADMITTED: return UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED;
    case TRANSPORT_RESULT_RESOLVER_UNAVAILABLE: return UNIVERSAL_NODE_GATEWAY_SUBMIT_RESOLVER_UNAVAILABLE;
    case TRANSPORT_RESULT_UNRESOLVED: return UNIVERSAL_NODE_GATEWAY_SUBMIT_DESTINATION_UNRESOLVED;
    case TRANSPORT_RESULT_BACKEND_UNAVAILABLE: return UNIVERSAL_NODE_GATEWAY_SUBMIT_BACKEND_UNAVAILABLE;
    case TRANSPORT_RESULT_BACKEND_REJECTED: return UNIVERSAL_NODE_GATEWAY_SUBMIT_BACKEND_REJECTED;
    default: return UNIVERSAL_NODE_GATEWAY_SUBMIT_INVALID;
    }
}

static universal_node_gateway_submit_result_t map_pending_result(
    gateway_pending_result_t result)
{
    switch (result) {
    case GATEWAY_PENDING_FULL:
        return UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_FULL;
    case GATEWAY_PENDING_DUPLICATE_KEY:
        return UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_CONFLICT;
    case GATEWAY_PENDING_INVALID:
    default:
        return UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_INVALID;
    }
}

static universal_node_gateway_submit_result_t gateway_submit_result(
    universal_node_gateway_submit_result_t result)
{
    s_last_submit_result = result;
    s_last_submit_valid = true;
    s_last_error = result == UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED
                       ? UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NONE
                       : result == UNIVERSAL_NODE_GATEWAY_SUBMIT_NOT_READY
                           ? UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NOT_READY
                           : UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_SUBMIT_REJECTED;
    return result;
}

bool universal_node_gateway_get_diagnostics(
    uint16_t node_id, universal_node_gateway_diagnostics_t *out_diagnostics)
{
    if (node_id == 0u || node_id == 0xffffu || out_diagnostics == NULL) return false;

    memset(out_diagnostics, 0, sizeof(*out_diagnostics));
    out_diagnostics->last_submit_valid = s_last_submit_valid;
    out_diagnostics->last_submit_result = s_last_submit_result;
    out_diagnostics->endpoint_observed =
        address_runtime_resolver_get_observed_thread_endpoint(
            node_id, out_diagnostics->endpoint_thread_ipv6);
    if (out_diagnostics->endpoint_observed) {
        out_diagnostics->endpoint_transport = ADDRESS_TRANSPORT_THREAD;
        out_diagnostics->endpoint_type = RESOLVED_DESTINATION_THREAD_IPV6;
    }
    return true;
}

static universal_node_gateway_thread_role_t gateway_thread_role_from_ot(
    otDeviceRole role)
{
    switch (role) {
    case OT_DEVICE_ROLE_CHILD: return UNIVERSAL_NODE_GATEWAY_THREAD_CHILD;
    case OT_DEVICE_ROLE_ROUTER: return UNIVERSAL_NODE_GATEWAY_THREAD_ROUTER;
    case OT_DEVICE_ROLE_LEADER: return UNIVERSAL_NODE_GATEWAY_THREAD_LEADER;
    case OT_DEVICE_ROLE_DETACHED: return UNIVERSAL_NODE_GATEWAY_THREAD_DETACHED;
    case OT_DEVICE_ROLE_DISABLED:
    default: return UNIVERSAL_NODE_GATEWAY_THREAD_DISABLED;
    }
}

static void universal_node_gateway_contract_linkage(void)
{
    uint16_t (*volatile identity_get)(void) = node_identity_get;
    uint16_t (*volatile empty_frame_length)(void) = control_frame_empty_frame_length;
    bool (*volatile protect)(uint8_t *, size_t, size_t, size_t *) =
        mesh_auth_protect_outbound;
    mesh_auth_verify_status_t (*volatile verify)(const uint8_t *, size_t,
                                                  mesh_auth_verification_t *) =
        mesh_auth_verify_inbound_result;
    bool (*volatile session_initialized)(void) = mesh_session_is_initialized;
    bool (*volatile root_available)(void) = security_root_has_root;

    (void)identity_get;
    (void)empty_frame_length;
    (void)protect;
    (void)verify;
    (void)session_initialized;
    (void)root_available;
}

esp_err_t universal_node_gateway_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    universal_node_gateway_contract_linkage();
    node_identity_init();
    manufacturing_identity_init();
    if (!provisioning_v2_init()) {
        s_last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_INITIALIZATION_FAILED;
        return ESP_FAIL;
    }
    if (!join_existing_init()) {
        s_last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_INITIALIZATION_FAILED;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "canonical foundation node=0x%04X manufacturing=%u p2=%u root=%d",
             node_identity_get(), (unsigned)manufacturing_identity_state(),
             (unsigned)provisioning_v2_lifecycle(), security_root_has_root());
    if (!universal_node_gateway_local_platform_init()) {
        s_last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_INITIALIZATION_FAILED;
        return ESP_FAIL;
    }
    if (!mesh_auth_init(node_identity_get()) ||
        !mesh_session_init(node_identity_get())) {
        s_last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_INITIALIZATION_FAILED;
        return ESP_FAIL;
    }

    s_initialized = true;
    s_last_submit_valid = false;
    s_last_submit_result = UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED;
    s_last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NONE;
    ESP_LOGI(TAG, "Universal Node gateway boundary initialized");
    return ESP_OK;
}

esp_err_t universal_node_gateway_start_provisioning_carrier(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_provisioning_carrier_started) return ESP_OK;

    /* This is the normal-node BLE stack and ADR-079 carrier; it is not a
     * Gateway transport and is started before any product ingress. */
    ble_pairing_policy_init();
    ble_key_manager_init();
    ble_bridge_init();
    const ble_mode_t mode =
        provisioning_v2_lifecycle() == PROVISIONING_V2_LIFECYCLE_NORMAL
            ? BLE_MODE_NORMAL
            : BLE_MODE_PROVISIONING;
    if (ble_bridge_start(mode) != ESP_OK) {
        s_last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_INITIALIZATION_FAILED;
        return ESP_FAIL;
    }
    s_provisioning_carrier_started = true;
    ESP_LOGI(TAG, "canonical BLE carrier started mode=%u", (unsigned)mode);
    return ESP_OK;
}

esp_err_t universal_node_gateway_start(void)
{
    if (!s_initialized) {
        s_last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NOT_INITIALIZED;
        return ESP_ERR_INVALID_STATE;
    }
    if (s_started) {
        return ESP_OK;
    }

    s_started = true;
    s_last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NONE;
    ESP_LOGI(TAG, "Universal Node gateway boundary started");
    return ESP_OK;
}

esp_err_t universal_node_gateway_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_started = false;
    return ESP_OK;
}

bool universal_node_gateway_is_ready(void)
{
    return s_initialized && s_started;
}

bool universal_node_gateway_get_status(universal_node_gateway_status_t *out_status)
{
    if (out_status == NULL) return false;

    *out_status = (universal_node_gateway_status_t){
        .ready = universal_node_gateway_is_ready(),
        .thread_role = UNIVERSAL_NODE_GATEWAY_THREAD_DISABLED,
        .last_error = s_last_error,
    };

    wifi_ap_record_t ap_info;
    out_status->wifi_connected = esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;

    otInstance *instance = esp_openthread_get_instance();
    if (instance == NULL) return true;

    esp_openthread_lock_acquire(portMAX_DELAY);
    out_status->thread_enabled = otIp6IsEnabled(instance);
    out_status->thread_role = gateway_thread_role_from_ot(otThreadGetDeviceRole(instance));
    out_status->thread_attached =
        out_status->thread_role == UNIVERSAL_NODE_GATEWAY_THREAD_CHILD ||
        out_status->thread_role == UNIVERSAL_NODE_GATEWAY_THREAD_ROUTER ||
        out_status->thread_role == UNIVERSAL_NODE_GATEWAY_THREAD_LEADER;
    if (out_status->thread_attached) {
        out_status->rloc16 = otThreadGetRloc16(instance);
        out_status->rloc16_valid = true;
    }
    esp_openthread_lock_release();
    return true;
}

universal_node_gateway_submit_result_t universal_node_gateway_submit_frame(
    uint16_t target_node_id, const uint8_t *frame, uint16_t frame_len)
{
    control_frame_parsed_t parsed;
    delivery_descriptor_t delivery;
    uint8_t protected_frame[PROTECTED_WIRE_FRAME_CAPACITY];
    size_t protected_length = 0u;
    uint8_t expected_response_command = 0u;
    bool pending_registered = false;

    if (!s_initialized || !s_started) return gateway_submit_result(UNIVERSAL_NODE_GATEWAY_SUBMIT_NOT_READY);
    if (!frame || target_node_id == 0u || target_node_id == 0xffffu ||
        frame_len > CONTROL_FRAME_MAX_SIZE ||
        !control_frame_parse(frame, frame_len, &parsed) ||
        parsed.target != target_node_id || parsed.origin != node_identity_get()) {
        return gateway_submit_result(UNIVERSAL_NODE_GATEWAY_SUBMIT_INVALID);
    }
    memcpy(protected_frame, frame, frame_len);
    if (!mesh_auth_protect_outbound(protected_frame, frame_len,
                                    sizeof(protected_frame), &protected_length)) {
        (void)mesh_session_control_start(target_node_id);
        return gateway_submit_result(UNIVERSAL_NODE_GATEWAY_SUBMIT_SECURITY_UNAVAILABLE);
    }
    if ((parsed.flags & CONTROL_FLAG_RESPONSE) == 0u &&
        request_completion_expected_response_command(
            parsed.command, &expected_response_command)) {
        const gateway_pending_result_t pending_result = gateway_pending_register(
            parsed.request_id, expected_response_command, target_node_id,
            (uint64_t)(esp_timer_get_time() / 1000ULL));
        if (pending_result != GATEWAY_PENDING_REGISTERED) {
            return gateway_submit_result(map_pending_result(pending_result));
        }
        pending_registered = true;
    }
    delivery = (delivery_descriptor_t){
        .type = DELIVERY_DEVICE_NODE,
        .destination.node_id = target_node_id,
    };
    const transport_result_t transport_result = transport_send_result(
        &delivery, protected_frame, (uint16_t)protected_length);
    if (pending_registered && transport_result != TRANSPORT_RESULT_ADMITTED) {
        (void)gateway_pending_cancel(parsed.request_id, expected_response_command,
                                     target_node_id);
    }
    return gateway_submit_result(map_transport_result(transport_result));
}
