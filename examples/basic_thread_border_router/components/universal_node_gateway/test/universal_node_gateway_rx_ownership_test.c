#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "device_event_delivery.h"
#include "synchronization_protocol.h"
#include "universal_node_gateway_rx.h"

static void test_control_frame_view_is_borrowed(void)
{
    uint8_t canonical_frame[CONTROL_FRAME_MAX_SIZE] = { CONTROL_FRAME_VERSION };
    const uint16_t canonical_length = CONTROL_FRAME_HEADER_SIZE;
    universal_node_gateway_rx_event_t event = {
        .classification = GATEWAY_RX_RESPONSE,
        .canonical_payload = {
            .data = canonical_frame,
            .length = canonical_length,
        },
    };

    assert(event.canonical_payload.data == canonical_frame);
    assert(event.canonical_payload.length == canonical_length);
}

static void test_udev_view_is_borrowed(void)
{
    uint8_t udev[] = {
        'U', 'D', 'E', 'V', 1u, 1u, 28u, 0u,
        1u, 1u, 0u, 1u, 0x04u, 0x05u, 1u, 1u, 1u, 1u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 8u,
        1u, 1u, 1u, 0u, 3u, 0u, 1u, 1u,
    };
    device_event_delivery_envelope_t envelope;

    assert(device_event_delivery_parse_udev(udev, sizeof(udev), &envelope) ==
           DEVICE_EVENT_UDEV_VALID);
    assert(envelope.domain == DEVICE_EVENT_DELIVERY_DOMAIN_RUNTIME);
    assert(envelope.type == DEVICE_EVENT_DELIVERY_TYPE_RELAY_STATE_CHANGED);
    assert(envelope.source_node_id == 0x0405u);
    assert(envelope.payload_length == 8u);
    assert(envelope.payload == udev + 8u + DEVICE_EVENT_DELIVERY_HEADER_SIZE);
    assert(envelope.payload[0] == 1u && envelope.payload[7] == 1u);
}

static void test_usyn_output_is_owned(void)
{
    uint8_t usyn[] = {
        'U', 'S', 'Y', 'N', 1u, 1u, 1u,
        1u, 0u, 0x34u, 0x12u, 5u, 0x05u, 0x04u, 3u, 2u,
        0xefu, 0xbeu,
    };
    runtime_state_update_t update;

    assert(synchronization_parse_usyn(usyn, sizeof(usyn), &update) ==
           SYNCHRONIZATION_USYN_VALID);
    assert(update.value.type == RUNTIME_STATE_VALUE_TYPE_U16);
    assert(update.value.u16 == 0xbeefu);
    memset(usyn, 0, sizeof(usyn));
    assert(update.value.type == RUNTIME_STATE_VALUE_TYPE_U16);
    assert(update.value.u16 == 0xbeefu);
}

int main(void)
{
    test_control_frame_view_is_borrowed();
    test_udev_view_is_borrowed();
    test_usyn_output_is_owned();
    assert(sizeof(universal_node_gateway_rx_event_t) < CONTROL_FRAME_MAX_SIZE);
    printf("CONTROL_FRAME_MAX_SIZE=%u sizeof(gateway_rx_event_t)=%zu\n",
           CONTROL_FRAME_MAX_SIZE, sizeof(universal_node_gateway_rx_event_t));
    return 0;
}
