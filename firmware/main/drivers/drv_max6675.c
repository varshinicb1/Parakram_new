/**
 * @file drv_max6675.c
 * @brief MAX6675 SPI K-type thermocouple digitiser.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DRV_MAX6675";

typedef struct {
    gpio_num_t cs_pin;
    gpio_num_t sck_pin;
    gpio_num_t miso_pin;
    bool       initialized;
    float      last_temp;
} max6675_state_t;

static max6675_state_t s_state[4];
static uint8_t s_count = 0;

/* Bit-bang SPI read — MAX6675 is SPI Mode 0/1, CS active low */
static uint16_t max6675_read_raw(max6675_state_t *st) {
    uint16_t val = 0;
    gpio_set_level(st->cs_pin, 0);
    esp_rom_delay_us(1);
    for (int i = 15; i >= 0; i--) {
        gpio_set_level(st->sck_pin, 0);
        esp_rom_delay_us(1);
        if (gpio_get_level(st->miso_pin)) val |= (1 << i);
        gpio_set_level(st->sck_pin, 1);
        esp_rom_delay_us(1);
    }
    gpio_set_level(st->cs_pin, 1);
    return val;
}

static esp_err_t max6675_init(const driver_config_t *cfg) {
    if (s_count >= 4) return ESP_ERR_NO_MEM;
    max6675_state_t *st = &s_state[s_count];

    st->cs_pin   = cfg->pins[0].gpio_num;
    st->sck_pin  = cfg->pins[1].gpio_num;
    st->miso_pin = cfg->pins[2].gpio_num;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << st->cs_pin) | (1ULL << st->sck_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_config_t mi = {
        .pin_bit_mask = (1ULL << st->miso_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&mi);
    gpio_set_level(st->cs_pin, 1);
    gpio_set_level(st->sck_pin, 0);

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "MAX6675 init OK CS=%d SCK=%d MISO=%d", st->cs_pin, st->sck_pin, st->miso_pin);
    return ESP_OK;
}

static esp_err_t max6675_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    max6675_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;
    if (field != CAP_TEMPERATURE) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint16_t raw = max6675_read_raw(st);

    if (raw & 0x0004) { /* Open thermocouple bit */
        out->error = DRV_ERR_HW_FAULT;
        return ESP_FAIL;
    }

    /* Bits 14:3 = 12-bit temperature, LSB = 0.25°C */
    st->last_temp = (float)((raw >> 3) & 0x0FFF) * 0.25f;

    out->type = VAL_TYPE_FLOAT;
    out->f = st->last_temp;
    out->capability = CAP_TEMPERATURE;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t max6675_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) s_state[h.driver_index].initialized = false;
    return ESP_OK;
}

static const driver_meta_t max6675_meta = {
    .name = "drv_max6675", .display_name = "MAX6675 Thermocouple",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_SPI,
    .capabilities = {CAP_TEMPERATURE}, .num_capabilities = 1,
    .max_latency_us = 500, .min_interval_ms = 250,
    .failure_modes = {{DRV_ERR_HW_FAULT, "Open thermocouple", {.type=VAL_TYPE_FLOAT,.f=0}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(max6675_state_t),
};

const driver_vtable_t drv_max6675_vtable = {
    .init=max6675_init, .read=max6675_read, .write=NULL, .deinit=max6675_deinit, .meta=&max6675_meta
};
