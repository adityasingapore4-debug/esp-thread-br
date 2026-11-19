#include "universal_node_gateway_p4_identity.h"
#include "manufacturing_cib_storage.h"
#include "p4_server_identity.h"
#include "p4_server_proof.h"
#include "p4_storage.h"
#include "p4_crypto.h"
#include "provisioning_v2.h"
#include "node_identity.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "test_nvs.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t local_principal[8], root_secret[64], device_seed[32], xprivate[32];
static uint16_t local_node;
static bool local_read_ok = true;
static manufacturing_root_t root;
static manufacturing_attestation_record_t att;
static uint8_t artifact[138], credentials[205];
static unsigned cases;

/* Linker hardware-boundary wrappers only. Canonical CRC implementation and
 * manufacturing verifier remain the real production code. */
uint16_t __wrap_node_identity_get(void) { return local_node; }
bool __wrap_node_identity_principal_eui64_get(uint8_t out[8])
{
    if (!local_read_ok) return false;
    memcpy(out, local_principal, 8); return true;
}
static void sign_cib(uint8_t cib[137]) { crypto_ed25519_sign(cib + 73, root_secret, cib, 73); }
static void make_attestation(manufacturing_attestation_record_t *record)
{
    uint8_t wire[141], message[98];
    assert(manufacturing_attestation_record_encode(record, wire));
    memcpy(message, "UN-ADR077-MFG-CERT-V1", 21); memcpy(message + 21, wire, 77);
    crypto_ed25519_sign(record->signature, root_secret, message, sizeof(message));
}
static void make_cib(const manufacturing_attestation_record_t *record, uint8_t profile, uint16_t node, uint8_t cib[137])
{
    memset(cib, 0, 137); memcpy(cib, "UNCanonicalBindV1\0\0\0", 20);
    cib[20] = 1; cib[21] = profile; cib[22] = node >> 8; cib[23] = node;
    memcpy(cib + 24, record->eui64, 8);
    cib[32] = 0x54; cib[33] = 0x45; cib[34] = 0x53; cib[35] = 0x54;
    assert(manufacturing_identity_attestation_fingerprint(record, cib + 36));
    cib[71] = 1; sign_cib(cib);
}
static void setup(unsigned profile_case)
{
    uint8_t seed[32] = {11}, secret[64], seed_copy[32];
    const uint8_t principals[3][8] = {
        {0xba,0xf8,0x62,0xff,0xfe,0x62,0xf9,0x84},
        {0x10,0x20,0x30,0xff,0xfe,0x60,0x70,0x80},
        {0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80}};
    const uint8_t s3_mac[6] = {0xb8,0xf8,0x62,0x62,0xf9,0x84};
    test_nvs_reset();
    local_read_ok = true;
    root = (manufacturing_root_t){.key_id = 0x54455354, .state = MANUFACTURING_ROOT_ACTIVE};
    crypto_ed25519_key_pair(root_secret, root.public_key, seed);
    memcpy(local_principal, principals[profile_case], 8);
    local_node = profile_case == 0 ? node_identity_from_factory_id(s3_mac, 6) :
                                   node_identity_from_eui64(local_principal);
    if (profile_case == 0) assert(local_node == 0xfc61);
    att = (manufacturing_attestation_record_t){.manufacturer_key_id = root.key_id};
    memcpy(att.eui64, local_principal, 8);
    memset(device_seed, 22, 32); memset(xprivate, 33, 32);
    memcpy(seed_copy, device_seed, 32);
    crypto_ed25519_key_pair(secret, att.ed25519_public_key, seed_copy);
    crypto_x25519_public_key(att.x25519_public_key, xprivate);
    make_attestation(&att);
    assert(manufacturing_identity_configure_roots(&root, 1));
    assert(manufacturing_identity_development_provision(device_seed, xprivate, &att));
    manufacturing_identity_init();
    assert(manufacturing_identity_state() == MANUFACTURING_IDENTITY_VALID);
    memcpy(credentials, device_seed, 32); memcpy(credentials + 32, xprivate, 32);
    assert(manufacturing_attestation_record_encode(&att, credentials + 64));
    make_cib(&att, profile_case == 0 ? 2 : 1, local_node, artifact);
}

