/**
 * @file pal_impl.cpp
 * @brief Arduino framework implementation of the Parakram Platform Abstraction Layer.
 *
 * Targets: AVR (Uno, Mega), ESP32 Arduino, RP2040 Arduino, STM32 Arduino.
 * Build with Arduino IDE or PlatformIO. The PAL header (parakram_pal.h) is a
 * pure C header; all functions here are exported extern "C" to match it.
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <EEPROM.h>
#include <stdarg.h>
#include <string.h>

#include "../include/parakram_pal.h"

/* AVR watchdog support */
#if defined(__AVR__)
#  include <avr/wdt.h>
#endif

/* ESP32: heap query */
#if defined(ESP32)
#  include "esp_heap_caps.h"
#endif

/* ── Utilities ─────────────────────────────────────────────────────────────── */

/** Select TwoWire instance by bus index. */
static TwoWire *wire_bus(uint8_t bus)
{
#if defined(WIRE_HAS_END) && defined(WIRE1_HAS_END)
    /* Board defines Wire1 */
    return (bus == 1) ? &Wire1 : &Wire;
#elif defined(Wire1)
    return (bus == 1) ? &Wire1 : &Wire;
#else
    (void)bus;
    return &Wire;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* Platform identification                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

extern "C" const char *pal_get_platform_name(void)
{
    return "Arduino";
}

extern "C" pal_mcu_t pal_get_mcu(void)
{
    return PAL_MCU_ARDUINO;
}

extern "C" uint32_t pal_get_cpu_freq_hz(void)
{
    return (uint32_t)F_CPU;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* Timing                                                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

extern "C" void pal_delay_us(uint32_t us)
{
    delayMicroseconds(us);
}

extern "C" void pal_delay_ms(uint32_t ms)
{
    delay(ms);
}

extern "C" uint32_t pal_get_time_ms(void)
{
    return (uint32_t)millis();
}

extern "C" uint64_t pal_get_time_us(void)
{
    return (uint64_t)micros();
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* GPIO                                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

extern "C" pal_err_t pal_gpio_set_direction(int pin, pal_gpio_dir_t dir,
                                             bool pullup, bool pulldown)
{
    switch (dir) {
    case PAL_GPIO_OUTPUT:
    case PAL_GPIO_INPUT_OUTPUT:
        pinMode((uint8_t)pin, OUTPUT);
        break;

    case PAL_GPIO_INPUT:
        if (pullup) {
            pinMode((uint8_t)pin, INPUT_PULLUP);
        } else if (pulldown) {
#if defined(INPUT_PULLDOWN)
            pinMode((uint8_t)pin, INPUT_PULLDOWN);
#else
            pinMode((uint8_t)pin, INPUT);
#endif
        } else {
            pinMode((uint8_t)pin, INPUT);
        }
        break;

    default:
        return PAL_ERR_INVALID;
    }
    return PAL_OK;
}

extern "C" pal_err_t pal_gpio_set_level(int pin, int level)
{
    digitalWrite((uint8_t)pin, level ? HIGH : LOW);
    return PAL_OK;
}

extern "C" int pal_gpio_get_level(int pin)
{
    return (int)digitalRead((uint8_t)pin);
}

/* ── Interrupt support ──────────────────────────────────────────────────────
 *
 * Arduino's attachInterrupt() only accepts a plain void(void) function
 * pointer — no user argument. We solve this with a small static dispatch
 * table (up to 8 slots) and 8 concrete ISR wrapper functions.
 */

#define PAL_ISR_SLOTS 8

struct PalIsrSlot {
    pal_gpio_isr_t cb;
    void          *arg;
    int            pin;
    bool           used;
};

static PalIsrSlot s_isr_table[PAL_ISR_SLOTS];

/* One concrete ISR per slot — dispatches to the registered callback. */
#define DEFINE_ISR_WRAPPER(N)                          \
    static void isr_wrapper_##N(void)                  \
    {                                                   \
        if (s_isr_table[N].cb)                         \
            s_isr_table[N].cb(s_isr_table[N].arg);     \
    }

DEFINE_ISR_WRAPPER(0)
DEFINE_ISR_WRAPPER(1)
DEFINE_ISR_WRAPPER(2)
DEFINE_ISR_WRAPPER(3)
DEFINE_ISR_WRAPPER(4)
DEFINE_ISR_WRAPPER(5)
DEFINE_ISR_WRAPPER(6)
DEFINE_ISR_WRAPPER(7)

typedef void (*voidfunc_t)(void);

static const voidfunc_t s_isr_wrappers[PAL_ISR_SLOTS] = {
    isr_wrapper_0, isr_wrapper_1, isr_wrapper_2, isr_wrapper_3,
    isr_wrapper_4, isr_wrapper_5, isr_wrapper_6, isr_wrapper_7,
};

/** Convert PAL interrupt type to Arduino mode constant. */
static int pal_intr_to_arduino_mode(pal_gpio_intr_t edge)
{
    switch (edge) {
    case PAL_GPIO_INTR_POSEDGE: return RISING;
    case PAL_GPIO_INTR_NEGEDGE: return FALLING;
    case PAL_GPIO_INTR_ANYEDGE: return CHANGE;
    case PAL_GPIO_INTR_LOW:     return LOW;
    case PAL_GPIO_INTR_HIGH:    return HIGH;
    default:                    return CHANGE;
    }
}

extern "C" pal_err_t pal_gpio_set_interrupt(int pin, pal_gpio_intr_t edge,
                                              pal_gpio_isr_t cb, void *arg)
{
    /* Find an existing slot for this pin, or claim a free one. */
    int slot = -1;
    for (int i = 0; i < PAL_ISR_SLOTS; i++) {
        if (s_isr_table[i].used && s_isr_table[i].pin == pin) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < PAL_ISR_SLOTS; i++) {
            if (!s_isr_table[i].used) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        return PAL_ERR_NO_MEM;
    }

    s_isr_table[slot].cb   = cb;
    s_isr_table[slot].arg  = arg;
    s_isr_table[slot].pin  = pin;
    s_isr_table[slot].used = true;

    int ardu_intr = digitalPinToInterrupt((uint8_t)pin);
    if (ardu_intr < 0) {
        return PAL_ERR_INVALID;
    }

    attachInterrupt((uint8_t)ardu_intr,
                    s_isr_wrappers[slot],
                    pal_intr_to_arduino_mode(edge));
    return PAL_OK;
}

extern "C" pal_err_t pal_gpio_remove_interrupt(int pin)
{
    int ardu_intr = digitalPinToInterrupt((uint8_t)pin);
    if (ardu_intr >= 0) {
        detachInterrupt((uint8_t)ardu_intr);
    }
    for (int i = 0; i < PAL_ISR_SLOTS; i++) {
        if (s_isr_table[i].used && s_isr_table[i].pin == pin) {
            s_isr_table[i].cb   = nullptr;
            s_isr_table[i].arg  = nullptr;
            s_isr_table[i].used = false;
        }
    }
    return PAL_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* I2C                                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

extern "C" pal_err_t pal_i2c_init(uint8_t bus, int sda_pin, int scl_pin,
                                    uint32_t freq_hz)
{
    TwoWire *w = wire_bus(bus);
    if (sda_pin >= 0 && scl_pin >= 0) {
#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(STM32)
        w->begin((int)sda_pin, (int)scl_pin);
#else
        /* AVR Wire.begin() takes no pin arguments; pins are fixed by hardware. */
        (void)sda_pin;
        (void)scl_pin;
        w->begin();
#endif
    } else {
        w->begin();
    }
    if (freq_hz > 0) {
        w->setClock(freq_hz);
    }
    return PAL_OK;
}

extern "C" pal_err_t pal_i2c_deinit(uint8_t bus)
{
#if defined(WIRE_HAS_END)
    wire_bus(bus)->end();
#else
    (void)bus;
#endif
    return PAL_OK;
}

extern "C" pal_err_t pal_i2c_read(uint8_t bus, uint8_t addr, uint8_t reg,
                                    uint8_t *buf, uint16_t len)
{
    TwoWire *w = wire_bus(bus);

    w->beginTransmission(addr);
    w->write(reg);
    uint8_t err = w->endTransmission(false);
    if (err != 0) {
        return PAL_ERR_FAIL;
    }

    uint8_t got = w->requestFrom((uint8_t)addr, (uint8_t)len);
    if (got != len) {
        return PAL_ERR_FAIL;
    }
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)w->read();
    }
    return PAL_OK;
}

extern "C" pal_err_t pal_i2c_write(uint8_t bus, uint8_t addr, uint8_t reg,
                                     const uint8_t *buf, uint16_t len)
{
    TwoWire *w = wire_bus(bus);

    w->beginTransmission(addr);
    w->write(reg);
    for (uint16_t i = 0; i < len; i++) {
        w->write(buf[i]);
    }
    uint8_t err = w->endTransmission();
    return (err == 0) ? PAL_OK : PAL_ERR_FAIL;
}

extern "C" pal_err_t pal_i2c_write_raw(uint8_t bus, uint8_t addr,
                                         const uint8_t *buf, uint16_t len)
{
    TwoWire *w = wire_bus(bus);

    w->beginTransmission(addr);
    for (uint16_t i = 0; i < len; i++) {
        w->write(buf[i]);
    }
    uint8_t err = w->endTransmission();
    return (err == 0) ? PAL_OK : PAL_ERR_FAIL;
}

extern "C" pal_err_t pal_i2c_read_raw(uint8_t bus, uint8_t addr,
                                        uint8_t *buf, uint16_t len)
{
    TwoWire *w = wire_bus(bus);

    uint8_t got = w->requestFrom((uint8_t)addr, (uint8_t)len);
    if (got != len) {
        return PAL_ERR_FAIL;
    }
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)w->read();
    }
    return PAL_OK;
}

