/**
 * @file drv_reed.c
 * @brief Reed switch / magnetic door sensor driver.
 *
 * GPIO input with internal pull-up. Interrupt on both edges.
 * Hardware debounce: ignores transitions within 50 ms of the last transition.
 * State:
 *   GPIO high (pull-up, magnet absent) → door OPEN  (true)
 *   GPIO low  (magnet present, contact closed) → door CLOSED (false)
 *
 * cfg->invert: reverses the logic (contact-closed = OPEN).
 * cfg->pins[0]: reed switch GPIO input.
 *
 * Read CAP_DOOR_STATE: bool (true = door open, false = door closed).
 *
 * Max 4 reed switch instances.
 *
 * Note: gpio_install_isr_service() must be called by the application
 * before any interrupt-using driver is initialised (typically in app_main).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "DRV_REED";

#define REED_MAX_INSTANCES      4
#define REED_DEBOUNCE_US        50000   /* 50 ms */

typedef struct {
    gpio_num_t  pin;
    bool        inverted;
    volatile bool state;            /* true = open */
    volatile int64_t last_change_us;
    bool        initialized;
} reed_state_t;

static reed_state_t s_state[REED_MAX_INSTANCES];
static uint8_t s_count = 0;

static void IRAM_ATTR reed_isr(void *arg)
{
    reed_state_t *st = (reed_state_t *)arg;
    int64_t now = esp_timer_get_time();

    /* Debounce: ignore if < REED_DEBOUNCE_US since last change */
    if ((now - st->last_change_us) < REED_DEBOUNCE_US) return;
    st->last_change_us = now;

    int level = gpio_get_level(st->pin);
    /* High = open (pull-up, no magnet); Low = closed (magnet) */
    bool open = (level == 1);
    if (st->inverted) open = !open;
    st->state = open;
}

static esp_err_t reed_init(const driver_config_t *cfg)
{
    if (s_count >= REED_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max reed instances (%d) reached", REED_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    reed_state_t *st = &s_state[s_count];
    st->pin            = (gpio_num_t)cfg->pins[0].gpio_num;
    st->inverted       = cfg->invert;
    st->last_change_us = 0;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << st->pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %d", err);
        return err;
    }

    /* Read initial state */
    int level = gpio_get_level(st->pin);
    st->state = (level == 1);
    if (st->inverted) st->state = !st->state;

    /* Install ISR handler */
    err = gpio_isr_handler_add(st->pin, reed_isr, st);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ISR handler add failed: %d", err);
        /* Clear interrupt type so it works without ISR service */
        gpio_set_intr_type(st->pin, GPIO_INTR_DISABLE);
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "Reed[%d] on GPIO%d (inverted=%d) → %s",
             s_count - 1, st->pin, st->inverted, st->state ? "OPEN" : "CLOSED");
    return ESP_OK;
}

static esp_err_t reed_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    reed_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_DOOR_STATE) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->type         = VAL_TYPE_BOOL;
    out->b            = st->state;
    out->capability   = CAP_DOOR_STATE;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t reed_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    (void)h; (void)cmd;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t reed_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        gpio_isr_handler_remove(s_state[idx].pin);
        gpio_set_intr_type(s_state[idx].pin, GPIO_INTR_DISABLE);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t reed_meta = {
    .name             = "drv_reed",
    .display_name     = "Reed Switch / Door Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_DOOR_STATE},
    .num_capabilities = 1,
    .max_latency_us   = 50000,   /* debounce window */
    .min_interval_ms  = 50,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "GPIO stuck", {.type = VAL_TYPE_BOOL, .b = false}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(reed_state_t),
};

const driver_vtable_t drv_reed_vtable = {
    .init   = reed_init,
    .read   = reed_read,
    .write  = reed_write,
    .deinit = reed_deinit,
    .meta   = &reed_meta,
};
