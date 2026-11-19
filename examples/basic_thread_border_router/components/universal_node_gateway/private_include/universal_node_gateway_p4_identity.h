#pragma once
#include <stddef.h>
#include <stdint.h>

/* PROPOSED M3.5C body format. Isolated only: no HTTP adapter, registration or
 * pre-authentication permission. Default builds exclude this source; the inert
 * qualification option compiles/retains it without a runtime caller.
 * Requires the explicitly selected ADR-090/M3 firmware API contract. */
#define GATEWAY_P4_IDENTITY_SIZE 475u
#define GATEWAY_P4_IDENTITY_HEADER_SIZE 12u
#define GATEWAY_P4_IDENTITY_ATTESTATION_OFFSET 12u
#define GATEWAY_P4_IDENTITY_CIB_OFFSET 153u
#define GATEWAY_P4_IDENTITY_PROOF_OFFSET 290u

typedef enum {
    GATEWAY_P4_IDENTITY_OK = 0,
    GATEWAY_P4_IDENTITY_UNAVAILABLE,
    GATEWAY_P4_IDENTITY_NO_SPACE,
} gateway_p4_identity_result_t;

/* Serialized by the future evidence/security owner with identity, root-policy
 * and P4 key changes. No heap, persistent copies, tokens, or mutation authority.
 * Only OK publishes *length=475. Other outcomes clear min(capacity,475) bytes
 * and *length when supplied. Raw response evidence grants no session authority.
 * Caller must discard output on failure; no partial response may be sent. */
gateway_p4_identity_result_t gateway_p4_identity_evidence_build(
    uint8_t *output, size_t capacity, size_t *length);
