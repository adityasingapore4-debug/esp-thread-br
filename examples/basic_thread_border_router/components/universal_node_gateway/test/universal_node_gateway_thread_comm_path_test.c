#include <assert.h>
#include <string.h>

#include "universal_node_gateway_thread_comm_path.h"

#include <openthread/thread.h>
#include <openthread/thread_ftd.h>

struct otInstance { int unused; };

static otDeviceRole s_role = OT_DEVICE_ROLE_ROUTER;
static otError s_leader_result = OT_ERROR_NONE;
static otIp6Address s_leader = { .mFields.m8 =
    { 0xfd, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x00, 0x00, 0x00, 0xff, 0xfe, 0x00, 0x12, 0x34 } };
static otMeshLocalPrefix s_prefix = { .m8 =
    { 0xfd, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 } };
static otRouterInfo s_router;

uint16_t node_identity_from_eui64(const uint8_t *ext_addr)
{
    return ((uint16_t)ext_addr[0] << 8u) | ext_addr[1];
}

otDeviceRole otThreadGetDeviceRole(otInstance *instance)
{
    return instance ? s_role : OT_DEVICE_ROLE_DISABLED;
}

otError otThreadGetLeaderRloc(otInstance *instance, otIp6Address *address)
{
    if (!instance || !address || s_leader_result != OT_ERROR_NONE) return OT_ERROR_FAILED;
    *address = s_leader;
    return OT_ERROR_NONE;
}

const otMeshLocalPrefix *otThreadGetMeshLocalPrefix(otInstance *instance)
{
    return instance ? &s_prefix : NULL;
}

otError otThreadGetRouterInfo(otInstance *instance, uint16_t router_id,
                              otRouterInfo *info)
{
    if (!instance || !info || router_id != 0u || !s_router.mAllocated) {
        return OT_ERROR_FAILED;
    }
    *info = s_router;
    return OT_ERROR_NONE;
}

otError otThreadGetChildInfoByIndex(otInstance *instance, uint16_t index,
                                    otChildInfo *info)
{
    (void)instance;
    (void)index;
    (void)info;
    return OT_ERROR_FAILED;
}

static void set_node_id(otExtAddress *address, uint16_t node_id)
{
    memset(address, 0, sizeof(*address));
    address->m8[0] = (uint8_t)(node_id >> 8u);
    address->m8[1] = (uint8_t)node_id;
}

int main(void)
{
    struct otInstance instance = {0};
    resolved_destination_t destination;

    memset(&s_router, 0, sizeof(s_router));
    s_router.mAllocated = true;
    s_router.mLinkEstablished = true;
    s_router.mRloc16 = 0x5678u;
    set_node_id(&s_router.mExtAddress, 0x72efu);
    assert(universal_node_gateway_thread_select_device_path(
               &instance, 0x72efu, &destination) == ADDRESS_RESOLUTION_RESOLVED);
    assert(destination.destination.thread_ipv6[14] == 0x56u &&
           destination.destination.thread_ipv6[15] == 0x78u);

    memset(&s_router, 0, sizeof(s_router));
    s_role = OT_DEVICE_ROLE_ROUTER;
    assert(universal_node_gateway_thread_select_device_path(
               &instance, 0x9782u, &destination) == ADDRESS_RESOLUTION_RESOLVED);
    assert(memcmp(destination.destination.thread_ipv6, s_leader.mFields.m8,
                  THREAD_ENDPOINT_IPV6_SIZE) == 0);

    s_role = OT_DEVICE_ROLE_LEADER;
    assert(universal_node_gateway_thread_select_device_path(
               &instance, 0x9782u, &destination) == ADDRESS_RESOLUTION_UNRESOLVED);

    s_role = OT_DEVICE_ROLE_ROUTER;
    s_leader_result = OT_ERROR_FAILED;
    assert(universal_node_gateway_thread_select_device_path(
               &instance, 0x9782u, &destination) == ADDRESS_RESOLUTION_UNRESOLVED);
    assert(universal_node_gateway_thread_select_device_path(
               &instance, 0u, &destination) == ADDRESS_RESOLUTION_INVALID);
    return 0;
}
