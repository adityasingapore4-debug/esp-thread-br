#ifndef ADR091_QUAL_RESULT_PROTOCOL_H
#define ADR091_QUAL_RESULT_PROTOCOL_H
#include "fixture_guard.h"
#define QUAL_RESULT_CAP 512u
/* Fixed field names, enum-derived literals only. Never formats raw identities,
 * caller-supplied strings, record bytes or hashes. All storage permissions stay
 * disabled even for a positive identity-only preflight. */
bool qual_result_format(qual_guard_result_t, char *, size_t, size_t *);
#endif
