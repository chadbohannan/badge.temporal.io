#pragma once
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack,
                                   void* arg, UBaseType_t prio, TaskHandle_t* out, BaseType_t core);
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                       UBaseType_t prio, TaskHandle_t* out);
void vTaskDelete(TaskHandle_t t);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t* prev, TickType_t inc);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xPortGetCoreID(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
BaseType_t xTaskNotifyGive(TaskHandle_t t);
void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t* woken);
void vTaskSuspend(TaskHandle_t t);
void vTaskResume(TaskHandle_t t);
void taskYIELD(void);
#ifdef __cplusplus
}
#endif
