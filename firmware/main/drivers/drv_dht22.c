/**
 * @file drv_dht22.c
 * @brief DHT22 one-wire temperature + humidity sensor driver.
 *
 * Protocol: pull data line low ≥1 ms to start, release and wait for sensor
 * response, then read 40 bits.  Each bit is encoded as a ~50 µs low pulse
 * followed by a high pulse whose width determines the bit value:
 *   high < 28 µs → 0,  high ≥ 50 µs → 1.
 * Checksum = (byte0 + byte1 + byte2 + byte3) & 0xFF == byte4.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

static const char *TAG = "DRV_DHT22";

#define DHT22_MAX_INSTANCES     4
#define DHT22_START_LOW_US      1100    /* ≥1 ms start pulse */
#define DHT22_RELEASE_US        30      /* wait before sampling response */
#define DHT22_TIMEOUT_US        1000    /* per-edge timeout */

typedef struct {
    gpio_num_t  pin;
    bool        initialized;
    float       temperature;  /* °C */
    float       humidity;     /* %RH */
    int64_t     last_read_us;
} dht22_state_t;

static dht22_state_t s_state[DHT22_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

/* Wait for a specific GPIO level; return elapsed µs or -1 on timeout */
static int dht22_wait_level(gpio_num_t pin, int level, int timeout_us)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(pin) != level) {
        if ((esp_timer_get_time() - t0) > timeout_us) return -1;
    }
    return (int)(esp_timer_get_time() - t0);
}

static esp_err_t dht22_read_raw(dht22_state_t *st)
{
    uint8_t data[5] = {0};

    /* --- Send start pulse --- */
    gpio_set_direction(st->pin, GPIO_MODE_OUTPUT);
    gpio_set_level(st->pin, 0);
    esp_rom_delay_us(DHT22_START_LOW_US);
    gpio_set_level(st->pin, 1);
    esp_rom_delay_us(DHT22_RELEASE_US);
    gpio_set_direction(st->pin, GPIO_MODE_INPUT);

    /* --- Sensor response: 80 µs low, 80 µs high --- */
    if (dht22_wait_level(st->pin, 0, DHT22_TIMEOUT_US) < 0) {
        ESP_LOGE(TAG, "No response (low phase)");
        return ESP_ERR_TIMEOUT;
    }
    if (dht22_wait_level(st->pin, 1, DHT22_TIMEOUT_US) < 0) {
        ESP_LOGE(TAG, "No response (high phase)");
        return ESP_ERR_TIMEOUT;
    }

    /* --- Read 40 bits --- */
    for (int i = 0; i < 40; i++) {
        /* Wait for low (start of bit) */
        if (dht22_wait_level(st->pin, 0, DHT22_TIMEOUT_US) < 0) {
            ESP_LOGE(TAG, "Timeout waiting for bit %d low", i);
            return ESP_ERR_TIMEOUT;
        }
        /* Wait for high (data pulse) */
        if (dht22_wait_level(st->pin, 1, DHT22_TIMEOUT_US) < 0) {
            ESP_LOGE(TAG, "Timeout waiting for bit %d high", i);
            return ESP_ERR_TIMEOUT;
        }
        /* Measure high duration */
        int64_t t_high = esp_timer_get_time();
        if (dht22_wait_level(st->pin, 0, DHT22_TIMEOUT_US) < 0) {
            /* Last bit may not see falling edge cleanly; treat as read */
        }
        int64_t high_us = esp_timer_get_time() - t_high;

        uint8_t byte_idx = i / 8;
        data[byte_idx] <<= 1;
        if (high_us > 40) {   /* > ~40 µs → bit is 1 */
            data[byte_idx] |= 1;
        }
    }

    /* --- Checksum --- */
    uint8_t chk = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (chk != data[4]) {
        ESP_LOGE(TAG, "CRC error: calc=0x%02X rx=0x%02X", chk, data[4]);
        return ESP_FAIL;
    }

    /* --- Decode --- */
    uint16_t raw_h = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_t = ((uint16_t)(data[2] & 0x7F) << 8) | data[3];
    st->humidity    = raw_h / 10.0f;
    st->temperature = raw_t / 10.0f;
    if (data[2] & 0x80) st->temperature = -st->temperature; /* sign bit */
    return ESP_OK;
}

static esp_err_t dht22_init(const driver_config_t *cfg)
{
    if (s_instance_count >= DHT22_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    dht22_state_t *st = &s_state[s_instance_count];
    st->pin = (gpio_num_t)cfg->pins[0].gpio_num;
    st->last_read_us = 0;

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

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "DHT22 initialized on GPIO%d", st->pin);
    return ESP_OK;
}

static esp_err_t dht22_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    dht22_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    /* DHT22 min interval 2 s; enforce 2000 ms minimum */
    int64_t now = esp_timer_get_time();
    if ((now - st->last_read_us) < 2000000LL) {
        /* Return cached values */
    } else {
        esp_err_t err = dht22_read_raw(st);
        if (err != ESP_OK) {
            out->error = (err == ESP_ERR_TIMEOUT) ? DRV_ERR_TIMEOUT : DRV_ERR_CRC;
            return err;
        }
        st->last_read_us = now;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->capability   = field;

    switch (field) {
        case CAP_TEMPERATURE: out->f = st->temperature; break;
        case CAP_HUMIDITY:    out->f = st->humidity;    break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t dht22_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t dht22_meta = {
    .name             = "drv_dht22",
    .display_name     = "DHT22 Temperature & Humidity",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_ONEWIRE,
    .capabilities     = {CAP_TEMPERATURE, CAP_HUMIDITY},
    .num_capabilities = 2,
    .max_latency_us   = 5000,
    .min_interval_ms  = 2000,
    .failure_modes    = {
        {DRV_ERR_TIMEOUT, "No sensor response",   {.type=VAL_TYPE_FLOAT, .f=0}},
        {DRV_ERR_CRC,     "Checksum mismatch",    {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 2,
    .internal_state_size = sizeof(dht22_state_t),
};

const driver_vtable_t drv_dht22_vtable = {
    .init   = dht22_init,
    .read   = dht22_read,
    .write  = NULL,
    .deinit = dht22_deinit,
    .meta   = &dht22_meta,
};
