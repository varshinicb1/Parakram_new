/**
 * @file drv_ili9341.c
 * @brief ILI9341 SPI TFT display driver — 240×320 color.
 *        Uses bit-bang SPI via GPIO to avoid needing the SPI bus abstraction.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DRV_ILI9341";

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t dc_pin;
    gpio_num_t rst_pin;
    bool       initialized;
} ili9341_state_t;

static ili9341_state_t s_state[2];
static uint8_t s_count = 0;

static void ili9341_send_cmd(ili9341_state_t *st, uint8_t cmd) {
    gpio_set_level(st->dc_pin, 0); /* command mode */
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_transmit(st->spi, &t);
}

static void ili9341_send_data(ili9341_state_t *st, const uint8_t *data, size_t len) {
    gpio_set_level(st->dc_pin, 1); /* data mode */
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    spi_device_transmit(st->spi, &t);
}

static void ili9341_send_byte(ili9341_state_t *st, uint8_t byte) {
    ili9341_send_data(st, &byte, 1);
}

static esp_err_t ili9341_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    ili9341_state_t *st = &s_state[s_count];

    st->dc_pin  = cfg->pins[0].gpio_num;
    st->rst_pin = cfg->pins[1].gpio_num;
    gpio_num_t cs_pin = cfg->pins[2].gpio_num;
    gpio_num_t sck    = cfg->pins[3].gpio_num;

    /* DC and RST as output */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << st->dc_pin) | (1ULL << st->rst_pin),
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg->pins[2].gpio_num, /* reusing field */
        .miso_io_num = -1,
        .sclk_io_num = sck,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 2 * 20,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 7,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &st->spi);

    /* Hardware reset */
    gpio_set_level(st->rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(st->rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Initialization sequence */
    ili9341_send_cmd(st, 0x01); vTaskDelay(pdMS_TO_TICKS(5)); /* SW reset */
    ili9341_send_cmd(st, 0x11); vTaskDelay(pdMS_TO_TICKS(120)); /* sleep out */
    ili9341_send_cmd(st, 0x3A); ili9341_send_byte(st, 0x55); /* 16-bit color */
    ili9341_send_cmd(st, 0x36); ili9341_send_byte(st, 0x48); /* MADCTL */
    ili9341_send_cmd(st, 0x29); /* display on */

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "ILI9341 init OK");
    return ESP_OK;
}

static esp_err_t ili9341_write(driver_handle_t h, const actuator_cmd_t *cmd) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    ili9341_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (cmd->capability == CAP_COLOR_RGB) {
        /* Fill screen with color */
        uint16_t color = (uint16_t)cmd->i;
        uint8_t hi = color >> 8, lo = color & 0xFF;
        ili9341_send_cmd(st, 0x2A); /* column */
        uint8_t ca[4] = {0,0,0,239};
        ili9341_send_data(st, ca, 4);
        ili9341_send_cmd(st, 0x2B); /* row */
        uint8_t ra[4] = {0,0,1,63};
        ili9341_send_data(st, ra, 4);
        ili9341_send_cmd(st, 0x2C); /* write */
        /* Send 240*320 pixels — batch of 240*2 bytes per row */
        uint8_t row[480];
        for (int i = 0; i < 480; i += 2) { row[i] = hi; row[i+1] = lo; }
        for (int r = 0; r < 320; r++) ili9341_send_data(st, row, 480);
    }
    return ESP_OK;
}

static esp_err_t ili9341_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    (void)h; (void)field;
    out->error = DRV_ERR_NOT_SUPPORTED;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t ili9341_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        ili9341_send_cmd(&s_state[h.driver_index], 0x28); /* display off */
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t ili9341_meta = {
    .name = "drv_ili9341", .display_name = "ILI9341 TFT 240x320",
    .version = "1.0.0", .type = DRIVER_TYPE_DISPLAY, .bus_type = BUS_TYPE_SPI,
    .capabilities = {CAP_COLOR_RGB, CAP_TEXT_DISPLAY}, .num_capabilities = 2,
    .max_latency_us = 500000, .min_interval_ms = 16,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "SPI failure", {.type=VAL_TYPE_BOOL,.b=false}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(ili9341_state_t),
};

const driver_vtable_t drv_ili9341_vtable = {
    .init=ili9341_init, .read=ili9341_read, .write=ili9341_write, .deinit=ili9341_deinit, .meta=&ili9341_meta
};
