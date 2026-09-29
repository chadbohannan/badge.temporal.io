#pragma once
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
void vQueueDelete(QueueHandle_t q);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void* item, BaseType_t* woken);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait);
BaseType_t xQueueReset(QueueHandle_t q);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
#ifdef __cplusplus
}
#endif
