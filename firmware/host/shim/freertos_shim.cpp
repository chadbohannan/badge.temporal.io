// Host FreeRTOS: no threads. Task creation reports success and runs nothing.
// Semaphores and queues never block; a wait that cannot be satisfied moves the
// virtual clock on by the wait time, so a polling loop still ends.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "Arduino.h"

#include <deque>
#include <vector>

namespace {
struct Sem {
  int count;
  int max;
  int depth = 0;  // recursive-mutex depth
  bool recursive;
};
struct Queue {
  size_t item;
  size_t length;
  std::deque<std::vector<uint8_t>> items;
};

// portMAX_DELAY would hang forever on a wait nothing can satisfy; give it a
// finite virtual cost. The wall-clock watchdog catches real deadlocks.
void burn(TickType_t wait) {
  if (wait == 0) return;
  delay(wait == portMAX_DELAY ? 1000 : wait);
}
}  // namespace

extern "C" {

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t,
                                   TaskHandle_t* out, BaseType_t) {
  if (out) *out = (TaskHandle_t)1;
  return pdPASS;
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                       UBaseType_t prio, TaskHandle_t* out) {
  return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, 0);
}
void vTaskDelete(TaskHandle_t) {}
void vTaskDelay(TickType_t ticks) { delay(ticks); }
void vTaskDelayUntil(TickType_t*, TickType_t inc) { delay(inc); }
TickType_t xTaskGetTickCount(void) { return (TickType_t)millis(); }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
BaseType_t xPortGetCoreID(void) { return 1; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t wait) { burn(wait); return 0; }
BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdPASS; }
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t* woken) { if (woken) *woken = pdFALSE; }
void vTaskSuspend(TaskHandle_t) {}
void vTaskResume(TaskHandle_t) {}
void taskYIELD(void) { yield(); }

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return new Sem{1, 1, 0, false}; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return new Sem{1, 1, 0, true}; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return new Sem{0, 1, 0, false}; }
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial) {
  return new Sem{(int)initial, (int)max, 0, false};
}
void vSemaphoreDelete(SemaphoreHandle_t s) { delete (Sem*)s; }

BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t wait) {
  Sem* s = (Sem*)h;
  if (!s) return pdFALSE;
  if (s->count > 0) { s->count--; return pdTRUE; }
  burn(wait);
  return pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
  Sem* s = (Sem*)h;
  if (!s || s->count >= s->max) return pdFALSE;
  s->count++;
  return pdTRUE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h, TickType_t wait) {
  Sem* s = (Sem*)h;
  if (!s) return pdFALSE;
  // One thread, so the holder is always the caller.
  if (s->depth > 0) { s->depth++; return pdTRUE; }
  if (s->count > 0) { s->count--; s->depth = 1; return pdTRUE; }
  burn(wait);
  return pdFALSE;
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h) {
  Sem* s = (Sem*)h;
  if (!s || s->depth == 0) return pdFALSE;
  if (--s->depth == 0) s->count++;
  return pdTRUE;
}
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t h, BaseType_t* woken) {
  if (woken) *woken = pdFALSE;
  return xSemaphoreGive(h);
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize) {
  return new Queue{itemSize, length, {}};
}
void vQueueDelete(QueueHandle_t q) { delete (Queue*)q; }
BaseType_t xQueueSend(QueueHandle_t h, const void* item, TickType_t wait) {
  Queue* q = (Queue*)h;
  if (!q) return pdFALSE;
  if (q->items.size() >= q->length) { burn(wait); return errQUEUE_FULL; }
  const uint8_t* b = (const uint8_t*)item;
  q->items.emplace_back(b, b + q->item);
  return pdTRUE;
}
BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t wait) { return xQueueSend(q, item, wait); }
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void* item, BaseType_t* woken) {
  if (woken) *woken = pdFALSE;
  return xQueueSend(q, item, 0);
}
BaseType_t xQueueReceive(QueueHandle_t h, void* item, TickType_t wait) {
  Queue* q = (Queue*)h;
  if (!q) return pdFALSE;
  if (q->items.empty()) { burn(wait); return pdFALSE; }
  memcpy(item, q->items.front().data(), q->item);
  q->items.pop_front();
  return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t h) {
  Queue* q = (Queue*)h;
  if (q) q->items.clear();
  return pdTRUE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t h) {
  Queue* q = (Queue*)h;
  return q ? (UBaseType_t)q->items.size() : 0;
}

}  // extern "C"
