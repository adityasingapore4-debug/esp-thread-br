#ifndef ADR091_QUAL_FIXTURE_GUARD_H
#define ADR091_QUAL_FIXTURE_GUARD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QUAL_FIXTURE_ID "ADR091_S3_NVS_QUAL_01"
#define QUAL_PARTITION_LABEL "nvs"
#define QUAL_PARTITION_OFFSET UINT32_C(0x9000)
#define QUAL_PARTITION_SIZE UINT32_C(0x6000)
#define QUAL_TARGET_ESP32S3 UINT32_C(0x00000009)
#define QUAL_PARTITION_DATA 1u
#define QUAL_PARTITION_NVS 2u
#define QUAL_CAP_PREFLIGHT UINT32_C(1)
#define QUAL_ALLOWLIST_MAX 8u
#define QUAL_LABEL_CAP 17u
#define QUAL_APPROVAL_REF_CAP 64u

typedef struct {
    char label[QUAL_LABEL_CAP];
    uint8_t type, subtype;
    uint32_t offset, size;
    bool internal_flash;
} qual_partition_identity_t;
/* Trusted input only: reviewed immutable build input authorized outside the
 * DUT, never populated from a file, NVS, command, or self-asserted manifest.
 * No real entry is supplied in M4.2B.1. Values in host tests are synthetic. */
typedef struct {
    char fixture_id[32];
    char external_approval_ref[QUAL_APPROVAL_REF_CAP];
    uint8_t silicon_mac[6];
    uint32_t target, flash_bytes;
    qual_partition_identity_t partition;
    /* Digest returned by esp_partition_get_sha256 for the RUNNING application,
     * approved externally using the same image-digest convention. Not ELF hash
     * or a device-provided version/name. Never an authorization secret. */
    uint8_t application_sha256[32];
    uint32_t capabilities;
} qual_approved_fixture_t;
typedef struct {
    const qual_approved_fixture_t *entries;
    size_t count;
} qual_fixture_allowlist_t;
typedef struct {
    void *ctx;
    bool (*read_silicon)(void *, uint8_t[6], uint32_t *);
    bool (*read_flash_size)(void *, uint32_t *);
    bool (*find_partition)(void *, qual_partition_identity_t *);
    bool (*read_application_hash)(void *, uint8_t[32]);
} qual_identity_provider_t;
typedef enum {
    QUAL_REASON_FIXTURE_NOT_ENROLLED,
    QUAL_REASON_ALLOWLIST_INVALID,
    QUAL_REASON_REQUIRED_IDENTITY_UNAVAILABLE,
    QUAL_REASON_HARDWARE_MISMATCH,
    QUAL_REASON_TARGET_MISMATCH,
    QUAL_REASON_FLASH_CAPACITY_MISMATCH,
    QUAL_REASON_PARTITION_LABEL_MISMATCH,
    QUAL_REASON_PARTITION_TYPE_MISMATCH,
    QUAL_REASON_PARTITION_SUBTYPE_MISMATCH,
    QUAL_REASON_PARTITION_OFFSET_MISMATCH,
    QUAL_REASON_PARTITION_SIZE_MISMATCH,
    QUAL_REASON_PARTITION_DEVICE_MISMATCH,
    QUAL_REASON_APPLICATION_MISMATCH,
    QUAL_REASON_UNAUTHORIZED_CAPABILITY,
    QUAL_REASON_IDENTITY_VERIFIED_PREFLIGHT_ONLY,
    QUAL_REASON_COUNT
} qual_guard_reason_t;
typedef struct {
    qual_guard_reason_t reason;
    bool enrolled, identity_verified;
} qual_guard_result_t;

/* Pure preflight decision. No NVS or mutation capability exists in this API.
 * All result paths, including identity_verified, leave STORAGE_ADMISSION DENIED.
 * Outputs never contain observed or expected identity bytes. No pointer retained. */
qual_guard_result_t qual_fixture_guard(const qual_fixture_allowlist_t *,
                                      qual_identity_provider_t, uint32_t requested_capabilities);
extern const qual_fixture_allowlist_t qual_approved_fixtures;
#endif
