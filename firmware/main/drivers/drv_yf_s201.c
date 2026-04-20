/**
 * @file drv_yf_s201.c
 * @brief YF-S201 water flow sensor driver.
 *
 * The YF-S201 outputs a square wave on its signal pin: approximately
 * 7.5 pulses per second per litre per minute (7.5 Hz·min/L).
 *
 * Measurement:
 *   - GPIO interrupt on rising edge increments pulse_count.
 *   - Every 1 second the ISR-safe snapshot of pulse_count is taken.
 *   - Flow rate (L/min) = pulse_count / 7.5
 *   - Converted to mL/min for the CAP_COUNT output value.
 *
 * cfg->pins[0]: signal GPIO (requires pull-up if open-drain).
 *
 * Read CAP_COUNT: int32 flow rate in mL/min.
 *
 * Note: gpio_install_isr_service() must be called before init.
 * Max 4 flow sensor instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "DRV_YF_S201";

#define YF_S201_MAX_INSTANCES       4
#define YF_S201_PULSES_PER_LPM      7.5f   /* pulses per second per L/min */
#define YF_S201_MEASURE_INTERVAL_US 1000000 /* 1 second */

typedef struct {
    gpio_num_t          pin;
    volatile uint32_t   pulse_count;        /* incremented in ISR */
    uint32_t            flow_ml_min;        /* last computed flow rate */
    int64_t             last_measure_us;
    bool                initialized;
} yf_s201_state_t;

static yf_s201_state_t s_state[YF_S201_MAX_INSTANCES];
static uint8_t s_count = 0;

static void IRAM_ATTR yf_s201_isr(void *arg)
{
    yf_s201_state_t *st = (yf_s201_state_t *)arg;
    st->pulse_count++;
}

static void yf_s201_update(yf_s201_state_t *st)
{
    int64_t now = esp_timer_get_time();
    int64_t elapsed = now - st->last_measure_us;

    if (elapsed < YF_S201_MEASURE_INTERVAL_US) return;

    /* Atomically snapshot and reset */
    portDISABLE_INTERRUPTS();
    uint32_t pulses = st->pulse_count;
    st->pulse_count = 0;
    portENABLE_INTERRUPTS();

    /* Pulses per second (over the measurement window) */
    float elapsed_s = (float)elapsed / 1000000.0f;
    float pps = (float)pulses / elapsed_s;

    /* L/min = pps / 7.5 ; mL/min = L/min * 1000 */
    float flow_lpm = pps / YF_S201_PULSES_PER_LPM;
    st->flow_ml_min = (uint32_t)(flow_lpm * 1000.0f);
    st->last_measure_us = now;

    ESP_LOGD(TAG, "Flow: %lu pulses in %.2fs → %.2f L/min (%lu mL/min)",
             (unsigned long)pulses, elapsed_s, flow_lpm, (unsigned long)st->flow_ml_min);
}

static esp_err_t yf_s201_init(const driver_config_t *cfg)
{
    if (s_count >= YF_S201_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max YF-S201 instances (%d) reached", YF_S201_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    yf_s201_state_t *st = &s_state[s_count];
    st->pin             = (gpio_num_t)cfg->pins[0].gpio_num;
    st->pulse_count     = 0;
    st->flow_ml_min     = 0;
    st->last_measure_us = esp_timer_get_time();

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << st->pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %d", err);
        return err;
    }

    err = gpio_isr_handler_add(st->pin, yf_s201_isr, st);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ISR handler add failed: %d", err);
        gpio_set_intr_type(st->pin, GPIO_INTR_DISABLE);
        return err;
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "YF-S201[%d] on GPIO%d", s_count - 1, st->pin);
    return ESP_OK;
}

static esp_err_t yf_s201_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    yf_s201_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_COUNT) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    yf_s201_update(st);

    out->type         = VAL_TYPE_INT;
    out->i            = (int32_t)st->flow_ml_min;
    out->capability   = CAP_COUNT;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t yf_s201_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    (void)h; (void)cmd;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t yf_s201_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        gpio_isr_handler_remove(s_state[idx].pin);
        gpio_set_intr_type(s_state[idx].pin, GPIO_INTR_DISABLE);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t yf_s201_meta = {
    .name             = "drv_yf_s201",
    .display_name     = "YF-S201 Water Flow Sensor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_GPIO,
    .capabilities     = {CAP_COUNT},
    .num_capabilities = 1,
    .max_latency_us   = 1000000,   /* 1-second measurement window */
    .min_interval_ms  = 1000,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "No pulses detected", {.type = VAL_TYPE_INT, .i = 0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(yf_s201_state_t),
};

const driver_vtable_t drv_yf_s201_vtable = {
    .init   = yf_s201_init,
    .read   = yf_s201_read,
    .write  = yf_s201_write,
    .deinit = yf_s201_deinit,
    .meta   = &yf_s201_meta,
};
