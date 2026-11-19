/* Inactive M3.5D producer/encoder. Default builds exclude it; explicit inert
 * qualification may compile/link it. No HTTPS registration is authorized. */
#include "universal_node_gateway_p4_identity.h"
#include "manufacturing_identity.h"
#include "manufacturing_cib_storage.h"
#include "p4_server_identity.h"
#include <string.h>

_Static_assert(MANUFACTURING_IDENTITY_ATTESTATION_RECORD_SIZE == 141u, "attestation contract");
_Static_assert(MANUFACTURING_CIB_V1_SIZE == 137u, "CIB contract");
_Static_assert(P4_PROOF_SIZE == 185u, "P4 contract");
_Static_assert(12u + 141u + 137u + 185u == GATEWAY_P4_IDENTITY_SIZE, "UNIB contract");

static uint32_t read32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Final encoder publication: body bytes were filled directly by their owners.
 * Explicit wire bytes only; never serialize a compiler structure. */
static void encode_header(uint8_t *out)
{
    memcpy(out, "UNIB", 4);
    out[4] = 1; out[5] = 0;
    out[6] = 0; out[7] = 141;
    out[8] = 0; out[9] = 137;
    out[10] = 0; out[11] = 185;
}

gateway_p4_identity_result_t gateway_p4_identity_evidence_build(
    uint8_t *output, size_t capacity, size_t *length)
{
    manufacturing_attestation_record_t attestation;
    manufacturing_cib_status_t status;
    uint8_t public_key[P4_PUBLIC_KEY_SIZE];
    uint32_t generation;
    size_t cib_length;
    size_t clear_length = capacity < GATEWAY_P4_IDENTITY_SIZE ? capacity : GATEWAY_P4_IDENTITY_SIZE;
    if (length) *length = 0;
    if (output) memset(output, 0, clear_length);
    if (!output || !length) return GATEWAY_P4_IDENTITY_UNAVAILABLE;
    if (capacity < GATEWAY_P4_IDENTITY_SIZE) return GATEWAY_P4_IDENTITY_NO_SPACE;

    uint8_t *attestation_wire = output + GATEWAY_P4_IDENTITY_ATTESTATION_OFFSET;
    uint8_t *cib = output + GATEWAY_P4_IDENTITY_CIB_OFFSET;
    uint8_t *proof = output + GATEWAY_P4_IDENTITY_PROOF_OFFSET;
    if (!manufacturing_identity_get_attestation(&attestation) ||
        !manufacturing_attestation_record_encode(&attestation, attestation_wire) ||
        manufacturing_identity_cib_load(cib, &cib_length) != CIB_VALID ||
        cib_length != MANUFACTURING_CIB_V1_SIZE ||
        !p4_server_identity_public(public_key, &generation) || !generation ||
        !p4_server_identity_build_proof_checked(proof, &status) || status != CIB_VALID)
        goto unavailable;

    /* Reuse security owners' verifiers on the actual delivered bytes. This
     * detects mismatched snapshots; transport does not become a crypto owner. */
    if (!manufacturing_identity_verify_p4_server_key_proof(proof, &attestation, cib, cib_length) ||
        read32(proof + 48) != generation || memcmp(proof + 52, public_key, sizeof(public_key)) != 0)
        goto unavailable;
    /* Detect a key/generation change during collection. Calls still require
     * owner serialization: this is not a new locking or rotation mechanism. */
    if (!p4_server_identity_public(public_key, &generation) || !generation ||
        read32(proof + 48) != generation || memcmp(proof + 52, public_key, sizeof(public_key)) != 0)
        goto unavailable;
    encode_header(output);
    *length = GATEWAY_P4_IDENTITY_SIZE;
    return GATEWAY_P4_IDENTITY_OK;

unavailable:
    memset(output, 0, GATEWAY_P4_IDENTITY_SIZE);
    return GATEWAY_P4_IDENTITY_UNAVAILABLE;
}
