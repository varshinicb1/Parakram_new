/**
 * @file drv_ws2812.c
 * @brief WS2812B LED strip driver via ESP32-S3 RMT peripheral.
 *
 * Bit encoding (NZR protocol):
 *   Bit 0: T0H = 350 ns high, T0L = 800 ns low
 *   Bit 1: T1H = 700 ns high, T1L = 600 ns low
 *   Reset : ≥ 50 µs low
 *
 * Data order: G7..G0, R7..R0, B7..B0  (GRB byte order)
 * Write CAP_COLOR_RGB: int32 packed as 0x00RRGGBB
 *
 * Supports up to 300 LEDs per strip, up to 4 strips.
 * Static framebuffer: 300 * 3 bytes = 900 bytes per instance.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include <string.h>

static const char *TAG = "DRV_WS2812";

#define WS2812_MAX_INSTANCES    4
#define WS2812_MAX_LEDS         300

/* Timing in RMT ticks — RMT clock divider set for 10 MHz (100 ns per tick) */
#define RMT_CLK_HZ              10000000UL  /* 10 MHz */
#define T0H_TICKS               4           /* 400 ns  (spec 350 ns, rounded) */
#define T0L_TICKS               8           /* 800 ns */
#define T1H_TICKS               7           /* 700 ns */
#define T1L_TICKS               6           /* 600 ns */
#define TRESET_TICKS            500         /* 50 µs reset */

/* One RMT symbol encodes 2 levels: level0/duration0, level1/duration1 */
typedef struct {
    uint16_t duration0 : 15;
    uint16_t level0    :  1;
    uint16_t duration1 : 15;
    uint16_t level1    :  1;
} ws2812_rmt_symbol_t;

/* ---------- custom encoder ------------------------------------------------ */

typedef struct {
    rmt_encoder_t       base;       /* MUST be first */
    rmt_encoder_t      *copy_enc;   /* rmt_copy_encoder for sending raw symbols */
    int                 phase;      /* 0 = data, 1 = reset */
    /* pre-built symbol table for bits 0 and 1 */
    ws2812_rmt_symbol_t bit0;
    ws2812_rmt_symbol_t bit1;
    ws2812_rmt_symbol_t reset_sym;
} ws2812_encoder_t;

/* Encode one byte into rmt symbols, MSB first */
static size_t ws2812_encode_byte(ws2812_encoder_t *enc, rmt_encode_state_t *ret_state,
                                 uint8_t byte, rmt_channel_handle_t channel,
                                 const rmt_transmit_config_t *config)
{
    (void)config;
    ws2812_rmt_symbol_t syms[8];
    for (int i = 7; i >= 0; i--) {
        syms[7 - i] = (byte & (1u << i)) ? enc->bit1 : enc->bit0;
    }
    size_t encoded = 0;
    rmt_encode_state_t state = 0;
    encoded += enc->copy_enc->encode(enc->copy_enc, channel, syms,
                                     sizeof(syms), &state);
    *ret_state = state;
    return encoded;
}

/* ---------- strip instance state ------------------------------------------ */

typedef struct {
    rmt_channel_handle_t    channel;
    rmt_encoder_handle_t    encoder;
    ws2812_encoder_t       *enc_ctx;
    uint8_t                 led_count;
    uint8_t                 framebuffer[WS2812_MAX_LEDS * 3]; /* GRB */
    bool                    initialized;
} ws2812_state_t;

static ws2812_state_t      s_state[WS2812_MAX_INSTANCES];
static ws2812_encoder_t    s_enc_ctx[WS2812_MAX_INSTANCES];
static uint8_t             s_count = 0;

/* ---------- RMT encoder callbacks ----------------------------------------- */

static size_t ws2812_rmt_encode(rmt_encoder_t *encoder,
                                rmt_channel_handle_t channel,
                                const void *primary_data,
                                size_t data_size,
                                rmt_encode_state_t *ret_state)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    const uint8_t    *data = (const uint8_t *)primary_data;
    size_t            encoded = 0;
    rmt_encode_state_t state = 0;

    if (enc->phase == 0) {
        for (size_t i = 0; i < data_size; i++) {
            encoded += ws2812_encode_byte(enc, &state, data[i], channel, NULL);
            if (state & RMT_ENCODING_MEM_FULL) {
                *ret_state = state;
                return encoded;
            }
        }
        enc->phase = 1;
    }

    if (enc->phase == 1) {
        /* Append reset pulse */
        encoded += enc->copy_enc->encode(enc->copy_enc, channel,
                                         &enc->reset_sym, sizeof(enc->reset_sym), &state);
        if (state & RMT_ENCODING_COMPLETE) {
            enc->phase = 0;
            *ret_state = state;
        }
    }

    *ret_state = state;
    return encoded;
}

static esp_err_t ws2812_rmt_reset(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    enc->phase = 0;
    return enc->copy_enc->reset(enc->copy_enc);
}

static esp_err_t ws2812_rmt_delete(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *enc = __containerof(encoder, ws2812_encoder_t, base);
    return rmt_del_encoder(enc->copy_enc);
}

/* ---------- flush framebuffer to strip ------------------------------------ */

static esp_err_t ws2812_flush(ws2812_state_t *st)
{
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    esp_err_t err = rmt_transmit(st->channel, st->encoder,
                                 st->framebuffer,
                                 (size_t)(st->led_count * 3),
                                 &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %d", err);
        return err;
    }
    return rmt_tx_wait_all_done(st->channel, 100);
}

