#pragma once
// Host stub: deep sleep, which PowerManager uses.
#include "driver/gpio.h"
enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_EXT0 };
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_UNDEFINED; }
inline int esp_sleep_enable_ext0_wakeup(gpio_num_t, int) { return 0; }
inline void esp_deep_sleep_start() {}
