/**
 * @file drv_sh1106.c
 * @brief SH1106 132x64 OLED display — I2C, page-addressed (differs from SSD1306).
 *        Commonly used in 1.3" OLED modules.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DRV_SH1106";

#define SH1106_ADDR     0x3C
#define SH1106_CMD      0x00
#define SH1106_DATA     0x40

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

/* 5×7 ASCII font (same as SSD1306 driver) */
static const uint8_t FONT5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20 space */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
};

#define SH1106_W  132
#define SH1106_H  64
#define SH1106_PAGES (SH1106_H / 8)

typedef struct {
    uint8_t i2c_port;
    bool    initialized;
    uint8_t fb[SH1106_PAGES][SH1106_W]; /* full framebuffer */
} sh1106_state_t;

static sh1106_state_t s_state[2];
static uint8_t s_count = 0;

static void sh1106_cmd(uint8_t port, uint8_t cmd) {
    i2c_bus_write(port, SH1106_ADDR, SH1106_CMD, &cmd, 1);
}

static void sh1106_flush(sh1106_state_t *st) {
    for (uint8_t page = 0; page < SH1106_PAGES; page++) {
        sh1106_cmd(st->i2c_port, 0xB0 | page);     /* set page */
        sh1106_cmd(st->i2c_port, 0x02);             /* col low nibble (offset 2 for SH1106) */
        sh1106_cmd(st->i2c_port, 0x10);             /* col high nibble */
        /* Write 128 pixels (skip 2-pixel offset columns) */
        i2c_bus_write(st->i2c_port, SH1106_ADDR, SH1106_DATA, &st->fb[page][2], 128);
    }
}

static esp_err_t sh1106_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    sh1106_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;
    memset(st->fb, 0, sizeof(st->fb));

    const uint8_t init_seq[] = {
        0xAE, /* display off */
        0xD5, 0x80, /* clock div */
        0xA8, 0x3F, /* multiplex 64 */
        0xD3, 0x00, /* display offset 0 */
        0x40,       /* start line 0 */
        0xAD, 0x8B, /* charge pump on */
        0xA1,       /* seg remap */
        0xC8,       /* com scan dec */
        0xDA, 0x12, /* com pins */
        0x81, 0xFF, /* contrast */
        0xD9, 0x1F, /* precharge */
        0xDB, 0x40, /* vcomh */
        0xA4,       /* display all on resume */
        0xA6,       /* normal display */
        0xAF,       /* display on */
    };
    for (size_t i = 0; i < sizeof(init_seq); i++) {
        sh1106_cmd(st->i2c_port, init_seq[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    sh1106_flush(st);

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "SH1106 init OK on I2C%d", st->i2c_port);
    return ESP_OK;
}

static esp_err_t sh1106_write(driver_handle_t h, const actuator_cmd_t *cmd) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    sh1106_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;
    if (cmd->type != VAL_TYPE_STRING) return ESP_ERR_INVALID_ARG;

    memset(st->fb, 0, sizeof(st->fb));
    const char *text = cmd->s;
    uint8_t col = 2; /* SH1106 starts at col 2 */
    uint8_t page = 0;

    for (int i = 0; text[i] && page < SH1106_PAGES; i++) {
        char c = text[i];
        if (c == '\n') { page++; col = 2; continue; }
        if (c < 0x20 || c > '9') c = ' ';
        const uint8_t *glyph = FONT5x7[c - 0x20 < 26 ? c - 0x20 : 0];
        if (col + 6 > SH1106_W) { page++; col = 2; }
        for (int b = 0; b < 5; b++) st->fb[page][col++] = glyph[b];
        col++; /* 1 pixel spacing */
    }

    sh1106_flush(st);
    return ESP_OK;
}

static esp_err_t sh1106_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    (void)h; (void)field;
    out->error = DRV_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t sh1106_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        sh1106_cmd(s_state[h.driver_index].i2c_port, 0xAE);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t sh1106_meta = {
    .name = "drv_sh1106", .display_name = "SH1106 OLED 128x64",
    .version = "1.0.0", .type = DRIVER_TYPE_DISPLAY, .bus_type = BUS_TYPE_I2C,
    .capabilities = {CAP_TEXT_DISPLAY}, .num_capabilities = 1,
    .max_latency_us = 50000, .min_interval_ms = 50,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C failure", {.type=VAL_TYPE_BOOL,.b=false}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(sh1106_state_t),
};

const driver_vtable_t drv_sh1106_vtable = {
    .init=sh1106_init, .read=sh1106_read, .write=sh1106_write, .deinit=sh1106_deinit, .meta=&sh1106_meta
};
