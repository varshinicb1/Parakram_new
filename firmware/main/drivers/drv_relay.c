/**
 * @file drv_relay.c
 * @brief GPIO relay driver — on/off switching.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "DRV_RELAY";

typedef struct {
    gpio_num_t  pin;
    bool        inverted;
    bool        state;
    bool        initialized;
} relay_state_t;

static relay_state_t s_relay_state[4]; /* Max 4 relays */
static uint8_t s_relay_count = 0;

static esp_err_t relay_init(const driver_config_t *cfg) {
    if (s_relay_count >= 4) return ESP_ERR_NO_MEM;

    relay_state_t *st = &s_relay_state[s_relay_count];
    st->pin = cfg->pins[0].gpio_num;
    st->inverted = cfg->invert;
    st->state = false;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << st->pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed for pin %d: %d", st->pin, err);
        return err;
    }

    /* Set initial state: OFF */
    gpio_set_level(st->pin, st->inverted ? 1 : 0);

    st->initialized = true;
    s_relay_count++;
    ESP_LOGI(TAG, "Relay initialized on GPIO%d (inverted=%d)", st->pin, st->inverted);
    return ESP_OK;
}

static esp_err_t relay_write(driver_handle_t h, const actuator_cmd_t *cmd) {
    uint8_t idx = h.driver_index;
    if (idx >= s_relay_count) idx = 0;
    relay_state_t *st = &s_relay_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    bool new_state;
    switch (cmd->type) {
        case VAL_TYPE_BOOL:  new_state = cmd->b; break;
        case VAL_TYPE_INT:   new_state = (cmd->i != 0); break;
        case VAL_TYPE_FLOAT: new_state = (cmd->f > 0.5f); break;
        default: return ESP_ERR_INVALID_ARG;
    }

    uint32_t level = new_state ? 1 : 0;
    if (st->inverted) level = !level;

    esp_err_t err = gpio_set_level(st->pin, level);
    if (err == ESP_OK) {
        st->state = new_state;
        ESP_LOGD(TAG, "Relay GPIO%d → %s", st->pin, new_state ? "ON" : "OFF");
    }
    return err;
}

static esp_err_t relay_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    uint8_t idx = h.driver_index;
    if (idx >= s_relay_count) idx = 0;
    relay_state_t *st = &s_relay_state[idx];

    out->type = VAL_TYPE_BOOL;
    out->b = st->state;
    out->capability = CAP_ON_OFF;
    out->error = DRV_OK;
    return ESP_OK;
}

static esp_err_t relay_deinit(driver_handle_t h) {
    uint8_t idx = h.driver_index;
    if (idx < s_relay_count) {
        gpio_set_level(s_relay_state[idx].pin, s_relay_state[idx].inverted ? 1 : 0);
        s_relay_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t relay_meta = {
    .name = "drv_relay",
    .display_name = "Relay Switch",
    .version = "1.0.0",
    .type = DRIVER_TYPE_ACTUATOR,
    .bus_type = BUS_TYPE_GPIO,
    .capabilities = {CAP_ON_OFF},
    .num_capabilities = 1,
    .max_latency_us = 10,
    .min_interval_ms = 100,
    .failure_modes = {{DRV_ERR_HW_FAULT, "GPIO stuck", {.type=VAL_TYPE_BOOL, .b=false}}},
    .num_failure_modes = 1,
    .internal_state_size = sizeof(relay_state_t),
};

const driver_vtable_t drv_relay_vtable = {
    .init   = relay_init,
    .read   = relay_read,
    .write  = relay_write,
    .deinit = relay_deinit,
    .meta   = &relay_meta,
};
