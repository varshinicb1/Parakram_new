/**
 * @file pal_impl.c  [ESP32-S3]
 * @brief PAL implementation for ESP32-S3 using ESP-IDF 5.x.
 */

#include "parakram_pal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "driver/i2s_std.h"
#include "esp_adc/adc_oneshot.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static const char *TAG_PAL = "PAL_ESP32S3";

/* ── System ──────────────────────────────────────────────────────────────── */

const char *pal_get_platform_name(void) { return "ESP32-S3"; }
pal_mcu_t   pal_get_mcu(void)           { return PAL_MCU_ESP32S3; }
uint32_t    pal_get_cpu_freq_hz(void)   { return 240000000UL; }

pal_err_t pal_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return (err == ESP_OK) ? PAL_OK : PAL_ERR_FAIL;
}

void pal_reboot(void)            { esp_restart(); }
uint32_t pal_get_free_heap(void) { return (uint32_t)esp_get_free_heap_size(); }
void pal_feed_watchdog(void)     { esp_task_wdt_reset(); }

/* ── Timing ──────────────────────────────────────────────────────────────── */

void     pal_delay_us(uint32_t us) { esp_rom_delay_us(us); }
void     pal_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
uint32_t pal_get_time_ms(void)     { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
uint64_t pal_get_time_us(void)     { return (uint64_t)esp_timer_get_time(); }

/* ── Logging ─────────────────────────────────────────────────────────────── */

void pal_log(int level, const char *tag, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    switch (level) {
        case PAL_LOG_ERROR: ESP_LOGE(tag, "%s", buf); break;
        case PAL_LOG_WARN:  ESP_LOGW(tag, "%s", buf); break;
        case PAL_LOG_INFO:  ESP_LOGI(tag, "%s", buf); break;
        case PAL_LOG_DEBUG: ESP_LOGD(tag, "%s", buf); break;
    }
}

/* ── GPIO ────────────────────────────────────────────────────────────────── */

typedef struct { pal_gpio_isr_t cb; void *arg; } isr_entry_t;
static isr_entry_t s_isr[SOC_GPIO_PIN_COUNT];
static bool s_isr_service_installed = false;

static void IRAM_ATTR gpio_isr_dispatcher(void *arg) {
    int pin = (int)(intptr_t)arg;
    if (pin < SOC_GPIO_PIN_COUNT && s_isr[pin].cb) {
        s_isr[pin].cb(s_isr[pin].arg);
    }
}

pal_err_t pal_gpio_set_direction(int pin, pal_gpio_dir_t dir, bool pullup, bool pulldown) {
    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << pin),
        .mode          = (dir == PAL_GPIO_OUTPUT) ? GPIO_MODE_OUTPUT :
                         (dir == PAL_GPIO_INPUT_OUTPUT) ? GPIO_MODE_INPUT_OUTPUT : GPIO_MODE_INPUT,
        .pull_up_en    = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en  = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    return (gpio_config(&cfg) == ESP_OK) ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_gpio_set_level(int pin, int level) {
    return (gpio_set_level((gpio_num_t)pin, level) == ESP_OK) ? PAL_OK : PAL_ERR_FAIL;
}

int pal_gpio_get_level(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

pal_err_t pal_gpio_set_interrupt(int pin, pal_gpio_intr_t edge, pal_gpio_isr_t cb, void *arg) {
    if (!s_isr_service_installed) {
        gpio_install_isr_service(0);
        s_isr_service_installed = true;
    }
    static const gpio_int_type_t map[] = {
        GPIO_INTR_DISABLE, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE,
        GPIO_INTR_ANYEDGE, GPIO_INTR_LOW_LEVEL, GPIO_INTR_HIGH_LEVEL
    };
    gpio_set_intr_type((gpio_num_t)pin, map[edge]);
    s_isr[pin].cb  = cb;
    s_isr[pin].arg = arg;
    gpio_isr_handler_add((gpio_num_t)pin, gpio_isr_dispatcher, (void *)(intptr_t)pin);
    return PAL_OK;
}

pal_err_t pal_gpio_remove_interrupt(int pin) {
    gpio_isr_handler_remove((gpio_num_t)pin);
    s_isr[pin].cb = NULL;
    return PAL_OK;
}

/* ── I2C ─────────────────────────────────────────────────────────────────── */

static bool s_i2c_init[PAL_I2C_MAX_BUSES] = {false};

pal_err_t pal_i2c_init(uint8_t bus, int sda, int scl, uint32_t freq_hz) {
    if (bus >= PAL_I2C_MAX_BUSES) return PAL_ERR_INVALID;
    if (s_i2c_init[bus]) return PAL_OK;
    i2c_config_t c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda, .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = freq_hz,
    };
    if (i2c_param_config(bus, &c) != ESP_OK) return PAL_ERR_FAIL;
    if (i2c_driver_install(bus, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) return PAL_ERR_FAIL;
    s_i2c_init[bus] = true;
    return PAL_OK;
}

pal_err_t pal_i2c_deinit(uint8_t bus) {
    if (bus >= PAL_I2C_MAX_BUSES) return PAL_ERR_INVALID;
    i2c_driver_delete(bus);
    s_i2c_init[bus] = false;
    return PAL_OK;
}

pal_err_t pal_i2c_read(uint8_t bus, uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len) {
    if (bus >= PAL_I2C_MAX_BUSES || !s_i2c_init[bus]) return PAL_ERR_INVALID;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &buf[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (err == ESP_OK) ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_i2c_write(uint8_t bus, uint8_t addr, uint8_t reg, const uint8_t *buf, uint16_t len) {
    if (bus >= PAL_I2C_MAX_BUSES || !s_i2c_init[bus]) return PAL_ERR_INVALID;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    if (len > 0) i2c_master_write(cmd, (uint8_t *)buf, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (err == ESP_OK) ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_i2c_write_raw(uint8_t bus, uint8_t addr, const uint8_t *buf, uint16_t len) {
    if (bus >= PAL_I2C_MAX_BUSES || !s_i2c_init[bus]) return PAL_ERR_INVALID;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t *)buf, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (err == ESP_OK) ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_i2c_read_raw(uint8_t bus, uint8_t addr, uint8_t *buf, uint16_t len) {
    if (bus >= PAL_I2C_MAX_BUSES || !s_i2c_init[bus]) return PAL_ERR_INVALID;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &buf[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (err == ESP_OK) ? PAL_OK : PAL_ERR_FAIL;
}

/* ── PWM (LEDC) ──────────────────────────────────────────────────────────── */

typedef struct { bool used; uint8_t res; uint32_t freq; } pwm_ch_t;
static pwm_ch_t s_pwm[PAL_PWM_MAX_CHANNELS];

pal_err_t pal_pwm_init(uint8_t ch, int pin, uint32_t freq_hz, uint8_t res_bits) {
    if (ch >= PAL_PWM_MAX_CHANNELS) return PAL_ERR_INVALID;
    ledc_timer_config_t tc = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num  = (ledc_timer_t)(ch % 4),
        .duty_resolution = (ledc_timer_bit_t)res_bits,
        .freq_hz    = freq_hz,
        .clk_cfg    = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&tc) != ESP_OK) return PAL_ERR_FAIL;
    ledc_channel_config_t cc = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = (ledc_channel_t)ch,
        .timer_sel  = (ledc_timer_t)(ch % 4),
        .gpio_num   = pin,
        .duty       = 0,
        .hpoint     = 0,
    };
    if (ledc_channel_config(&cc) != ESP_OK) return PAL_ERR_FAIL;
    s_pwm[ch] = (pwm_ch_t){true, res_bits, freq_hz};
    return PAL_OK;
}

pal_err_t pal_pwm_deinit(uint8_t ch) {
    if (ch >= PAL_PWM_MAX_CHANNELS) return PAL_ERR_INVALID;
    ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, 0);
    s_pwm[ch].used = false;
    return PAL_OK;
}

pal_err_t pal_pwm_set_duty(uint8_t ch, uint32_t duty) {
    if (ch >= PAL_PWM_MAX_CHANNELS || !s_pwm[ch].used) return PAL_ERR_INVALID;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
    return PAL_OK;
}

pal_err_t pal_pwm_set_freq(uint8_t ch, uint32_t freq_hz) {
    if (ch >= PAL_PWM_MAX_CHANNELS || !s_pwm[ch].used) return PAL_ERR_INVALID;
    ledc_set_freq(LEDC_LOW_SPEED_MODE, (ledc_timer_t)(ch % 4), freq_hz);
    s_pwm[ch].freq = freq_hz;
    return PAL_OK;
}

uint32_t pal_pwm_get_max_duty(uint8_t res) { return (1u << res) - 1; }

/* ── ADC ─────────────────────────────────────────────────────────────────── */

static adc_oneshot_unit_handle_t s_adc1 = NULL;

pal_err_t pal_adc_init(uint8_t channel, int pin) {
    (void)pin;
    if (!s_adc1) {
        adc_oneshot_unit_init_cfg_t cfg = { .unit_id = ADC_UNIT_1 };
        adc_oneshot_new_unit(&cfg, &s_adc1);
    }
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    return (adc_oneshot_config_channel(s_adc1, (adc_channel_t)channel, &ch_cfg) == ESP_OK)
           ? PAL_OK : PAL_ERR_FAIL;
}

uint16_t pal_adc_read(uint8_t channel) {
    int v = 0;
    if (s_adc1) adc_oneshot_read(s_adc1, (adc_channel_t)channel, &v);
    return (uint16_t)v;
}

float pal_adc_read_voltage(uint8_t channel) {
    return pal_adc_read(channel) * 3.3f / 4095.0f;
}

/* ── UART ────────────────────────────────────────────────────────────────── */

#define UART_BUF 512
static bool s_uart_init[PAL_UART_MAX_PORTS] = {false};

pal_err_t pal_uart_init(uint8_t port, int tx, int rx, uint32_t baud) {
    if (port >= PAL_UART_MAX_PORTS) return PAL_ERR_INVALID;
    uart_config_t c = {
        .baud_rate = (int)baud, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    if (uart_driver_install(port, UART_BUF, 0, 0, NULL, 0) != ESP_OK) return PAL_ERR_FAIL;
    uart_param_config(port, &c);
    uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    s_uart_init[port] = true;
    return PAL_OK;
}

pal_err_t pal_uart_deinit(uint8_t port) {
    uart_driver_delete(port);
    s_uart_init[port] = false;
    return PAL_OK;
}

int pal_uart_available(uint8_t port) {
    size_t n = 0;
    uart_get_buffered_data_len(port, &n);
    return (int)n;
}

int pal_uart_read_byte(uint8_t port, uint32_t timeout_ms) {
    uint8_t b;
    return (uart_read_bytes(port, &b, 1, pdMS_TO_TICKS(timeout_ms)) == 1) ? b : -1;
}

int pal_uart_read(uint8_t port, uint8_t *buf, size_t len, uint32_t timeout_ms) {
    return uart_read_bytes(port, buf, len, pdMS_TO_TICKS(timeout_ms));
}

pal_err_t pal_uart_write(uint8_t port, const uint8_t *buf, size_t len) {
    return (uart_write_bytes(port, buf, len) >= 0) ? PAL_OK : PAL_ERR_FAIL;
}

/* ── I2S ─────────────────────────────────────────────────────────────────── */

static i2s_chan_handle_t s_i2s_rx[2] = {NULL};
static i2s_chan_handle_t s_i2s_tx[2] = {NULL};

pal_err_t pal_i2s_init(uint8_t port, pal_i2s_dir_t dir,
                        int bclk, int ws, int data,
                        uint32_t sample_rate, uint8_t bits) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)port, I2S_ROLE_MASTER);
    i2s_chan_handle_t *handle = (dir == PAL_I2S_DIR_RX) ? &s_i2s_rx[port] : &s_i2s_tx[port];
    i2s_new_channel(&cc, (dir == PAL_I2S_DIR_TX) ? handle : NULL,
                         (dir == PAL_I2S_DIR_RX) ? handle : NULL);
    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                        (bits == 32) ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)bclk, .ws = (gpio_num_t)ws,
            .dout = (dir == PAL_I2S_DIR_TX) ? (gpio_num_t)data : I2S_GPIO_UNUSED,
            .din  = (dir == PAL_I2S_DIR_RX) ? (gpio_num_t)data : I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };
    i2s_channel_init_std_mode(*handle, &sc);
    i2s_channel_enable(*handle);
    return PAL_OK;
}

