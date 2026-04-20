/**
 * @file pal_impl.c  [RP2040]
 * @brief PAL implementation for Raspberry Pi RP2040 using Pico SDK 1.5+.
 *
 * Build with: cmake -DPARAKRAM_TARGET_MCU=rp2040 ..
 * Requires: pico_sdk_import.cmake in the project root.
 */

#include "parakram_pal.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pico/flash.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ── System ──────────────────────────────────────────────────────────────── */

const char *pal_get_platform_name(void) { return "RP2040"; }
pal_mcu_t   pal_get_mcu(void)           { return PAL_MCU_RP2040; }
uint32_t    pal_get_cpu_freq_hz(void)   { return clock_get_hz(clk_sys); }

pal_err_t pal_init(void) {
    stdio_init_all();
    adc_init();
    return PAL_OK;
}

void pal_reboot(void) { watchdog_reboot(0, SRAM_END, 0); }

uint32_t pal_get_free_heap(void) {
    /* RP2040 has 264KB SRAM; estimate remaining by checking stack */
    extern char __StackLimit, __bss_end__;
    return (uint32_t)(&__StackLimit - &__bss_end__);
}

void pal_feed_watchdog(void) { watchdog_update(); }

/* ── Timing ──────────────────────────────────────────────────────────────── */

void     pal_delay_us(uint32_t us) { sleep_us(us); }
void     pal_delay_ms(uint32_t ms) { sleep_ms(ms); }
uint32_t pal_get_time_ms(void)     { return (uint32_t)(time_us_64() / 1000ULL); }
uint64_t pal_get_time_us(void)     { return time_us_64(); }

/* ── Logging ─────────────────────────────────────────────────────────────── */

static const char *level_str[] = {"", "ERROR", "WARN", "INFO", "DEBUG"};

