#pragma once
// Host stub: the ESP-IDF I2S driver calls the real AudioEngine makes. It never
// runs here (no begin()), so these only need to compile.
#include <cstddef>
#include <cstdint>
typedef int i2s_port_t; typedef int i2s_mode_t; typedef int esp_err_t;
#define ESP_OK 0
#define I2S_NUM_0 0
#define I2S_MODE_MASTER 1
#define I2S_MODE_TX 4
enum i2s_bits_per_sample_t { I2S_BITS_PER_SAMPLE_16BIT = 16 };
enum i2s_channel_fmt_t { I2S_CHANNEL_FMT_RIGHT_LEFT };
enum i2s_comm_format_t { I2S_COMM_FORMAT_STAND_I2S = 1 };
#define ESP_INTR_FLAG_LEVEL1 2
#define I2S_PIN_NO_CHANGE -1
struct i2s_config_t { i2s_mode_t mode; uint32_t sample_rate; i2s_bits_per_sample_t bits_per_sample; i2s_channel_fmt_t channel_format; i2s_comm_format_t communication_format; int intr_alloc_flags; int dma_buf_count; int dma_buf_len; bool use_apll; bool tx_desc_auto_clear; int fixed_mclk; };
struct i2s_pin_config_t { int bck_io_num, ws_io_num, data_out_num, data_in_num; };
inline esp_err_t i2s_driver_install(i2s_port_t, const i2s_config_t*, int, void*) { return 0; }
inline esp_err_t i2s_set_pin(i2s_port_t, const i2s_pin_config_t*) { return 0; }
inline esp_err_t i2s_zero_dma_buffer(i2s_port_t) { return 0; }
inline esp_err_t i2s_write(i2s_port_t, const void*, size_t, size_t*, uint32_t) { return 0; }
inline esp_err_t i2s_driver_uninstall(i2s_port_t) { return 0; }
