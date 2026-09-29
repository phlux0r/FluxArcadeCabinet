#pragma once
#include "FreeRTOS.h"
inline BaseType_t xTaskCreatePinnedToCore(void(*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int) { return 1; }
inline void vTaskDelay(uint32_t) {}
inline void vTaskDelete(TaskHandle_t) {}
inline void taskYIELD() {}