static bool installation_allowed;
static uint8_t p4_record[P4_RECORD_SIZE];
static bool p4_present;
static bool ready(void) { return true; }
static p4_load_result_t load(uint8_t out[P4_RECORD_SIZE])
{
    if (!p4_present) return P4_LOAD_ABSENT;
    memcpy(out, p4_record, sizeof(p4_record)); return P4_LOAD_PRESENT;
}
static bool commit(const uint8_t record[P4_RECORD_SIZE])
{
    memcpy(p4_record, record, sizeof(p4_record)); p4_present = true; return true;
}
const p4_storage_provider_t *p4_storage_provider(void)
{
    static const p4_storage_provider_t provider = {ready, load, commit};
    return &provider;
}
bool provisioning_v2_snapshot_get(provisioning_v2_snapshot_t *out)
{
    if (!installation_allowed) return false;
    memset(out, 0, sizeof(*out)); out->installation_id[0] = 1; return true;
}

typedef enum {
    GOOD, NO_ATTESTATION, NO_CIB, BAD_CIB, CIB_IO, CIB_LENGTH,
    TORN_CIB, NO_PROOF, BAD_PROOF_STATUS, BAD_PROOF_SIGNATURE,
    BAD_PROOF_PRINCIPAL, BAD_PROOF_NODE, NO_PUBLIC_KEY,
    ZERO_GENERATION, ROTATED_GENERATION, ROTATED_KEY
} fault_t;
static fault_t fault;
static unsigned proof_calls, cib_calls, public_calls;
bool __real_manufacturing_identity_get_attestation(manufacturing_attestation_record_t *);
bool __wrap_manufacturing_identity_get_attestation(manufacturing_attestation_record_t *out)
{
    return fault != NO_ATTESTATION && __real_manufacturing_identity_get_attestation(out);
}
manufacturing_cib_status_t __real_manufacturing_identity_cib_load(uint8_t *, size_t *);
manufacturing_cib_status_t __wrap_manufacturing_identity_cib_load(uint8_t *out, size_t *n)
{
    ++cib_calls;
    if (fault == NO_CIB || fault == BAD_CIB || fault == CIB_IO) {
        *n = 0; memset(out, 0, 137);
        return fault == NO_CIB ? CIB_ABSENT : fault == BAD_CIB ? CIB_INVALID : CIB_STORAGE_ERROR;
    }
    manufacturing_cib_status_t status = __real_manufacturing_identity_cib_load(out, n);
    if (cib_calls == 1 && fault == CIB_LENGTH) *n = 136;
    if (cib_calls == 1 && fault == TORN_CIB) out[36] ^= 1;
    return status;
}
bool __real_p4_server_identity_build_proof_checked(uint8_t *, manufacturing_cib_status_t *);
bool __wrap_p4_server_identity_build_proof_checked(uint8_t *out, manufacturing_cib_status_t *status)
{
    ++proof_calls;
    if (fault == NO_PROOF) { memset(out, 0x7b, 185); *status = CIB_VALID; return false; }
    bool ok = __real_p4_server_identity_build_proof_checked(out, status);
    if (fault == BAD_PROOF_STATUS) *status = CIB_INVALID;
    if (fault == BAD_PROOF_SIGNATURE) out[121] ^= 1;
    if (fault == BAD_PROOF_PRINCIPAL) out[24] ^= 1;
    if (fault == BAD_PROOF_NODE) out[23] ^= 1;
    return ok;
}
bool __real_p4_server_identity_public(uint8_t *, uint32_t *);
bool __wrap_p4_server_identity_public(uint8_t *out, uint32_t *generation)
{
    ++public_calls;
    if (fault == NO_PUBLIC_KEY) return false;
    bool ok = __real_p4_server_identity_public(out, generation);
    if (fault == ZERO_GENERATION) *generation = 0;
    if (public_calls == 2 && fault == ROTATED_GENERATION) ++*generation;
    if (public_calls == 2 && fault == ROTATED_KEY) out[1] ^= 1;
    return ok;
}
#define CHECK(x) do { assert(x); ++cases; } while (0)
static void reset_calls(void) { proof_calls = 0; cib_calls = 0; public_calls = 0; }
static void unavailable(void)
{
    uint8_t out[475], zero[475] = {0}; size_t length = 999;
    memset(out, 0xa5, sizeof(out)); reset_calls();
    CHECK(gateway_p4_identity_evidence_build(out, sizeof(out), &length) == GATEWAY_P4_IDENTITY_UNAVAILABLE);
    CHECK(length == 0); CHECK(memcmp(out, zero, sizeof(out)) == 0);
    CHECK(test_nvs_committed_blob_equals("mfg.identity", "record_v1", credentials, sizeof(credentials)));
}
int main(void)
{
    uint8_t out[477], expected_proof[185]; size_t length;
    const uint8_t header[12] = {'U','N','I','B',1,0,0,141,0,137,0,185};
    for (unsigned platform = 0; platform < 3; ++platform) {
        fault = GOOD; setup(platform); p4_present = false; installation_allowed = true;
        CHECK(p4_server_identity_init());
        CHECK(test_nvs_seed_blob("mfg.identity", "cib_v1", artifact, 137) == ESP_OK);
        manufacturing_cib_status_t status;
        CHECK(__real_p4_server_identity_build_proof_checked(expected_proof, &status));
        reset_calls(); memset(out, 0xa5, sizeof(out));
        CHECK(gateway_p4_identity_evidence_build(out + 1, 475, &length) == GATEWAY_P4_IDENTITY_OK);
        CHECK(length == 475); CHECK(memcmp(out + 1, header, 12) == 0);
        CHECK(memcmp(out + 1 + 12, credentials + 64, 141) == 0);
        CHECK(memcmp(out + 1 + 153, artifact, 137) == 0);
        CHECK(memcmp(out + 1 + 290, expected_proof, 185) == 0);
        CHECK(out[0] == 0xa5 && out[476] == 0xa5);
        CHECK(public_calls == 2 && proof_calls == 1);
        for (fault_t f = NO_ATTESTATION; f <= ROTATED_KEY; ++f) {
            fault = f; unavailable();
        }
        fault = GOOD; installation_allowed = false; unavailable(); installation_allowed = true;
        test_nvs_fail_next_open(ESP_FAIL); unavailable();
        root.state = MANUFACTURING_ROOT_REVOKED;
        CHECK(manufacturing_identity_configure_roots(&root, 1)); unavailable();
        root.state = MANUFACTURING_ROOT_ACTIVE;
        CHECK(manufacturing_identity_configure_roots(&root, 1));
        uint8_t saved = artifact[73]; artifact[73] ^= 1;
        CHECK(test_nvs_seed_blob("mfg.identity", "cib_v1", artifact, 137) == ESP_OK); unavailable();
        artifact[73] = saved;
        CHECK(test_nvs_seed_blob("mfg.identity", "cib_v1", artifact, 137) == ESP_OK);
        for (size_t capacity = 0; capacity < 475; ++capacity) {
            memset(out, 0xa5, sizeof(out)); length = 999; reset_calls();
            CHECK(gateway_p4_identity_evidence_build(out + 1, capacity, &length) == GATEWAY_P4_IDENTITY_NO_SPACE);
            CHECK(length == 0 && proof_calls == 0 && cib_calls == 0);
            CHECK(out[0] == 0xa5 && out[capacity + 1] == 0xa5);
            for (size_t i = 1; i <= capacity; ++i) assert(out[i] == 0);
        }
    }
    fault = GOOD; length = 999;
    CHECK(gateway_p4_identity_evidence_build(NULL, 475, &length) == GATEWAY_P4_IDENTITY_UNAVAILABLE);
    CHECK(length == 0);
    memset(out, 0xa5, sizeof(out));
    CHECK(gateway_p4_identity_evidence_build(out, 475, NULL) == GATEWAY_P4_IDENTITY_UNAVAILABLE);
    for (size_t i = 0; i < 475; ++i) assert(out[i] == 0);
    CHECK(out[475] == 0xa5);
    printf("Identity evidence: %u checks PASS (real manufacturer/P4 verifiers; synthetic keys)\n", cases);
    return 0;
}
