#pragma once
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TaskFunction_t)(void *);

void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char *const pcName,
                       const uint32_t usStackDepth, void *const pvParameters,
                       uint32_t uxPriority, TaskHandle_t *const pvCreatedTask);
void vTaskDelete(TaskHandle_t xTask);
void vTaskStartScheduler(void);
uint32_t uxTaskGetNumberOfTasks(void);

#ifdef __cplusplus
}
#endif
