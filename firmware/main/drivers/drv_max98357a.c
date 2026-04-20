/**
 * @file drv_max98357a.c
 * @brief MAX98357A I2S Class-D audio amplifier driver.
 *        Plays 16-bit PCM audio via I2S peripheral; write tone/frequency command.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "DRV_MAX98357A";

#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_FRAME_SIZE    512
#define M_PI_F              3.14159265358979f

typedef struct {
    i2s_chan_handle_t tx_chan;
    gpio_num_t       sd_mode_pin; /* SD_MODE: high=stereo, low=shutdown */
    bool             initialized;
    float            freq_hz;     /* current tone frequency, 0=silent */
    uint32_t         phase_acc;
    int16_t          pcm_buf[AUDIO_FRAME_SIZE];
} max98357a_state_t;

static max98357a_state_t s_state[2];
static uint8_t s_count = 0;

static esp_err_t max98357a_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    max98357a_state_t *st = &s_state[s_count];

    st->sd_mode_pin = cfg->pins[0].gpio_num;
    gpio_num_t bclk = cfg->pins[1].gpio_num;
    gpio_num_t ws   = cfg->pins[2].gpio_num;
    gpio_num_t dout = cfg->pins[3].gpio_num;

    /* SD_MODE pin: active high to enable */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << st->sd_mode_pin),
        .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(st->sd_mode_pin, 0); /* start silent */

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)cfg->bus_index, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &st->tx_chan, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed");
        return ESP_FAIL;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk, .ws = ws, .dout = dout, .din = I2S_GPIO_UNUSED,
            .invert_flags = {.mclk_inv=false,.bclk_inv=false,.ws_inv=false},
        },
    };
    if (i2s_channel_init_std_mode(st->tx_chan, &std_cfg) != ESP_OK) {
        i2s_del_channel(st->tx_chan);
        return ESP_FAIL;
    }
    i2s_channel_enable(st->tx_chan);

    st->freq_hz = 0.0f;
    st->phase_acc = 0;
    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "MAX98357A init OK on I2S%d", cfg->bus_index);
    return ESP_OK;
}

static void play_tone(max98357a_state_t *st) {
    if (st->freq_hz <= 0.0f) {
        /* Silence */
        memset(st->pcm_buf, 0, sizeof(st->pcm_buf));
        gpio_set_level(st->sd_mode_pin, 0);
    } else {
        gpio_set_level(st->sd_mode_pin, 1);
        float phase_inc = st->freq_hz * 65536.0f / AUDIO_SAMPLE_RATE;
        for (int i = 0; i < AUDIO_FRAME_SIZE; i++) {
            float angle = (float)st->phase_acc * 2.0f * M_PI_F / 65536.0f;
            st->pcm_buf[i] = (int16_t)(sinf(angle) * 16384.0f);
            st->phase_acc = (uint32_t)(st->phase_acc + (uint32_t)phase_inc) & 0xFFFF;
        }
    }
    size_t written = 0;
    i2s_channel_write(st->tx_chan, st->pcm_buf, sizeof(st->pcm_buf), &written, pdMS_TO_TICKS(50));
}

static esp_err_t max98357a_write(driver_handle_t h, const actuator_cmd_t *cmd) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    max98357a_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    switch (cmd->type) {
        case VAL_TYPE_FLOAT: st->freq_hz = cmd->f; break;
        case VAL_TYPE_INT:   st->freq_hz = (float)cmd->i; break;
        case VAL_TYPE_BOOL:  st->freq_hz = cmd->b ? 440.0f : 0.0f; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    play_tone(st);
    return ESP_OK;
}

static esp_err_t max98357a_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    out->type = VAL_TYPE_FLOAT;
    out->f = s_state[h.driver_index].freq_hz;
    out->capability = CAP_TONE_HZ;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t max98357a_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        gpio_set_level(s_state[h.driver_index].sd_mode_pin, 0);
        i2s_channel_disable(s_state[h.driver_index].tx_chan);
        i2s_del_channel(s_state[h.driver_index].tx_chan);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t max98357a_meta = {
    .name = "drv_max98357a", .display_name = "MAX98357A I2S Amplifier",
    .version = "1.0.0", .type = DRIVER_TYPE_ACTUATOR, .bus_type = BUS_TYPE_I2C, /* I2S */
    .capabilities = {CAP_TONE_HZ}, .num_capabilities = 1,
    .max_latency_us = 10000, .min_interval_ms = 10,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2S failure", {.type=VAL_TYPE_FLOAT,.f=0}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(max98357a_state_t),
};

const driver_vtable_t drv_max98357a_vtable = {
    .init=max98357a_init, .read=max98357a_read, .write=max98357a_write,
    .deinit=max98357a_deinit, .meta=&max98357a_meta
};