/* ---------- ABI ----------------------------------------------------------- */

static esp_err_t ws2812_init(const driver_config_t *cfg)
{
    if (s_count >= WS2812_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max WS2812 instances (%d) reached", WS2812_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    uint8_t led_count = cfg->led_count;
    if (led_count == 0) led_count = 1;
    if (led_count > WS2812_MAX_LEDS) {
        ESP_LOGE(TAG, "led_count %d exceeds max %d", led_count, WS2812_MAX_LEDS);
        return ESP_ERR_INVALID_ARG;
    }

    ws2812_state_t  *st  = &s_state[s_count];
    ws2812_encoder_t *enc = &s_enc_ctx[s_count];

    st->led_count = led_count;
    memset(st->framebuffer, 0, sizeof(st->framebuffer));

    /* Configure RMT TX channel */
    rmt_tx_channel_config_t ch_cfg = {
        .gpio_num          = cfg->pins[0].gpio_num,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RMT_CLK_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&ch_cfg, &st->channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %d", err);
        return err;
    }

    /* Build bit symbols */
    enc->bit0.level0 = 1; enc->bit0.duration0 = T0H_TICKS;
    enc->bit0.level1 = 0; enc->bit0.duration1 = T0L_TICKS;
    enc->bit1.level0 = 1; enc->bit1.duration0 = T1H_TICKS;
    enc->bit1.level1 = 0; enc->bit1.duration1 = T1L_TICKS;
    enc->reset_sym.level0 = 0; enc->reset_sym.duration0 = TRESET_TICKS;
    enc->reset_sym.level1 = 0; enc->reset_sym.duration1 = TRESET_TICKS;
    enc->phase = 0;

    /* Create inner copy encoder */
    rmt_copy_encoder_config_t copy_cfg = {};
    err = rmt_new_copy_encoder(&copy_cfg, &enc->copy_enc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %d", err);
        rmt_del_channel(st->channel);
        return err;
    }

    enc->base.encode = ws2812_rmt_encode;
    enc->base.reset  = ws2812_rmt_reset;
    enc->base.del    = ws2812_rmt_delete;
    st->encoder      = &enc->base;
    st->enc_ctx      = enc;

    err = rmt_enable(st->channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %d", err);
        rmt_del_channel(st->channel);
        return err;
    }

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "WS2812[%d] on GPIO%d, %d LEDs", s_count - 1,
             cfg->pins[0].gpio_num, led_count);
    return ESP_OK;
}

static esp_err_t ws2812_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    ws2812_state_t *st = &s_state[idx];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability != CAP_COLOR_RGB) return ESP_ERR_NOT_SUPPORTED;

    /* Decode packed RGB from int32: 0x00RRGGBB */
    int32_t packed;
    switch (cmd->type) {
        case VAL_TYPE_INT:  packed = cmd->i; break;
        case VAL_TYPE_FLOAT: packed = (int32_t)cmd->f; break;
        default: return ESP_ERR_INVALID_ARG;
    }

    uint8_t r = (uint8_t)((packed >> 16) & 0xFF);
    uint8_t g = (uint8_t)((packed >>  8) & 0xFF);
    uint8_t b = (uint8_t)( packed        & 0xFF);

    /* Fill all LEDs with the same colour (GRB byte order) */
    for (int i = 0; i < st->led_count; i++) {
        st->framebuffer[i * 3 + 0] = g;
        st->framebuffer[i * 3 + 1] = r;
        st->framebuffer[i * 3 + 2] = b;
    }

    ESP_LOGD(TAG, "WS2812[%d] → R%d G%d B%d × %d LEDs", idx, r, g, b, st->led_count);
    return ws2812_flush(st);
}

static esp_err_t ws2812_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    /* Actuator-only: return packed GRB of first pixel */
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    ws2812_state_t *st = &s_state[idx];

    if (field != CAP_COLOR_RGB) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t g = st->framebuffer[0];
    uint8_t r = st->framebuffer[1];
    uint8_t bv = st->framebuffer[2];

    out->type         = VAL_TYPE_INT;
    out->i            = (int32_t)(((uint32_t)r << 16) | ((uint32_t)g << 8) | bv);
    out->capability   = CAP_COLOR_RGB;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;
    return ESP_OK;
}

static esp_err_t ws2812_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count && s_state[idx].initialized) {
        /* Zero out the strip */
        memset(s_state[idx].framebuffer, 0, sizeof(s_state[idx].framebuffer));
        ws2812_flush(&s_state[idx]);
        rmt_disable(s_state[idx].channel);
        rmt_del_channel(s_state[idx].channel);
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t ws2812_meta = {
    .name             = "drv_ws2812",
    .display_name     = "WS2812B LED Strip",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_ACTUATOR,
    .bus_type         = BUS_TYPE_RMT,
    .capabilities     = {CAP_COLOR_RGB},
    .num_capabilities = 1,
    .max_latency_us   = 10000,
    .min_interval_ms  = 20,
    .failure_modes    = {
        {DRV_ERR_HW_FAULT, "RMT transmit failure", {.type = VAL_TYPE_INT, .i = 0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(ws2812_state_t),
};

const driver_vtable_t drv_ws2812_vtable = {
    .init   = ws2812_init,
    .read   = ws2812_read,
    .write  = ws2812_write,
    .deinit = ws2812_deinit,
    .meta   = &ws2812_meta,
};