pal_err_t pal_i2s_deinit(uint8_t port) {
    if (s_i2s_rx[port]) { i2s_channel_disable(s_i2s_rx[port]); i2s_del_channel(s_i2s_rx[port]); s_i2s_rx[port]=NULL; }
    if (s_i2s_tx[port]) { i2s_channel_disable(s_i2s_tx[port]); i2s_del_channel(s_i2s_tx[port]); s_i2s_tx[port]=NULL; }
    return PAL_OK;
}

pal_err_t pal_i2s_read(uint8_t port, void *buf, size_t len, size_t *bytes_read) {
    if (!s_i2s_rx[port]) return PAL_ERR_INVALID;
    return (i2s_channel_read(s_i2s_rx[port], buf, len, bytes_read, pdMS_TO_TICKS(100)) == ESP_OK)
           ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_i2s_write(uint8_t port, const void *buf, size_t len, size_t *written) {
    if (!s_i2s_tx[port]) return PAL_ERR_INVALID;
    return (i2s_channel_write(s_i2s_tx[port], buf, len, written, pdMS_TO_TICKS(100)) == ESP_OK)
           ? PAL_OK : PAL_ERR_FAIL;
}

/* ── NVS ─────────────────────────────────────────────────────────────────── */

pal_err_t pal_nvs_set(const char *key, const void *data, size_t len) {
    nvs_handle_t h;
    if (nvs_open("parakram", NVS_READWRITE, &h) != ESP_OK) return PAL_ERR_FAIL;
    esp_err_t err = nvs_set_blob(h, key, data, len);
    nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_nvs_get(const char *key, void *data, size_t *len) {
    nvs_handle_t h;
    if (nvs_open("parakram", NVS_READONLY, &h) != ESP_OK) return PAL_ERR_NOT_FOUND;
    esp_err_t err = nvs_get_blob(h, key, data, len);
    nvs_close(h);
    return (err == ESP_OK) ? PAL_OK : PAL_ERR_NOT_FOUND;
}

pal_err_t pal_nvs_erase(const char *key) {
    nvs_handle_t h;
    if (nvs_open("parakram", NVS_READWRITE, &h) != ESP_OK) return PAL_ERR_FAIL;
    nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
    return PAL_OK;
}

/* ── One-Wire (bit-bang — platform-agnostic via GPIO) ────────────────────── */
/* Implemented in hal/pal_onewire.c, shared across all platforms. */