/* ── i2c_bus_read / i2c_bus_write ── extern "C" wrappers used by all 59 drivers */

extern "C" int i2c_bus_read(uint8_t bus, uint8_t addr, uint8_t reg,
                              uint8_t *buf, uint16_t len)
{
    return pal_i2c_read(bus, addr, reg, buf, len);
}

extern "C" int i2c_bus_write(uint8_t bus, uint8_t addr, uint8_t reg,
                               const uint8_t *buf, uint16_t len)
{
    return pal_i2c_write(bus, addr, reg, buf, len);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* SPI                                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

static SPISettings s_spi_settings[PAL_SPI_MAX_BUSES];

extern "C" pal_err_t pal_spi_init(uint8_t bus, int sck, int mosi, int miso,
                                    uint32_t speed_hz, uint8_t mode)
{
    if (bus >= PAL_SPI_MAX_BUSES) {
        return PAL_ERR_INVALID;
    }

    uint8_t spi_mode;
    switch (mode) {
    case PAL_SPI_MODE_0: spi_mode = SPI_MODE0; break;
    case PAL_SPI_MODE_1: spi_mode = SPI_MODE1; break;
    case PAL_SPI_MODE_2: spi_mode = SPI_MODE2; break;
    case PAL_SPI_MODE_3: spi_mode = SPI_MODE3; break;
    default:             spi_mode = SPI_MODE0; break;
    }

    s_spi_settings[bus] = SPISettings(speed_hz, MSBFIRST, spi_mode);

#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
    /* Boards that accept explicit pin arguments. */
    if (sck >= 0 && mosi >= 0 && miso >= 0) {
        SPI.begin((int8_t)sck, (int8_t)miso, (int8_t)mosi, (int8_t)-1);
    } else {
        SPI.begin();
    }
#else
    (void)sck; (void)mosi; (void)miso;
    SPI.begin();
#endif

    return PAL_OK;
}

extern "C" pal_err_t pal_spi_deinit(uint8_t bus)
{
    (void)bus;
    SPI.end();
    return PAL_OK;
}

extern "C" pal_err_t pal_spi_transfer(uint8_t bus, int cs_pin,
                                        const uint8_t *tx, uint8_t *rx,
                                        size_t len)
{
    if (bus >= PAL_SPI_MAX_BUSES) {
        return PAL_ERR_INVALID;
    }
    if (cs_pin >= 0) {
        pinMode((uint8_t)cs_pin, OUTPUT);
        digitalWrite((uint8_t)cs_pin, LOW);
    }
    SPI.beginTransaction(s_spi_settings[bus]);
    for (size_t i = 0; i < len; i++) {
        uint8_t out = tx ? tx[i] : 0xFF;
        uint8_t in  = SPI.transfer(out);
        if (rx) {
            rx[i] = in;
        }
    }
    SPI.endTransaction();
    if (cs_pin >= 0) {
        digitalWrite((uint8_t)cs_pin, HIGH);
    }
    return PAL_OK;
}

extern "C" pal_err_t pal_spi_write(uint8_t bus, int cs_pin,
                                     const uint8_t *tx, size_t len)
{
    return pal_spi_transfer(bus, cs_pin, tx, nullptr, len);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* PWM                                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

struct PalPwmChannel {
    int      pin;
    uint8_t  resolution_bits;
    uint32_t freq_hz;
    bool     used;
};

static PalPwmChannel s_pwm_channels[PAL_PWM_MAX_CHANNELS];

extern "C" pal_err_t pal_pwm_init(uint8_t channel, int pin,
                                    uint32_t freq_hz, uint8_t resolution_bits)
{
    if (channel >= PAL_PWM_MAX_CHANNELS) {
        return PAL_ERR_INVALID;
    }
    s_pwm_channels[channel].pin             = pin;
    s_pwm_channels[channel].resolution_bits = resolution_bits;
    s_pwm_channels[channel].freq_hz         = freq_hz;
    s_pwm_channels[channel].used            = true;

    pinMode((uint8_t)pin, OUTPUT);

#if defined(ESP32)
    /* ESP32 Arduino SDK: configure LEDC channel. */
    ledcSetup((uint8_t)channel, freq_hz, resolution_bits);
    ledcAttachPin((uint8_t)pin, (uint8_t)channel);
#endif

    return PAL_OK;
}

extern "C" pal_err_t pal_pwm_deinit(uint8_t channel)
{
    if (channel >= PAL_PWM_MAX_CHANNELS || !s_pwm_channels[channel].used) {
        return PAL_ERR_INVALID;
    }
#if defined(ESP32)
    ledcDetachPin((uint8_t)s_pwm_channels[channel].pin);
#endif
    s_pwm_channels[channel].used = false;
    return PAL_OK;
}

extern "C" pal_err_t pal_pwm_set_duty(uint8_t channel, uint32_t duty)
{
    if (channel >= PAL_PWM_MAX_CHANNELS || !s_pwm_channels[channel].used) {
        return PAL_ERR_INVALID;
    }
    int pin = s_pwm_channels[channel].pin;

#if defined(ESP32)
    ledcWrite((uint8_t)channel, duty);
#else
    /* Arduino analogWrite() is 8-bit (0-255). Scale duty down. */
    uint8_t resolution = s_pwm_channels[channel].resolution_bits;
    uint8_t val8;
    if (resolution > 8) {
        val8 = (uint8_t)(duty >> (resolution - 8));
    } else if (resolution < 8) {
        val8 = (uint8_t)(duty << (8 - resolution));
    } else {
        val8 = (uint8_t)duty;
    }
    analogWrite((uint8_t)pin, val8);
#endif

    return PAL_OK;
}

extern "C" pal_err_t pal_pwm_set_freq(uint8_t channel, uint32_t freq_hz)
{
    if (channel >= PAL_PWM_MAX_CHANNELS || !s_pwm_channels[channel].used) {
        return PAL_ERR_INVALID;
    }
    s_pwm_channels[channel].freq_hz = freq_hz;

#if defined(ESP32)
    ledcSetup((uint8_t)channel, freq_hz,
              s_pwm_channels[channel].resolution_bits);
    ledcAttachPin((uint8_t)s_pwm_channels[channel].pin, (uint8_t)channel);
#endif
    /* On non-ESP32 Arduino, PWM frequency is fixed by hardware timer;
       changing it requires timer register manipulation that is board-specific.
       Store the new value for reference but take no action. */
    return PAL_OK;
}

extern "C" uint32_t pal_pwm_get_max_duty(uint8_t resolution_bits)
{
    return (uint32_t)((1UL << resolution_bits) - 1UL);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* ADC                                                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

struct PalAdcChannel {
    int  pin;
    bool used;
};

static PalAdcChannel s_adc_channels[8];

extern "C" pal_err_t pal_adc_init(uint8_t channel, int pin)
{
    if (channel >= 8) {
        return PAL_ERR_INVALID;
    }
    s_adc_channels[channel].pin  = pin;
    s_adc_channels[channel].used = true;
    pinMode((uint8_t)pin, INPUT);
    return PAL_OK;
}

extern "C" uint16_t pal_adc_read(uint8_t channel)
{
    if (channel >= 8 || !s_adc_channels[channel].used) {
        return 0;
    }
    int raw = analogRead((uint8_t)s_adc_channels[channel].pin);

#if defined(ESP32)
    /* ESP32 Arduino returns 0–4095 natively (12-bit). */
    return (uint16_t)raw;
#else
    /* Most Arduino boards return 0–1023 (10-bit). Scale to 0–4095. */
    return (uint16_t)((raw * 4095UL) / 1023UL);
#endif
}

extern "C" float pal_adc_read_voltage(uint8_t channel)
{
    return (float)pal_adc_read(channel) / 4095.0f * 3.3f;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* UART                                                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

/** Return a pointer to the HardwareSerial object for the given port. */
static HardwareSerial *uart_port(uint8_t port)
{
    switch (port) {
#if defined(HAVE_HWSERIAL1) || defined(Serial1)
    case 1: return &Serial1;
#endif
#if defined(HAVE_HWSERIAL2) || defined(Serial2)
    case 2: return &Serial2;
#endif
    default: return &Serial;
    }
}

extern "C" pal_err_t pal_uart_init(uint8_t port, int tx_pin, int rx_pin,
                                     uint32_t baud)
{
    HardwareSerial *s = uart_port(port);
    if (!s) {
        return PAL_ERR_INVALID;
    }

#if defined(ESP32)
    if (tx_pin >= 0 && rx_pin >= 0) {
        s->begin(baud, SERIAL_8N1, rx_pin, tx_pin);
    } else {
        s->begin(baud);
    }
#else
    (void)tx_pin; (void)rx_pin;
    s->begin(baud);
#endif

    return PAL_OK;
}

extern "C" pal_err_t pal_uart_deinit(uint8_t port)
{
    HardwareSerial *s = uart_port(port);
    if (!s) {
        return PAL_ERR_INVALID;
    }
    s->end();
    return PAL_OK;
}

extern "C" int pal_uart_available(uint8_t port)
{
    HardwareSerial *s = uart_port(port);
    if (!s) {
        return 0;
    }
    return s->available();
}

extern "C" int pal_uart_read_byte(uint8_t port, uint32_t timeout_ms)
{
    HardwareSerial *s = uart_port(port);
    if (!s) {
        return -1;
    }
    uint32_t deadline = millis() + timeout_ms;
    while (!s->available()) {
        if (millis() >= deadline) {
            return -1;
        }
    }
    return s->read();
}

extern "C" int pal_uart_read(uint8_t port, uint8_t *buf, size_t len,
                               uint32_t timeout_ms)
{
    HardwareSerial *s = uart_port(port);
    if (!s || !buf) {
        return PAL_ERR_INVALID;
    }
    uint32_t deadline = millis() + timeout_ms;
    size_t   count    = 0;
    while (count < len && millis() < deadline) {
        if (s->available()) {
            buf[count++] = (uint8_t)s->read();
        }
    }
    return (int)count;
}

extern "C" pal_err_t pal_uart_write(uint8_t port, const uint8_t *buf,
                                      size_t len)
{
    HardwareSerial *s = uart_port(port);
    if (!s || !buf) {
        return PAL_ERR_INVALID;
    }
    size_t written = s->write(buf, len);
    return (written == len) ? PAL_OK : PAL_ERR_FAIL;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* I2S — board-specific; stub returns PAL_ERR_NOT_FOUND                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Arduino I2S APIs differ significantly between targets (ESP32 uses
 * esp_i2s / I2S.h, RP2040 uses PIO-based I2S, STM32 uses SAI peripheral).
 * Use the board-specific I2S library for your target instead of this PAL
 * wrapper. These stubs preserve link-time compatibility for drivers that
 * optionally call the I2S layer.
 */

extern "C" pal_err_t pal_i2s_init(uint8_t port, pal_i2s_dir_t dir,
                                    int bclk, int ws, int data,
                                    uint32_t sample_rate, uint8_t bits)
{
    (void)port; (void)dir; (void)bclk; (void)ws;
    (void)data; (void)sample_rate; (void)bits;
    return PAL_ERR_NOT_FOUND;
}

extern "C" pal_err_t pal_i2s_deinit(uint8_t port)
{
    (void)port;
    return PAL_ERR_NOT_FOUND;
}

extern "C" pal_err_t pal_i2s_read(uint8_t port, void *buf, size_t len,
                                    size_t *bytes_read)
{
    (void)port; (void)buf; (void)len;
    if (bytes_read) *bytes_read = 0;
    return PAL_ERR_NOT_FOUND;
}

extern "C" pal_err_t pal_i2s_write(uint8_t port, const void *buf, size_t len,
                                     size_t *bytes_written)
{
    (void)port; (void)buf; (void)len;
    if (bytes_written) *bytes_written = 0;
    return PAL_ERR_NOT_FOUND;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* NVS — EEPROM-backed key-value store                                         */
/* ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Layout in EEPROM (starts at address 0):
 *
 *   [key_len : 1 byte] [key : key_len bytes]
 *   [val_len_lo : 1 byte] [val_len_hi : 1 byte] [value : val_len bytes]
 *   ... repeated for each entry ...
 *   [0x00] — sentinel marks end of used region
 *
 * key_len == 0 means the slot has been erased (skip to next).
 * A raw 0xFF byte at any key_len position indicates unused EEPROM.
 */

#define PAL_NVS_EEPROM_SIZE 512

/** Advance cursor past one record, return address of next record or -1. */
static int nvs_next_record(int addr)
{
    if (addr < 0 || addr >= PAL_NVS_EEPROM_SIZE) {
        return -1;
    }
    uint8_t klen = EEPROM.read(addr);
    if (klen == 0xFF) {
        /* Unwritten region — end of store. */
        return -1;
    }
    addr += 1 + (int)klen;               /* skip key_len byte + key bytes */
    if (addr + 2 > PAL_NVS_EEPROM_SIZE) {
        return -1;
    }
    uint8_t vlo = EEPROM.read(addr);
    uint8_t vhi = EEPROM.read(addr + 1);
    uint16_t vlen = (uint16_t)vlo | ((uint16_t)vhi << 8);
    addr += 2 + (int)vlen;               /* skip val_len bytes + value bytes */
    if (addr > PAL_NVS_EEPROM_SIZE) {
        return -1;
    }
    return addr;
}

/**
 * Scan EEPROM for a key. Returns the address of the key_len byte on match,
 * or -1 if not found. Sets *val_addr to the start of the value (after the
 * two length bytes) and *val_len to the stored value length.
 */
static int nvs_find_key(const char *key, int *val_addr, uint16_t *val_len)
{
    size_t klen = strlen(key);
    int addr = 0;

    while (addr >= 0 && addr < PAL_NVS_EEPROM_SIZE) {
        uint8_t stored_klen = EEPROM.read(addr);
        if (stored_klen == 0xFF) {
            break; /* end of store */
        }
        if (stored_klen == 0) {
            /* Erased slot — still need to skip it properly.
               An erased slot stores 0x00 key_len followed by val bytes.
               We stored the val_len pair at addr+1 even for erased slots. */
            if (addr + 3 > PAL_NVS_EEPROM_SIZE) break;
            uint8_t vlo = EEPROM.read(addr + 1);
            uint8_t vhi = EEPROM.read(addr + 2);
            uint16_t vlen = (uint16_t)vlo | ((uint16_t)vhi << 8);
            addr += 3 + (int)vlen;
            continue;
        }

        /* Compare key. */
        bool match = (stored_klen == (uint8_t)klen);
        if (match) {
            for (size_t i = 0; i < klen && match; i++) {
                if (EEPROM.read(addr + 1 + (int)i) != (uint8_t)key[i]) {
                    match = false;
                }
            }
        }

        int vlen_addr = addr + 1 + (int)stored_klen;
        if (vlen_addr + 2 > PAL_NVS_EEPROM_SIZE) {
            break;
        }
        uint8_t vlo = EEPROM.read(vlen_addr);
        uint8_t vhi = EEPROM.read(vlen_addr + 1);
        uint16_t vlen = (uint16_t)vlo | ((uint16_t)vhi << 8);

        if (match) {
            if (val_addr) *val_addr = vlen_addr + 2;
            if (val_len)  *val_len  = vlen;
            return addr;
        }

        addr = vlen_addr + 2 + (int)vlen;
    }
    return -1;
}

/** Find the first free address after all valid records. */
static int nvs_find_end(void)
{
    int addr = 0;
    while (addr >= 0 && addr < PAL_NVS_EEPROM_SIZE) {
        uint8_t klen = EEPROM.read(addr);
        if (klen == 0xFF) {
            return addr; /* first unwritten byte */
        }
        if (klen == 0) {
            /* Erased slot. */
            if (addr + 3 > PAL_NVS_EEPROM_SIZE) return PAL_NVS_EEPROM_SIZE;
            uint8_t vlo = EEPROM.read(addr + 1);
            uint8_t vhi = EEPROM.read(addr + 2);
            uint16_t vlen = (uint16_t)vlo | ((uint16_t)vhi << 8);
            addr += 3 + (int)vlen;
            continue;
        }
        int vlen_addr = addr + 1 + (int)klen;
        if (vlen_addr + 2 > PAL_NVS_EEPROM_SIZE) return PAL_NVS_EEPROM_SIZE;
        uint8_t vlo = EEPROM.read(vlen_addr);
        uint8_t vhi = EEPROM.read(vlen_addr + 1);
        uint16_t vlen = (uint16_t)vlo | ((uint16_t)vhi << 8);
        addr = vlen_addr + 2 + (int)vlen;
    }
    return PAL_NVS_EEPROM_SIZE;
}

extern "C" pal_err_t pal_nvs_set(const char *key, const void *data, size_t len)
{
    if (!key || !data || len == 0 || len > 0xFFFF) {
        return PAL_ERR_INVALID;
    }
    size_t  klen = strlen(key);
    if (klen == 0 || klen > 255) {
        return PAL_ERR_INVALID;
    }

#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
    EEPROM.begin(PAL_NVS_EEPROM_SIZE);
#endif

    /* Erase old entry if it exists. */
    int val_addr;
    uint16_t old_vlen;
    int old_addr = nvs_find_key(key, &val_addr, &old_vlen);
    if (old_addr >= 0) {
        /* Zero out key_len to mark as erased; leave val_len so we can skip. */
        EEPROM.write(old_addr, 0x00);
    }

    /* Calculate required space: 1 (klen) + klen + 2 (vlen) + len */
    size_t needed = 1 + klen + 2 + len;
    int write_addr = nvs_find_end();
    if (write_addr < 0 || (write_addr + (int)needed) > PAL_NVS_EEPROM_SIZE) {
#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
        EEPROM.commit();
#endif
        return PAL_ERR_NO_MEM;
    }

    /* Write key_len */
    EEPROM.write(write_addr, (uint8_t)klen);
    write_addr++;

    /* Write key bytes */
    for (size_t i = 0; i < klen; i++) {
        EEPROM.write(write_addr++, (uint8_t)key[i]);
    }

    /* Write val_len (little-endian) */
    EEPROM.write(write_addr++, (uint8_t)(len & 0xFF));
    EEPROM.write(write_addr++, (uint8_t)((len >> 8) & 0xFF));

    /* Write value bytes */
    const uint8_t *src = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        EEPROM.write(write_addr++, src[i]);
    }

#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
    EEPROM.commit();
#endif
    return PAL_OK;
}

extern "C" pal_err_t pal_nvs_get(const char *key, void *data, size_t *len)
{
    if (!key || !data || !len) {
        return PAL_ERR_INVALID;
    }

#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
    EEPROM.begin(PAL_NVS_EEPROM_SIZE);
#endif

    int val_addr;
    uint16_t val_len;
    int found = nvs_find_key(key, &val_addr, &val_len);
    if (found < 0) {
        return PAL_ERR_NOT_FOUND;
    }

    size_t copy_len = (val_len < (uint16_t)*len) ? (size_t)val_len : *len;
    uint8_t *dst = (uint8_t *)data;
    for (size_t i = 0; i < copy_len; i++) {
        dst[i] = EEPROM.read(val_addr + (int)i);
    }
    *len = copy_len;
    return PAL_OK;
}

extern "C" pal_err_t pal_nvs_erase(const char *key)
{
    if (!key) {
        return PAL_ERR_INVALID;
    }

#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
    EEPROM.begin(PAL_NVS_EEPROM_SIZE);
#endif

    int val_addr;
    uint16_t val_len;
    int found = nvs_find_key(key, &val_addr, &val_len);
    if (found < 0) {
        return PAL_ERR_NOT_FOUND;
    }

    /* Zero the key_len byte to mark slot as erased. */
    EEPROM.write(found, 0x00);

#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
    EEPROM.commit();
#endif
    return PAL_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* Logging                                                                     */
/* ═══════════════════════════════════════════════════════════════════════════ */

static const char *level_str(int level)
{
    switch (level) {
    case PAL_LOG_ERROR: return "E";
    case PAL_LOG_WARN:  return "W";
    case PAL_LOG_INFO:  return "I";
    case PAL_LOG_DEBUG: return "D";
    default:            return "?";
    }
}

extern "C" void pal_log(int level, const char *tag, const char *fmt, ...)
{
    if (level == PAL_LOG_NONE) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);

#if defined(ESP32)
    /* ESP32 Arduino HardwareSerial has printf(). */
    Serial.printf("[%s][%s] ", level_str(level), tag ? tag : "");
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    Serial.println(buf);
#else
    char buf[256];
    /* Prefix: [L][tag]  */
    int prefix_len = snprintf(buf, sizeof(buf), "[%s][%s] ",
                              level_str(level), tag ? tag : "");
    if (prefix_len < 0 || prefix_len >= (int)sizeof(buf)) {
        prefix_len = 0;
    }
    vsnprintf(buf + prefix_len, sizeof(buf) - (size_t)prefix_len, fmt, ap);
    buf[sizeof(buf) - 1] = '\0';
    Serial.println(buf);
#endif

    va_end(ap);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/* System                                                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

extern "C" void pal_reboot(void)
{
#if defined(ESP32)
    ESP.restart();
#elif defined(ARDUINO_ARCH_RP2040)
    rp2040.reboot();
#else
    /* Universal Arduino soft-reset: jump to address 0. */
    void (*resetFunc)(void) = 0;
    resetFunc();
#endif
}

extern "C" uint32_t pal_get_free_heap(void)
{
#if defined(ESP32)
    return (uint32_t)esp_get_free_heap_size();
#elif defined(ARDUINO_ARCH_RP2040)
    return (uint32_t)rp2040.getFreeHeap();
#else
    return 0;
#endif
}

extern "C" void pal_feed_watchdog(void)
{
#if defined(__AVR__)
    wdt_reset();
#elif defined(ESP32)
    /* ESP32 FreeRTOS WDT: not directly accessible here; yield instead. */
    yield();
#else
    /* No-op on boards without a portable WDT API. */
#endif
}

extern "C" pal_err_t pal_init(void)
{
    /* Initialise default I2C bus and console UART. */
    Wire.begin();
    Serial.begin(115200);
    return PAL_OK;
}
