#pragma once
#include "FreeRTOS.h"
int xTaskCreate(TaskFunction_t, const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(unsigned);
void vTaskDelete(void *);
unsigned uxTaskGetStackHighWaterMark(void *);
