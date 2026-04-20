/**
 * @file parakram_pal.h
 * @brief Parakram Platform Abstraction Layer (PAL).
 *
 * This is the ONLY hardware header any Parakram driver should include.
 * Every platform (ESP32-S3, RP2040, STM32, Arduino) provides the same
 * functions below. All 59 drivers compile unchanged on all targets.
 *
 * Build system selects the right implementation via PARAKRAM_TARGET_MCU:
 *   cmake -DPARAKRAM_TARGET_MCU=esp32s3 ...
 *   cmake -DPARAKRAM_TARGET_MCU=rp2040 ...
 *   cmake -DPARAKRAM_TARGET_MCU=stm32f4 ...
 *   cmake -DPARAKRAM_TARGET_MCU=arduino ...
 */

#ifndef PARAKRAM_PAL_H
#define PARAKRAM_PAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ─────────────────────────────────────────────────────────── */

#define PAL_OK              0
#define PAL_ERR_FAIL       -1
#define PAL_ERR_INVALID    -2
#define PAL_ERR_TIMEOUT    -3
#define PAL_ERR_BUSY       -4
#define PAL_ERR_NO_MEM     -5
#define PAL_ERR_NOT_FOUND  -6

typedef int pal_err_t;

/* On ESP32 these map directly to esp_err_t (same values). */

/* ── Platform identification ─────────────────────────────────────────────── */

typedef enum {
    PAL_MCU_UNKNOWN  = 0,
    PAL_MCU_ESP32S3  = 1,
    PAL_MCU_RP2040   = 2,
    PAL_MCU_STM32F4  = 3,
    PAL_MCU_ARDUINO  = 4,
} pal_mcu_t;

const char  *pal_get_platform_name(void);
pal_mcu_t   pal_get_mcu(void);
uint32_t    pal_get_cpu_freq_hz(void);

/* ── Timing ──────────────────────────────────────────────────────────────── */

void     pal_delay_us(uint32_t us);
void     pal_delay_ms(uint32_t ms);
uint32_t pal_get_time_ms(void);
uint64_t pal_get_time_us(void);

/* ── GPIO ────────────────────────────────────────────────────────────────── */

typedef enum {
    PAL_GPIO_INPUT        = 0,
    PAL_GPIO_OUTPUT       = 1,
    PAL_GPIO_INPUT_OUTPUT = 2,
} pal_gpio_dir_t;

typedef enum {
    PAL_GPIO_INTR_NONE    = 0,
    PAL_GPIO_INTR_POSEDGE = 1,
    PAL_GPIO_INTR_NEGEDGE = 2,
    PAL_GPIO_INTR_ANYEDGE = 3,
    PAL_GPIO_INTR_LOW     = 4,
    PAL_GPIO_INTR_HIGH    = 5,
} pal_gpio_intr_t;

typedef void (*pal_gpio_isr_t)(void *arg);

pal_err_t pal_gpio_set_direction(int pin, pal_gpio_dir_t dir,
                                  bool pullup, bool pulldown);
pal_err_t pal_gpio_set_level(int pin, int level);
int       pal_gpio_get_level(int pin);
pal_err_t pal_gpio_set_interrupt(int pin, pal_gpio_intr_t edge,
                                  pal_gpio_isr_t cb, void *arg);
pal_err_t pal_gpio_remove_interrupt(int pin);

/* ── I2C ─────────────────────────────────────────────────────────────────── */

#define PAL_I2C_MAX_BUSES   2
#define PAL_I2C_DEFAULT_FREQ 400000  /* 400 kHz */

pal_err_t pal_i2c_init(uint8_t bus, int sda_pin, int scl_pin, uint32_t freq_hz);
pal_err_t pal_i2c_deinit(uint8_t bus);

/**
 * Write a register address then read `len` bytes.
 * This is the primary function used by all I2C sensor drivers.
 */
pal_err_t pal_i2c_read(uint8_t bus, uint8_t addr, uint8_t reg,
                        uint8_t *buf, uint16_t len);

/**
 * Write a register address then write `len` data bytes.
 */
pal_err_t pal_i2c_write(uint8_t bus, uint8_t addr, uint8_t reg,
                         const uint8_t *buf, uint16_t len);

/**
 * Raw write (no register byte) — used by sensors with command-based protocols.
 */
pal_err_t pal_i2c_write_raw(uint8_t bus, uint8_t addr,
                              const uint8_t *buf, uint16_t len);

/**
 * Raw read — used by sensors that auto-increment address after command.
 */
pal_err_t pal_i2c_read_raw(uint8_t bus, uint8_t addr,
                             uint8_t *buf, uint16_t len);

/* ── SPI ─────────────────────────────────────────────────────────────────── */

#define PAL_SPI_MAX_BUSES   2
#define PAL_SPI_MODE_0      0   /* CPOL=0, CPHA=0 */
#define PAL_SPI_MODE_1      1
#define PAL_SPI_MODE_2      2
#define PAL_SPI_MODE_3      3

