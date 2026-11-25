#include "freertos/task.h"
#include "runtime_owner.h"
#include <stdint.h>
#include <assert.h>
static int64_t now_us;
int64_t esp_timer_get_time(void) { return now_us; }
void vTaskDelay(unsigned ms) { now_us+=(int64_t)ms*1000; }
void vTaskDelete(void *task) { (void)task; }
unsigned uxTaskGetStackHighWaterMark(void *task) { (void)task; return 1024; }
int xTaskCreate(TaskFunction_t fn,const char *name,unsigned depth,void *arg,unsigned priority,void *handle) {
    (void)name; (void)depth; (void)priority; (void)handle; fn(arg); return pdPASS;
}
int main(void) { assert(adr091_synthetic_owner_start()); return 0; }
