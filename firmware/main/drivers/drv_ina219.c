/**
 * @file drv_ina219.c
 * @brief INA219 I2C power monitor driver (voltage, current, power).
 *
 * I2C address: 0x40 (default, A0=A1=GND).
 *
 * Configuration register 0x00: write 0x399F
 *   Bus voltage range = 32V, PGA = /8 (±320 mV shunt), BADC/SADC = 12-bit,
 *   continuous shunt+bus mode.
 *
 * Registers:
 *   0x01  Shunt voltage (signed 16-bit, LSB = 10 µV)
 *   0x02  Bus voltage   (signed 16-bit, shift right 3, LSB = 4 mV)
 *   0x03  Power         (unsigned 16-bit, LSB = 20 mW when current_lsb=1 mA)
 *   0x04  Current       (signed 16-bit; requires calibration reg 0x05)
 *
 * Calibration for 100 mΩ shunt, current_lsb = 0.1 mA:
 *   CAL = trunc(0.04096 / (current_lsb * R_shunt))
 *       = trunc(0.04096 / (0.0001 * 0.1)) = 4096 = 0x1000
 *
 * Current [mA] = shunt_raw * 0.01f  (shunt LSB 10 µV / 1 mΩ → not used here;
 *   we derive current from shunt voltage directly: I = Vshunt / R_shunt).
 *   With R_shunt = 0.1 Ω: I[mA] = shunt_raw * 10 µV / 0.1 Ω = raw * 0.1 mA.
 *
 * Power [mW] = voltage [mV] * current [mA] / 1000.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DRV_INA219";

#define INA219_DEFAULT_ADDR     0x40
#define INA219_REG_CONFIG       0x00
#define INA219_REG_SHUNT_V      0x01
#define INA219_REG_BUS_V        0x02
#define INA219_REG_POWER        0x03
#define INA219_REG_CURRENT      0x04
#define INA219_REG_CALIB        0x05
#define INA219_MAX_INSTANCES    4

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       voltage_mv;
    float       current_ma;
    float       power_mw;
} ina219_state_t;

static ina219_state_t s_state[INA219_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t ina219_write_reg16(ina219_state_t *st, uint8_t reg, uint16_t val)
{
    uint8_t buf[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return i2c_bus_write(st->i2c_port, st->address, reg, buf, 2);
}

static esp_err_t ina219_read_reg16(ina219_state_t *st, uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, reg, buf, 2);
    if (err == ESP_OK) *out = ((uint16_t)buf[0] << 8) | buf[1];
    return err;
}

static esp_err_t ina219_init(const driver_config_t *cfg)
{
    if (s_instance_count >= INA219_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    ina219_state_t *st = &s_state[s_instance_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : INA219_DEFAULT_ADDR;

    /* Configure: 32V range, ±320 mV PGA, 12-bit ADC, continuous */
    esp_err_t err = ina219_write_reg16(st, INA219_REG_CONFIG, 0x399F);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config write failed: %d", err);
        return err;
    }

    /* Calibration for 0.1 Ω shunt, 0.1 mA/LSB: CAL = 4096 */
    ina219_write_reg16(st, INA219_REG_CALIB, 0x1000);

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "INA219 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

static esp_err_t ina219_sample(ina219_state_t *st)
{
    uint16_t raw_s = 0, raw_b = 0;

    esp_err_t err = ina219_read_reg16(st, INA219_REG_SHUNT_V, &raw_s);
    if (err != ESP_OK) return err;
    err = ina219_read_reg16(st, INA219_REG_BUS_V, &raw_b);
    if (err != ESP_OK) return err;

    /* Shunt voltage: signed, LSB = 10 µV → convert to µV then to mA */
    int16_t shunt_signed = (int16_t)raw_s;
    float   shunt_uv     = (float)shunt_signed * 10.0f;   /* µV */
    /* I = Vshunt / R_shunt = shunt_uv µV / (0.1 Ω) = shunt_uv * 10 µA = shunt_uv/100 mA */
    st->current_ma = shunt_uv / 100.0f;

    /* Bus voltage: bits [15:3], LSB = 4 mV */
    uint16_t bus_raw = raw_b >> 3;
    st->voltage_mv   = (float)bus_raw * 4.0f;

    /* Power: derived */
    st->power_mw = (st->voltage_mv * st->current_ma) / 1000.0f;
    return ESP_OK;
}

static esp_err_t ina219_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    ina219_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    esp_err_t err = ina219_sample(st);
    if (err != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return err;
    }

    out->type         = VAL_TYPE_FLOAT;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->capability   = field;

    switch (field) {
        case CAP_VOLTAGE: out->f = st->voltage_mv;  break;
        case CAP_CURRENT: out->f = st->current_ma;  break;
        case CAP_POWER:   out->f = st->power_mw;    break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t ina219_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Power-down mode */
        ina219_state_t *st = &s_state[h.driver_index];
        ina219_write_reg16(st, INA219_REG_CONFIG, 0x0000);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t ina219_meta = {
    .name             = "drv_ina219",
    .display_name     = "INA219 Power Monitor",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_VOLTAGE, CAP_CURRENT, CAP_POWER},
    .num_capabilities = 3,
    .max_latency_us   = 1000,
    .min_interval_ms  = 10,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(ina219_state_t),
};

const driver_vtable_t drv_ina219_vtable = {
    .init   = ina219_init,
    .read   = ina219_read,
    .write  = NULL,
    .deinit = ina219_deinit,
    .meta   = &ina219_meta,
};
