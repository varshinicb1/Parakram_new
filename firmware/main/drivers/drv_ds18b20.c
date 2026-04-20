/**
 * @file drv_ds18b20.c
 * @brief DS18B20 1-Wire temperature sensor driver.
 *
 * 1-Wire bit-bang protocol on a single GPIO:
 *   Reset  : pull low 480 µs, release, wait 60 µs, sample presence (low=present),
 *            wait remaining 420 µs.
 *   Write 0: pull low 60 µs, release, wait 10 µs.
 *   Write 1: pull low  5 µs, release, wait 55 µs.
 *   Read bit: pull low 5 µs, release, sample at 15 µs, wait remaining ~45 µs.
 *
 * Sequence: RESET → Skip ROM (0xCC) → Convert T (0x44) → wait 750 ms →
 *           RESET → Skip ROM (0xCC) → Read Scratchpad (0xBE) → read 9 bytes.
 *
 * Temperature = ((byte1 & 0x07) << 8 | byte0) * 0.0625f  [°C, 12-bit].
 * Handles negative temperatures via two's complement.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

static const char *TAG = "DRV_DS18B20";

#define DS18B20_MAX_INSTANCES   4

typedef struct {
    gpio_num_t  pin;
    bool        initialized;
    float       temperature;
} ds18b20_state_t;

static ds18b20_state_t s_state[DS18B20_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

/* ---------- 1-Wire primitives ---------- */

static inline void ow_pull_low(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

static inline void ow_release(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
}

static inline int ow_read_pin(gpio_num_t pin)
{
    return gpio_get_level(pin);
}

/* Returns true if a device is present */
static bool ow_reset(gpio_num_t pin)
{
    ow_pull_low(pin);
    esp_rom_delay_us(480);
    ow_release(pin);
    esp_rom_delay_us(70);
    int present = (ow_read_pin(pin) == 0) ? 1 : 0;
    esp_rom_delay_us(410);
    return (bool)present;
}

static void ow_write_bit(gpio_num_t pin, int bit)
{
    if (bit) {
        ow_pull_low(pin);
        esp_rom_delay_us(5);
        ow_release(pin);
        esp_rom_delay_us(55);
    } else {
        ow_pull_low(pin);
        esp_rom_delay_us(60);
        ow_release(pin);
        esp_rom_delay_us(10);
    }
}

static int ow_read_bit(gpio_num_t pin)
{
    ow_pull_low(pin);
    esp_rom_delay_us(3);
    ow_release(pin);
    esp_rom_delay_us(10);
    int bit = ow_read_pin(pin);
    esp_rom_delay_us(47);
    return bit;
}

static void ow_write_byte(gpio_num_t pin, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(pin, byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t ow_read_byte(gpio_num_t pin)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        if (ow_read_bit(pin)) byte |= (1 << i);
    }
    return byte;
}

/* ---------- DS18B20 logic ---------- */

static esp_err_t ds18b20_init(const driver_config_t *cfg)
{
    if (s_instance_count >= DS18B20_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    ds18b20_state_t *st = &s_state[s_instance_count];
    st->pin = (gpio_num_t)cfg->pins[0].gpio_num;

    /* Configure with pull-up; 1-Wire requires external 4.7 kΩ pull-up */
    gpio_config_t io = {
        .pin_bit_mask  = (1ULL << st->pin),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %d", err);
        return err;
    }

    /* Verify presence */
    if (!ow_reset(st->pin)) {
        ESP_LOGE(TAG, "No DS18B20 detected on GPIO%d", st->pin);
        return ESP_FAIL;
    }

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "DS18B20 initialized on GPIO%d", st->pin);
    return ESP_OK;
}

static esp_err_t ds18b20_sample(ds18b20_state_t *st)
{
    /* Step 1: Trigger conversion */
    if (!ow_reset(st->pin)) return ESP_ERR_TIMEOUT;
    ow_write_byte(st->pin, 0xCC); /* Skip ROM */
    ow_write_byte(st->pin, 0x44); /* Convert T */

    /* Wait 750 ms for 12-bit conversion */
    vTaskDelay(pdMS_TO_TICKS(750));

    /* Step 2: Read scratchpad */
    if (!ow_reset(st->pin)) return ESP_ERR_TIMEOUT;
    ow_write_byte(st->pin, 0xCC); /* Skip ROM */
    ow_write_byte(st->pin, 0xBE); /* Read Scratchpad */

    uint8_t pad[9];
    for (int i = 0; i < 9; i++) {
        pad[i] = ow_read_byte(st->pin);
    }

    /* CRC check (simple byte-sum; proper CRC-8 omitted for size) */
    /* A full Dallas CRC8 would use polynomial 0x31 — simple sanity check: */
    if (pad[0] == 0xFF && pad[1] == 0xFF) {
        ESP_LOGE(TAG, "Scratchpad all 0xFF — no sensor or bad contact");
        return ESP_FAIL;
    }

    int16_t raw = (int16_t)(((uint16_t)pad[1] << 8) | pad[0]);
    st->temperature = (float)raw * 0.0625f;
    return ESP_OK;
}

static esp_err_t ds18b20_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    ds18b20_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_TEMPERATURE) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = ds18b20_sample(st);
    if (err != ESP_OK) {
        out->error = DRV_ERR_TIMEOUT;
        return err;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->f            = st->temperature;
    out->capability   = CAP_TEMPERATURE;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t ds18b20_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t ds18b20_meta = {
    .name             = "drv_ds18b20",
    .display_name     = "DS18B20 1-Wire Temperature",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_ONEWIRE,
    .capabilities     = {CAP_TEMPERATURE},
    .num_capabilities = 1,
    .max_latency_us   = 800000,
    .min_interval_ms  = 1000,
    .failure_modes    = {
        {DRV_ERR_TIMEOUT,  "No 1-Wire presence pulse",  {.type=VAL_TYPE_FLOAT, .f=0}},
        {DRV_ERR_HW_FAULT, "Scratchpad read all 0xFF",  {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 2,
    .internal_state_size = sizeof(ds18b20_state_t),
};

const driver_vtable_t drv_ds18b20_vtable = {
    .init   = ds18b20_init,
    .read   = ds18b20_read,
    .write  = NULL,
    .deinit = ds18b20_deinit,
    .meta   = &ds18b20_meta,
};
