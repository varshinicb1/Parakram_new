/**
 * @file drv_inmp441.c
 * @brief INMP441 I2S MEMS microphone driver.
 *        Returns RMS audio level as a float (0.0–1.0 normalised).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "DRV_INMP441";

#define INMP441_SAMPLE_RATE 16000
#define INMP441_FRAME_SIZE  256
#define INMP441_BUF_SIZE    (INMP441_FRAME_SIZE * sizeof(int32_t))

typedef struct {
    i2s_chan_handle_t rx_chan;
    bool             initialized;
    float            rms_level;
    int32_t          sample_buf[INMP441_FRAME_SIZE];
} inmp441_state_t;

static inmp441_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t inmp441_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    inmp441_state_t *st = &s_state[s_count];

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)cfg->bus_index, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, NULL, &st->rx_chan) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed");
        return ESP_FAIL;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(INMP441_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)cfg->pins[0].gpio_num,
            .ws   = (gpio_num_t)cfg->pins[1].gpio_num,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)cfg->pins[2].gpio_num,
            .invert_flags = { .mclk_inv=false, .bclk_inv=false, .ws_inv=false },
        },
    };
    if (i2s_channel_init_std_mode(st->rx_chan, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed");
        i2s_del_channel(st->rx_chan);
        return ESP_FAIL;
    }
    i2s_channel_enable(st->rx_chan);

    st->rms_level = 0.0f;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "INMP441 init OK on I2S%d", cfg->bus_index);
    return ESP_OK;
}

static esp_err_t inmp441_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    inmp441_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (field != CAP_GAS_PPM) { /* reusing this field for sound level */
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t bytes_read = 0;
    i2s_channel_read(st->rx_chan, st->sample_buf, INMP441_BUF_SIZE, &bytes_read, pdMS_TO_TICKS(100));

    int samples = (int)(bytes_read / sizeof(int32_t));
    if (samples > 0) {
        double sum = 0.0;
        for (int i = 0; i < samples; i++) {
            double s = (double)(st->sample_buf[i] >> 8) / (double)0x800000;
            sum += s * s;
        }
        st->rms_level = (float)sqrt(sum / samples);
    }

    out->type = VAL_TYPE_FLOAT;
    out->f = st->rms_level;
    out->capability = field;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t inmp441_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        i2s_channel_disable(s_state[h.driver_index].rx_chan);
        i2s_del_channel(s_state[h.driver_index].rx_chan);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t inmp441_meta = {
    .name = "drv_inmp441", .display_name = "INMP441 I2S Microphone",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_I2C, /* I2S */
    .capabilities = {CAP_GAS_PPM}, .num_capabilities = 1,
    .max_latency_us = 20000, .min_interval_ms = 16,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2S failure", {.type=VAL_TYPE_FLOAT,.f=0}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(inmp441_state_t),
};

const driver_vtable_t drv_inmp441_vtable = {
    .init=inmp441_init, .read=inmp441_read, .write=NULL, .deinit=inmp441_deinit, .meta=&inmp441_meta
};