pal_err_t pal_spi_init(uint8_t bus, int sck, int mosi, int miso,
                        uint32_t speed_hz, uint8_t mode);
pal_err_t pal_spi_deinit(uint8_t bus);

/** Full-duplex transfer with manual CS control. */
pal_err_t pal_spi_transfer(uint8_t bus, int cs_pin,
                             const uint8_t *tx, uint8_t *rx, size_t len);

/** Write-only (rx=NULL). */
pal_err_t pal_spi_write(uint8_t bus, int cs_pin,
                          const uint8_t *tx, size_t len);

/* ── PWM ─────────────────────────────────────────────────────────────────── */

#define PAL_PWM_MAX_CHANNELS   8
#define PAL_PWM_MAX_RESOLUTION 16   /* bits */

pal_err_t pal_pwm_init(uint8_t channel, int pin,
                        uint32_t freq_hz, uint8_t resolution_bits);
pal_err_t pal_pwm_deinit(uint8_t channel);
pal_err_t pal_pwm_set_duty(uint8_t channel, uint32_t duty);
pal_err_t pal_pwm_set_freq(uint8_t channel, uint32_t freq_hz);
uint32_t  pal_pwm_get_max_duty(uint8_t resolution_bits);

/* ── ADC ─────────────────────────────────────────────────────────────────── */

#define PAL_ADC_RESOLUTION_BITS  12
#define PAL_ADC_MAX_VALUE        4095

pal_err_t pal_adc_init(uint8_t channel, int pin);
uint16_t  pal_adc_read(uint8_t channel);    /* returns 0–4095 */
float     pal_adc_read_voltage(uint8_t channel);  /* returns 0.0–3.3 V */

/* ── UART ────────────────────────────────────────────────────────────────── */

#define PAL_UART_MAX_PORTS  3

pal_err_t pal_uart_init(uint8_t port, int tx_pin, int rx_pin, uint32_t baud);
pal_err_t pal_uart_deinit(uint8_t port);
int       pal_uart_available(uint8_t port);
int       pal_uart_read_byte(uint8_t port, uint32_t timeout_ms);
int       pal_uart_read(uint8_t port, uint8_t *buf, size_t len, uint32_t timeout_ms);
pal_err_t pal_uart_write(uint8_t port, const uint8_t *buf, size_t len);

/* ── One-Wire ────────────────────────────────────────────────────────────── */

/** Bit-bang one-wire — implemented in pal_onewire.c (platform-agnostic). */
pal_err_t pal_onewire_reset(int pin);
void      pal_onewire_write_bit(int pin, uint8_t bit);
uint8_t   pal_onewire_read_bit(int pin);
void      pal_onewire_write_byte(int pin, uint8_t byte);
uint8_t   pal_onewire_read_byte(int pin);

/* ── I2S / Audio ─────────────────────────────────────────────────────────── */

typedef enum {
    PAL_I2S_DIR_RX = 0,
    PAL_I2S_DIR_TX = 1,
} pal_i2s_dir_t;

pal_err_t pal_i2s_init(uint8_t port, pal_i2s_dir_t dir,
                        int bclk, int ws, int data,
                        uint32_t sample_rate, uint8_t bits);
pal_err_t pal_i2s_deinit(uint8_t port);
pal_err_t pal_i2s_read(uint8_t port, void *buf, size_t len, size_t *bytes_read);
pal_err_t pal_i2s_write(uint8_t port, const void *buf, size_t len, size_t *bytes_written);

/* ── NVS / Flash storage ─────────────────────────────────────────────────── */

pal_err_t pal_nvs_set(const char *key, const void *data, size_t len);
pal_err_t pal_nvs_get(const char *key, void *data, size_t *len);
pal_err_t pal_nvs_erase(const char *key);

/* ── Logging ─────────────────────────────────────────────────────────────── */

#define PAL_LOG_NONE    0
#define PAL_LOG_ERROR   1
#define PAL_LOG_WARN    2
#define PAL_LOG_INFO    3
#define PAL_LOG_DEBUG   4

void pal_log(int level, const char *tag, const char *fmt, ...);

#define PAL_LOGE(tag, fmt, ...) pal_log(PAL_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define PAL_LOGW(tag, fmt, ...) pal_log(PAL_LOG_WARN,  tag, fmt, ##__VA_ARGS__)
#define PAL_LOGI(tag, fmt, ...) pal_log(PAL_LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#define PAL_LOGD(tag, fmt, ...) pal_log(PAL_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)

/* ── System ──────────────────────────────────────────────────────────────── */

void      pal_reboot(void);
uint32_t  pal_get_free_heap(void);
void      pal_feed_watchdog(void);
pal_err_t pal_init(void);   /* call once from app_main / setup() */

#ifdef __cplusplus
}
#endif

#endif /* PARAKRAM_PAL_H */
