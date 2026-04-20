/**
 * @file i2c_bus.c
 * @brief I2C bus abstraction layer.
 */

#include "esp_log.h"
#include "driver/i2c.h"
#include <string.h>

static const char *TAG = "I2C_BUS";

#define I2C_TIMEOUT_MS  100
#define I2C_MAX_PORTS   2

static bool s_initialized[I2C_MAX_PORTS] = {false, false};

esp_err_t i2c_bus_init(uint8_t port, int sda_pin, int scl_pin, uint32_t freq_hz) {
    if (port >= I2C_MAX_PORTS) return ESP_ERR_INVALID_ARG;
    if (s_initialized[port]) return ESP_OK;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = freq_hz,
    };

    esp_err_t err = i2c_param_config(port, &conf);
    if (err != ESP_OK) return err;

    err = i2c_driver_install(port, conf.mode, 0, 0, 0);
    if (err != ESP_OK) return err;

    s_initialized[port] = true;
    ESP_LOGI(TAG, "I2C%d initialized: SDA=%d SCL=%d freq=%luHz", port, sda_pin, scl_pin, freq_hz);
    return ESP_OK;
}

esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len) {
    if (port >= I2C_MAX_PORTS || !s_initialized[port]) return ESP_ERR_INVALID_STATE;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, &data[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len) {
    if (port >= I2C_MAX_PORTS || !s_initialized[port]) return ESP_ERR_INVALID_STATE;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err;
}
