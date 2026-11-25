#include <stdio.h>
#include "runtime_owner.h"
void app_main(void)
{
    if (!adr091_synthetic_owner_start()) {
        puts("ADR091_RUNTIME_SELFTEST: FAIL task creation");
    }
}
