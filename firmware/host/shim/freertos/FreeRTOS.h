#pragma once
// Host FreeRTOS shim. The harness is single-threaded: task creation is a no-op
// that reports success, and semaphores and queues never block.
#include <stdint.h>
#include <stddef.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef uint32_t StackType_t;
typedef struct { int dummy; } portMUX_TYPE;

#define pdFALSE ((BaseType_t)0)
#define pdTRUE ((BaseType_t)1)
#define pdPASS pdTRUE
#define pdFAIL pdFALSE
#define errQUEUE_FULL ((BaseType_t)0)

#define configTICK_RATE_HZ 1000
#define portTICK_PERIOD_MS ((TickType_t)1)
#define portTICK_RATE_MS portTICK_PERIOD_MS
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(t) ((uint32_t)(t))
#define tskNO_AFFINITY 0x7FFFFFFF
#define tskIDLE_PRIORITY ((UBaseType_t)0)
#define configMAX_PRIORITIES 25
#define configMINIMAL_STACK_SIZE 768

#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_ISR(m) ((void)(m))
#define portEXIT_CRITICAL_ISR(m) ((void)(m))
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portYIELD_FROM_ISR(...) ((void)0)
#define portYIELD() ((void)0)
#define portNUM_PROCESSORS 2
