#pragma once

#include "sdkconfig.h"

#if CONFIG_UN_GATEWAY_DEV_P4_TLS_SMOKE
void dev_heap_diagnostics_init(void);
void dev_heap_snapshot(const char *boundary);
#else
#define dev_heap_diagnostics_init() ((void)0)
#define dev_heap_snapshot(boundary) ((void)0)
#endif

#if CONFIG_UN_GATEWAY_DEV_PSRAM
void dev_heap_watch_https(void);
#else
#define dev_heap_watch_https() ((void)0)
#endif
