#pragma once
#ifndef ADR091_SYNTHETIC_RUNTIME_SELFTEST
#error "Synthetic isolated self-test project only"
#endif
#include <stdbool.h>
/* Only called by this isolated project's app_main. No ingress or business API. */
bool adr091_synthetic_owner_start(void);
