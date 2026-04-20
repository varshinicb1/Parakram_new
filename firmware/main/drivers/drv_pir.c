/**
 * @file drv_pir.c
 * @brief PIR motion sensor driver — GPIO input with rising-edge interrupt.
 *
 * Debounce: 500 ms.  CAP_MOTION returns bool (true = motion detected).
 * The motion flag is cleared after each read to implement "latch until polled"
 * semantics.  The interrupt sets the flag; the read function returns it.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "DRV_PIR";

#define PIR_MAX_INSTANCES   4
#define PIR_DEBOUNCE_US     500000LL   /* 500 ms */

typedef struct {
    gpio_num_t  pin;
    bool        initialized;
    volatile bool  motion_detected;
    volatile int64_t last_trigger_us;
} pir_state_t;

static pir_state_t s_state[PIR_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

/* ISR must not touch FreeRTOS API at task level; only atomics/flags */
static void IRAM_ATTR pir_isr_handler(void *arg)
{
    pir_state_t *st = (pir_state_t *)arg;
    int64_t now = esp_timer_get_time();
    if ((now - st->last_trigger_us) >= PIR_DEBOUNCE_US) {
        st->motion_detected  = true;
        st->last_trigger_us  = now;
    }
}

static esp_err_t pir_init(const driver_config_t *cfg)
{
    if (s_instance_count >= PIR_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    pir_state_t *st = &s_state[s_instance_count];
    st->pin              = (gpio_num_t)cfg->pins[0].gpio_num;
    st->motion_detected  = false;
    st->last_trigger_us  = 0;

    gpio_config_t io = {
        .pin_bit_mask  = (1ULL << st->pin),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_ENABLE,
        .intr_type     = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %d", err);
        return err;
    }

    /* Install ISR service if not already installed (ignore error on re-install) */
    gpio_install_isr_service(0);

    err = gpio_isr_handler_add(st->pin, pir_isr_handler, st);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ISR handler add failed: %d", err);
        return err;
    }

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "PIR initialized on GPIO%d", st->pin);
    return ESP_OK;
}

static esp_err_t pir_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    pir_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_MOTION) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Atomic read + clear */
    bool motion = st->motion_detected;
    st->motion_detected = false;

    out->type         = VAL_TYPE_BOOL;
    out->b            = motion;
    out->capability   = CAP_MOTION;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t pir_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        pir_state_t *st = &s_state[h.driver_index];
        gpio_isr_handler_remove(st->pin);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t pir_meta = {
    .name             = "drv_pir",
    .display_name     = "PIR Motion Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_MOTION},
    .num_capabilities = 1,
    .max_latency_us   = 500,
    .min_interval_ms  = 500,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "GPIO stuck or sensor unpowered", {.type=VAL_TYPE_BOOL, .b=false}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(pir_state_t),
};

const driver_vtable_t drv_pir_vtable = {
    .init   = pir_init,
    .read   = pir_read,
    .write  = NULL,
    .deinit = pir_deinit,
    .meta   = &pir_meta,
};