void pal_log(int level, const char *tag, const char *fmt, ...) {
    if (level < 1 || level > 4) return;
    printf("[%s] (%s) ", level_str[level], tag);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* ── GPIO ────────────────────────────────────────────────────────────────── */

typedef struct { pal_gpio_isr_t cb; void *arg; } rp_isr_t;
static rp_isr_t s_isr[32] = {0};

static void gpio_isr_dispatcher(uint gpio, uint32_t events) {
    if (gpio < 32 && s_isr[gpio].cb) s_isr[gpio].cb(s_isr[gpio].arg);
}

pal_err_t pal_gpio_set_direction(int pin, pal_gpio_dir_t dir, bool pullup, bool pulldown) {
    gpio_init(pin);
    gpio_set_dir(pin, (dir == PAL_GPIO_OUTPUT) ? GPIO_OUT : GPIO_IN);
    gpio_set_pulls(pin, pullup, pulldown);
    return PAL_OK;
}

pal_err_t pal_gpio_set_level(int pin, int level) {
    gpio_put(pin, level);
    return PAL_OK;
}

int pal_gpio_get_level(int pin) { return gpio_get(pin); }

pal_err_t pal_gpio_set_interrupt(int pin, pal_gpio_intr_t edge, pal_gpio_isr_t cb, void *arg) {
    static bool installed = false;
    if (!installed) { gpio_set_irq_enabled_with_callback(pin, 0, false, gpio_isr_dispatcher); installed = true; }
    s_isr[pin] = (rp_isr_t){cb, arg};
    uint32_t events = 0;
    if (edge == PAL_GPIO_INTR_POSEDGE || edge == PAL_GPIO_INTR_ANYEDGE) events |= GPIO_IRQ_EDGE_RISE;
    if (edge == PAL_GPIO_INTR_NEGEDGE || edge == PAL_GPIO_INTR_ANYEDGE) events |= GPIO_IRQ_EDGE_FALL;
    gpio_set_irq_enabled_with_callback(pin, events, true, gpio_isr_dispatcher);
    return PAL_OK;
}

pal_err_t pal_gpio_remove_interrupt(int pin) {
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    s_isr[pin].cb = NULL;
    return PAL_OK;
}

/* ── I2C ─────────────────────────────────────────────────────────────────── */

static i2c_inst_t *i2c_inst[2] = {i2c0, i2c1};
static bool s_i2c_init[2] = {false};

pal_err_t pal_i2c_init(uint8_t bus, int sda, int scl, uint32_t freq_hz) {
    if (bus >= 2) return PAL_ERR_INVALID;
    if (s_i2c_init[bus]) return PAL_OK;
    i2c_init(i2c_inst[bus], freq_hz);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    s_i2c_init[bus] = true;
    return PAL_OK;
}

pal_err_t pal_i2c_deinit(uint8_t bus) {
    if (bus >= 2) return PAL_ERR_INVALID;
    i2c_deinit(i2c_inst[bus]);
    s_i2c_init[bus] = false;
    return PAL_OK;
}

pal_err_t pal_i2c_read(uint8_t bus, uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len) {
    if (bus >= 2 || !s_i2c_init[bus]) return PAL_ERR_INVALID;
    /* Write register address, then read data */
    int r = i2c_write_blocking(i2c_inst[bus], addr, &reg, 1, true);
    if (r < 0) return PAL_ERR_FAIL;
    r = i2c_read_blocking(i2c_inst[bus], addr, buf, len, false);
    return (r == (int)len) ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_i2c_write(uint8_t bus, uint8_t addr, uint8_t reg, const uint8_t *buf, uint16_t len) {
    if (bus >= 2 || !s_i2c_init[bus]) return PAL_ERR_INVALID;
    uint8_t tmp[len + 1];
    tmp[0] = reg;
    memcpy(&tmp[1], buf, len);
    int r = i2c_write_blocking(i2c_inst[bus], addr, tmp, len + 1, false);
    return (r == (int)(len + 1)) ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_i2c_write_raw(uint8_t bus, uint8_t addr, const uint8_t *buf, uint16_t len) {
    if (bus >= 2 || !s_i2c_init[bus]) return PAL_ERR_INVALID;
    int r = i2c_write_blocking(i2c_inst[bus], addr, buf, len, false);
    return (r == (int)len) ? PAL_OK : PAL_ERR_FAIL;
}

pal_err_t pal_i2c_read_raw(uint8_t bus, uint8_t addr, uint8_t *buf, uint16_t len) {
    if (bus >= 2 || !s_i2c_init[bus]) return PAL_ERR_INVALID;
    int r = i2c_read_blocking(i2c_inst[bus], addr, buf, len, false);
    return (r == (int)len) ? PAL_OK : PAL_ERR_FAIL;
}

/* ── SPI ─────────────────────────────────────────────────────────────────── */

static spi_inst_t *spi_inst_arr[2] = {spi0, spi1};

pal_err_t pal_spi_init(uint8_t bus, int sck, int mosi, int miso, uint32_t speed_hz, uint8_t mode) {
    if (bus >= 2) return PAL_ERR_INVALID;
    spi_init(spi_inst_arr[bus], speed_hz);
    spi_set_format(spi_inst_arr[bus], 8, (mode >> 1) & 1, mode & 1, SPI_MSB_FIRST);
    gpio_set_function(sck, GPIO_FUNC_SPI);
    gpio_set_function(mosi, GPIO_FUNC_SPI);
    if (miso >= 0) gpio_set_function(miso, GPIO_FUNC_SPI);
    return PAL_OK;
}

pal_err_t pal_spi_deinit(uint8_t bus) { spi_deinit(spi_inst_arr[bus]); return PAL_OK; }

pal_err_t pal_spi_transfer(uint8_t bus, int cs, const uint8_t *tx, uint8_t *rx, size_t len) {
    gpio_put(cs, 0);
    spi_write_read_blocking(spi_inst_arr[bus], tx, rx, len);
    gpio_put(cs, 1);
    return PAL_OK;
}

pal_err_t pal_spi_write(uint8_t bus, int cs, const uint8_t *tx, size_t len) {
    gpio_put(cs, 0);
    spi_write_blocking(spi_inst_arr[bus], tx, len);
    gpio_put(cs, 1);
    return PAL_OK;
}

/* ── PWM ─────────────────────────────────────────────────────────────────── */

typedef struct { uint slice; uint channel; uint8_t res; } pwm_ch_rp_t;
static pwm_ch_rp_t s_pwm[PAL_PWM_MAX_CHANNELS];

pal_err_t pal_pwm_init(uint8_t ch, int pin, uint32_t freq_hz, uint8_t res_bits) {
    if (ch >= PAL_PWM_MAX_CHANNELS) return PAL_ERR_INVALID;
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    uint chn   = pwm_gpio_to_channel(pin);
    uint32_t top = clock_get_hz(clk_sys) / freq_hz - 1;
    if (top > 65535) top = 65535;
    pwm_set_wrap(slice, (uint16_t)top);
    pwm_set_chan_level(slice, chn, 0);
    pwm_set_enabled(slice, true);
    s_pwm[ch] = (pwm_ch_rp_t){slice, chn, res_bits};
    return PAL_OK;
}

pal_err_t pal_pwm_deinit(uint8_t ch) {
    if (ch >= PAL_PWM_MAX_CHANNELS) return PAL_ERR_INVALID;
    pwm_set_enabled(s_pwm[ch].slice, false);
    return PAL_OK;
}

pal_err_t pal_pwm_set_duty(uint8_t ch, uint32_t duty) {
    if (ch >= PAL_PWM_MAX_CHANNELS) return PAL_ERR_INVALID;
    /* Scale duty from 0..(2^res-1) to 0..wrap */
    uint16_t wrap = pwm_hw->slice[s_pwm[ch].slice].top;
    uint16_t level = (uint16_t)((uint64_t)duty * wrap / ((1u << s_pwm[ch].res) - 1));
    pwm_set_chan_level(s_pwm[ch].slice, s_pwm[ch].channel, level);
    return PAL_OK;
}

pal_err_t pal_pwm_set_freq(uint8_t ch, uint32_t freq_hz) {
    if (ch >= PAL_PWM_MAX_CHANNELS) return PAL_ERR_INVALID;
    uint32_t top = clock_get_hz(clk_sys) / freq_hz - 1;
    if (top > 65535) top = 65535;
    pwm_set_wrap(s_pwm[ch].slice, (uint16_t)top);
    return PAL_OK;
}

uint32_t pal_pwm_get_max_duty(uint8_t res) { return (1u << res) - 1; }

/* ── ADC ─────────────────────────────────────────────────────────────────── */

static bool s_adc_pin_init[5] = {false};

pal_err_t pal_adc_init(uint8_t channel, int pin) {
    if (!s_adc_pin_init[channel]) {
        adc_gpio_init(pin);
        s_adc_pin_init[channel] = true;
    }
    return PAL_OK;
}

uint16_t pal_adc_read(uint8_t channel) {
    adc_select_input(channel);
    return adc_read(); /* 12-bit, 0-4095 */
}

float pal_adc_read_voltage(uint8_t channel) { return pal_adc_read(channel) * 3.3f / 4095.0f; }

/* ── UART ────────────────────────────────────────────────────────────────── */

static uart_inst_t *uart_inst_arr[2] = {uart0, uart1};
static bool s_uart_init[2] = {false};

pal_err_t pal_uart_init(uint8_t port, int tx, int rx, uint32_t baud) {
    if (port >= 2) return PAL_ERR_INVALID;
    uart_init(uart_inst_arr[port], baud);
    gpio_set_function(tx, GPIO_FUNC_UART);
    gpio_set_function(rx, GPIO_FUNC_UART);
    s_uart_init[port] = true;
    return PAL_OK;
}

pal_err_t pal_uart_deinit(uint8_t port) {
    uart_deinit(uart_inst_arr[port]);
    s_uart_init[port] = false;
    return PAL_OK;
}

int pal_uart_available(uint8_t port) {
    return uart_is_readable(uart_inst_arr[port]) ? 1 : 0;
}

int pal_uart_read_byte(uint8_t port, uint32_t timeout_ms) {
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000;
    while (time_us_64() < deadline) {
        if (uart_is_readable(uart_inst_arr[port])) return uart_getc(uart_inst_arr[port]);
        sleep_us(100);
    }
    return -1;
}

int pal_uart_read(uint8_t port, uint8_t *buf, size_t len, uint32_t timeout_ms) {
    size_t n = 0;
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000;
    while (n < len && time_us_64() < deadline) {
        if (uart_is_readable(uart_inst_arr[port])) buf[n++] = uart_getc(uart_inst_arr[port]);
    }
    return (int)n;
}

pal_err_t pal_uart_write(uint8_t port, const uint8_t *buf, size_t len) {
    uart_write_blocking(uart_inst_arr[port], buf, len);
    return PAL_OK;
}

/* ── I2S / NVS / One-Wire ────────────────────────────────────────────────── */
/* RP2040 uses PIO for I2S — stub for now, PIO programs added in next iteration */

pal_err_t pal_i2s_init(uint8_t p, pal_i2s_dir_t d, int b, int w, int dat, uint32_t sr, uint8_t bits) {
    (void)p; (void)d; (void)b; (void)w; (void)dat; (void)sr; (void)bits;
    return PAL_ERR_NOT_FOUND; /* PIO I2S to be added */
}
pal_err_t pal_i2s_deinit(uint8_t p) { (void)p; return PAL_OK; }
pal_err_t pal_i2s_read(uint8_t p, void *buf, size_t len, size_t *n) { (void)p;(void)buf;(void)len;*n=0;return PAL_ERR_NOT_FOUND; }
pal_err_t pal_i2s_write(uint8_t p, const void *buf, size_t len, size_t *n) { (void)p;(void)buf;(void)len;*n=0;return PAL_ERR_NOT_FOUND; }

/* NVS backed by flash at last 4KB sector */
#define NVS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - 4096)
static uint8_t s_nvs_shadow[4096];
static bool    s_nvs_loaded = false;

static void nvs_load(void) {
    if (!s_nvs_loaded) {
        memcpy(s_nvs_shadow, (const uint8_t *)(XIP_BASE + NVS_FLASH_OFFSET), 4096);
        s_nvs_loaded = true;
    }
}

pal_err_t pal_nvs_set(const char *key, const void *data, size_t len) {
    nvs_load();
    /* Simple key=value store: 32-byte key + 2-byte length + data, packed */
    uint8_t *p = s_nvs_shadow;
    while (p < s_nvs_shadow + 4096 - 35) {
        if (*p == 0xFF) { /* empty slot */
            strncpy((char *)p, key, 32);
            p[32] = (uint8_t)(len >> 8);
            p[33] = (uint8_t)(len & 0xFF);
            memcpy(p + 34, data, len);
            uint32_t ints = save_and_disable_interrupts();
            flash_range_erase(NVS_FLASH_OFFSET, 4096);
            flash_range_program(NVS_FLASH_OFFSET, s_nvs_shadow, 4096);
            restore_interrupts(ints);
            return PAL_OK;
        }
        uint16_t entry_len = ((uint16_t)p[32] << 8) | p[33];
        p += 34 + entry_len;
    }
    return PAL_ERR_NO_MEM;
}

pal_err_t pal_nvs_get(const char *key, void *data, size_t *len) {
    nvs_load();
    uint8_t *p = s_nvs_shadow;
    while (p < s_nvs_shadow + 4096 - 35) {
        if (*p == 0xFF) break;
        if (strncmp((char *)p, key, 32) == 0) {
            uint16_t entry_len = ((uint16_t)p[32] << 8) | p[33];
            if (*len >= entry_len) { memcpy(data, p + 34, entry_len); *len = entry_len; return PAL_OK; }
            return PAL_ERR_NO_MEM;
        }
        uint16_t entry_len = ((uint16_t)p[32] << 8) | p[33];
        p += 34 + entry_len;
    }
    return PAL_ERR_NOT_FOUND;
}

pal_err_t pal_nvs_erase(const char *key) { (void)key; return PAL_OK; }
