/**
 * @file drv_st7789.c
 * @brief ST7789 SPI TFT display driver — 240×240 (round/square variants).
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DRV_ST7789";

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t dc_pin;
    gpio_num_t rst_pin;
    bool       initialized;
} st7789_state_t;

static st7789_state_t s_state[2];
static uint8_t s_count = 0;

static void st7789_cmd(st7789_state_t *st, uint8_t cmd) {
    gpio_set_level(st->dc_pin, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_transmit(st->spi, &t);
}

static void st7789_data(st7789_state_t *st, const uint8_t *data, size_t len) {
    if (!len) return;
    gpio_set_level(st->dc_pin, 1);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    spi_device_transmit(st->spi, &t);
}

static void st7789_byte(st7789_state_t *st, uint8_t b) { st7789_data(st, &b, 1); }

static esp_err_t st7789_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    st7789_state_t *st = &s_state[s_count];

    st->dc_pin  = cfg->pins[0].gpio_num;
    st->rst_pin = cfg->pins[1].gpio_num;
    gpio_num_t cs_pin = cfg->pins[2].gpio_num;
    gpio_num_t sck    = cfg->pins[3].gpio_num;
    gpio_num_t mosi   = cfg->pins[2].gpio_num;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << st->dc_pin) | (1ULL << st->rst_pin),
        .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    spi_bus_config_t buscfg = {
        .mosi_io_num = mosi, .miso_io_num = -1, .sclk_io_num = sck,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 2 * 20,
    };
    spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 80 * 1000 * 1000,
        .mode = 0, .spics_io_num = cs_pin, .queue_size = 7,
    };
    spi_bus_add_device(SPI3_HOST, &devcfg, &st->spi);

    /* Reset */
    gpio_set_level(st->rst_pin, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(st->rst_pin, 1); vTaskDelay(pdMS_TO_TICKS(120));

    st7789_cmd(st, 0x01); vTaskDelay(pdMS_TO_TICKS(150)); /* SW reset */
    st7789_cmd(st, 0x11); vTaskDelay(pdMS_TO_TICKS(500)); /* sleep out */
    st7789_cmd(st, 0x3A); st7789_byte(st, 0x55);          /* 16-bit color */
    st7789_cmd(st, 0x36); st7789_byte(st, 0x00);          /* MADCTL normal */
    st7789_cmd(st, 0x2A);                                  /* column addr */
    uint8_t ca[4] = {0,0,0,239};
    st7789_data(st, ca, 4);
    st7789_cmd(st, 0x2B);                                  /* row addr */
    uint8_t ra[4] = {0,0,0,239};
    st7789_data(st, ra, 4);
    st7789_cmd(st, 0x29); vTaskDelay(pdMS_TO_TICKS(20));  /* display on */

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "ST7789 init OK");
    return ESP_OK;
}

static esp_err_t st7789_write(driver_handle_t h, const actuator_cmd_t *cmd) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    st7789_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability == CAP_COLOR_RGB) {
        uint16_t color = (uint16_t)cmd->i;
        uint8_t hi = color >> 8, lo = color & 0xFF;
        uint8_t row[480];
        for (int i = 0; i < 480; i += 2) { row[i] = hi; row[i+1] = lo; }
        st7789_cmd(st, 0x2C);
        for (int r = 0; r < 240; r++) st7789_data(st, row, 480);
    }
    return ESP_OK;
}

static esp_err_t st7789_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    (void)h; (void)field;
    out->error = DRV_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t st7789_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        st7789_cmd(&s_state[h.driver_index], 0x28);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t st7789_meta = {
    .name = "drv_st7789", .display_name = "ST7789 TFT 240x240",
    .version = "1.0.0", .type = DRIVER_TYPE_DISPLAY, .bus_type = BUS_TYPE_SPI,
    .capabilities = {CAP_COLOR_RGB, CAP_TEXT_DISPLAY}, .num_capabilities = 2,
    .max_latency_us = 200000, .min_interval_ms = 16,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "SPI failure", {.type=VAL_TYPE_BOOL,.b=false}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(st7789_state_t),
};

const driver_vtable_t drv_st7789_vtable = {
    .init=st7789_init, .read=st7789_read, .write=st7789_write, .deinit=st7789_deinit, .meta=&st7789_meta
};
