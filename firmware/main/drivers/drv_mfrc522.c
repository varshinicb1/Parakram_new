/**
 * @file drv_mfrc522.c
 * @brief MFRC522 SPI RFID reader — 13.56 MHz ISO-14443A.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DRV_MFRC522";

/* Key registers */
#define MFRC522_REG_COMMAND     0x01
#define MFRC522_REG_COMIEN      0x02
#define MFRC522_REG_DIV1EN      0x03
#define MFRC522_REG_COMIRQ      0x04
#define MFRC522_REG_ERROR       0x06
#define MFRC522_REG_STATUS2     0x08
#define MFRC522_REG_FIFO_DATA   0x09
#define MFRC522_REG_FIFO_LEVEL  0x0A
#define MFRC522_REG_BIT_FRAMING 0x0D
#define MFRC522_REG_MODE        0x11
#define MFRC522_REG_TX_CTRL     0x14
#define MFRC522_REG_TX_ASK      0x15
#define MFRC522_REG_VERSION     0x37

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

/* Using I2C variant (MFRC522 has I2C mode at 0x28) */
#define MFRC522_ADDR 0x28

typedef struct {
    uint8_t  i2c_port;
    bool     initialized;
    uint8_t  last_uid[4];
    uint8_t  last_uid_len;
    bool     card_present;
} mfrc522_state_t;

static mfrc522_state_t s_state[2];
static uint8_t s_count = 0;

static void mfrc522_write(uint8_t port, uint8_t reg, uint8_t val) {
    i2c_bus_write(port, MFRC522_ADDR, reg, &val, 1);
}
static uint8_t mfrc522_read_reg(uint8_t port, uint8_t reg) {
    uint8_t v = 0;
    i2c_bus_read(port, MFRC522_ADDR, reg, &v, 1);
    return v;
}

static esp_err_t mfrc522_init(const driver_config_t *cfg) {
    if (s_count >= 2) return ESP_ERR_NO_MEM;
    mfrc522_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;

    uint8_t ver = mfrc522_read_reg(st->i2c_port, MFRC522_REG_VERSION);
    if (ver != 0x91 && ver != 0x92) {
        ESP_LOGE(TAG, "Bad version: 0x%02X", ver);
        return ESP_FAIL;
    }

    /* Soft reset */
    mfrc522_write(st->i2c_port, MFRC522_REG_COMMAND, 0x0F);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Configure */
    mfrc522_write(st->i2c_port, MFRC522_REG_TX_ASK, 0x40); /* 100% ASK */
    mfrc522_write(st->i2c_port, MFRC522_REG_MODE, 0x3D);    /* CRC preset 0x6363 */
    /* Enable antenna */
    uint8_t tx = mfrc522_read_reg(st->i2c_port, MFRC522_REG_TX_CTRL);
    mfrc522_write(st->i2c_port, MFRC522_REG_TX_CTRL, tx | 0x03);

    st->initialized = true;
    st->card_present = false;
    s_count++;
    ESP_LOGI(TAG, "MFRC522 v0x%02X init OK on I2C%d", ver, st->i2c_port);
    return ESP_OK;
}

static esp_err_t mfrc522_read(driver_handle_t h, capability_t field, sensor_value_t *out) {
    if (h.driver_index >= s_count) return ESP_ERR_INVALID_ARG;
    mfrc522_state_t *st = &s_state[h.driver_index];
    if (!st->initialized) return ESP_ERR_INVALID_STATE;

    if (field != CAP_PROXIMITY) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* REQA command to detect card */
    mfrc522_write(st->i2c_port, MFRC522_REG_COMIRQ, 0x7F);
    mfrc522_write(st->i2c_port, MFRC522_REG_FIFO_LEVEL, 0x80); /* flush */
    mfrc522_write(st->i2c_port, MFRC522_REG_FIFO_DATA, 0x26);  /* REQA */
    mfrc522_write(st->i2c_port, MFRC522_REG_BIT_FRAMING, 0x07);
    mfrc522_write(st->i2c_port, MFRC522_REG_COMMAND, 0x0C); /* Transceive */
    mfrc522_write(st->i2c_port, MFRC522_REG_BIT_FRAMING, 0x87);

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t irq = mfrc522_read_reg(st->i2c_port, MFRC522_REG_COMIRQ);
    st->card_present = (irq & 0x20) != 0; /* RxIRq */

    out->type = VAL_TYPE_BOOL;
    out->b = st->card_present;
    out->capability = CAP_PROXIMITY;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

static esp_err_t mfrc522_deinit(driver_handle_t h) {
    if (h.driver_index < s_count) {
        /* Disable antenna */
        uint8_t tx = mfrc522_read_reg(s_state[h.driver_index].i2c_port, MFRC522_REG_TX_CTRL);
        mfrc522_write(s_state[h.driver_index].i2c_port, MFRC522_REG_TX_CTRL, tx & ~0x03);
        s_state[h.driver_index].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t mfrc522_meta = {
    .name = "drv_mfrc522", .display_name = "MFRC522 RFID Reader",
    .version = "1.0.0", .type = DRIVER_TYPE_SENSOR, .bus_type = BUS_TYPE_SPI,
    .capabilities = {CAP_PROXIMITY}, .num_capabilities = 1,
    .max_latency_us = 15000, .min_interval_ms = 100,
    .failure_modes = {{DRV_ERR_BUS_FAIL, "I2C/SPI failure", {.type=VAL_TYPE_BOOL,.b=false}}},
    .num_failure_modes = 1, .internal_state_size = sizeof(mfrc522_state_t),
};

const driver_vtable_t drv_mfrc522_vtable = {
    .init=mfrc522_init, .read=mfrc522_read, .write=NULL, .deinit=mfrc522_deinit, .meta=&mfrc522_meta
};
