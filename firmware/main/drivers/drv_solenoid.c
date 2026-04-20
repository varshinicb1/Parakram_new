/**
 * @file drv_solenoid.c
 * @brief Solenoid valve driver — single GPIO output.
 *
 * Functionally identical to drv_relay but semantically typed for solenoids.
 * Writes CAP_FLOW_CONTROL (bool: true=open, false=closed).
 * Reads  CAP_FLOW_CONTROL: returns current valve state.
 *
 * cfg->invert: if true, logic is inverted (active-low drive).
 * cfg->pins[0]: control GPIO.
 *
 * Max 4 solenoid instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "DRV_SOLENOID";

#define SOLENOID_MAX_INSTANCES  4

typedef struct {
    gpio_num_t  pin;
    bool        inverted;
    bool        state;          /* true = open / energised */
    bool        initialized;
} solenoid_state_t;

static solenoid_state_t s_state[SOLENOID_MAX_INSTANCES];
static uint8_t s_count = 0;

static esp_err_t solenoid_set(solenoid_state_t *st, bool open)
{
    uint32_t level = open ? 1u : 0u;
    if (st->inverted) level = !level;
    esp_err_t err = gpio_set_level(st->pin, level);
    if (err == ESP_OK) st->state = open;
    return err;
}

static esp_err_t solenoid_init(const driver_config_t *cfg)
{
    if (s_count >= SOLENOID_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max solenoid instances (%d) reached", SOLENOID_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    solenoid_state_t *st = &s_state[s_count];
    st->pin      = (gpio_num_t)cfg->pins[0].gpio_num;
    st->inverted = cfg->invert;
    st->state    = false;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << st->pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed for GPIO%d: %d", st->pin, err);
        return err;
    }

    /* Initialise to closed (safe state) */
    solenoid_set(st, false);

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "Solenoid[%d] on GPIO%d (inverted=%d) — CLOSED", s_count - 1, st->pin, st->inverted);
    return ESP_OK;
}

static esp_err_t solenoid_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    solenoid_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_FLOW_CONTROL) return ESP_ERR_NOT_SUPPORTED;

    bool open;
    switch (cmd->type) {
        case VAL_TYPE_BOOL:  open = cmd->b; break;
        case VAL_TYPE_INT:   open = (cmd->i != 0); break;
        case VAL_TYPE_FLOAT: open = (cmd->f > 0.5f); break;
        default:             return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = solenoid_set(st, open);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Solenoid[%d] → %s", idx, open ? "OPEN" : "CLOSED");
    }
    return err;
}

static esp_err_t solenoid_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    solenoid_state_t *st = &s_state[idx];

    if (field != CAP_FLOW_CONTROL) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->type         = VAL_TYPE_BOOL;
    out->b            = st->state;
    out->capability   = CAP_FLOW_CONTROL;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t solenoid_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        solenoid_set(&s_state[idx], false); /* close valve on deinit */
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t solenoid_meta = {
    .name             = "drv_solenoid",
    .display_name     = "Solenoid Valve",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_FLOW_CONTROL},
    .num_capabilities = 1,
    .max_latency_us   = 10,
    .min_interval_ms  = 50,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "GPIO stuck / coil short", {.type = VAL_TYPE_BOOL, .b = false}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(solenoid_state_t),
};

const driver_vtable_t drv_solenoid_vtable = {
    .init   = solenoid_init,
    .read   = solenoid_read,
    .write  = solenoid_write,
    .deinit = solenoid_deinit,
    .meta   = &solenoid_meta,
};
