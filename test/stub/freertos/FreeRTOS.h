#pragma once
// Host stub: just enough FreeRTOS for the audio engine to compile.
#include <cstdint>
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) (x)
typedef void* TaskHandle_t; typedef void* SemaphoreHandle_t; typedef int BaseType_t;
#define pdTRUE 1
