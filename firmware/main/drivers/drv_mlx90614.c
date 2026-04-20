/**
 * @file drv_mlx90614.c
 * @brief MLX90614 I2C IR non-contact thermometer driver.
 *
 * I2C address: 0x5A (default; factory-programmable).
 *
 * Protocol: SMBus with PEC (CRC-8, polynomial 0x07).
 * Each read returns 3 bytes: LSB, MSB, PEC byte.
 *
 * RAM read commands:
 *   0x06 – Ta  (ambient temperature)
 *   0x07 – Tobj1 (object temperature)
 *
 * Raw to Kelvin: temperature_K = raw * 0.02
 * Celsius: temperature_C = temperature_K - 273.15
 *
 * Error flags:
 *   Bit 15 of raw = error flag; if set, reading is invalid.
 *
 * PEC verification: CRC-8 over [addr_write, cmd, addr_read, LSB, MSB].
 * The i2c_bus_write/read API doesn't natively support SMBus PEC, so we
 * implement a raw SMBus read via i2c_cmd_handle_t directly.
 *
 * Read CAP_TEMPERATURE: object temperature in °C (Tobj1, addr 0x07).
 *
 * Max 4 instances.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include <string.h>

static const char *TAG = "DRV_MLX90614";

#define MLX90614_DEFAULT_ADDR   0x5A
#define MLX90614_CMD_TA         0x06   /* ambient temperature RAM register */
#define MLX90614_CMD_TOBJ1      0x07   /* object temperature 1 RAM register */
#define MLX90614_MAX_INSTANCES  4
#define MLX90614_RAW_SCALE      0.02f  /* K per LSB */
#define MLX90614_KELVIN_OFFSET  273.15f
#define I2C_TIMEOUT_MS          100

typedef struct {
    uint8_t i2c_port;
    uint8_t address;
    float   t_object;  /* °C */
    float   t_ambient; /* °C */
    bool    initialized;
} mlx90614_state_t;

static mlx90614_state_t s_state[MLX90614_MAX_INSTANCES];
static uint8_t s_count = 0;

/* CRC-8 (polynomial 0x07, as used in SMBus PEC) */
static uint8_t crc8(uint8_t crc, uint8_t data)
{
    crc ^= data;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x80) {
            crc = (crc << 1) ^ 0x07;
        } else {
            crc <<= 1;
        }
    }
    return crc;
}

/**
 * SMBus word read with PEC verification.
 * Sends: START, addr_W, cmd, rSTA, addr_R, LSB, MSB, PEC, STOP
 * Returns raw 15-bit value (bit 15 = error flag stripped separately).
 */
static esp_err_t mlx90614_smbus_read(uint8_t port, uint8_t addr, uint8_t cmd, uint16_t *out)
{
    uint8_t buf[3] = {0};

    i2c_cmd_handle_t h = i2c_cmd_link_create();

    /* Write command byte */
    i2c_master_start(h);
    i2c_master_write_byte(h, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, cmd, true);

    /* Repeated start — read 3 bytes (LSB, MSB, PEC) */
    i2c_master_start(h);
    i2c_master_write_byte(h, (addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(h, buf, 2, I2C_MASTER_ACK);
    i2c_master_read_byte(h, &buf[2], I2C_MASTER_NACK);
    i2c_master_stop(h);

    esp_err_t err = i2c_master_cmd_begin(port, h, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(h);

    if (err != ESP_OK) return err;

    /* Verify PEC: CRC over write-addr, cmd, read-addr, LSB, MSB */
    uint8_t pec = 0;
    pec = crc8(pec, (addr << 1) | 0);   /* write address */
    pec = crc8(pec, cmd);
    pec = crc8(pec, (addr << 1) | 1);   /* read address */
    pec = crc8(pec, buf[0]);             /* LSB */
    pec = crc8(pec, buf[1]);             /* MSB */

    if (pec != buf[2]) {
        ESP_LOGW(TAG, "PEC mismatch: calc=0x%02X rx=0x%02X", pec, buf[2]);
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw = (uint16_t)((buf[1] << 8) | buf[0]);

    /* Bit 15 = error flag */
    if (raw & 0x8000) {
        ESP_LOGE(TAG, "MLX90614 error flag set in raw data");
        return ESP_FAIL;
    }

    *out = raw & 0x7FFF;
    return ESP_OK;
}

static esp_err_t mlx90614_init(const driver_config_t *cfg)
{
    if (s_count >= MLX90614_MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max MLX90614 instances (%d) reached", MLX90614_MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    mlx90614_state_t *st = &s_state[s_count];
    st->i2c_port = cfg->bus_index;
    st->address  = cfg->i2c_address ? cfg->i2c_address : MLX90614_DEFAULT_ADDR;
    st->t_object = st->t_ambient = 25.0f;

    /* Verify sensor responds by reading object temperature */
    uint16_t raw = 0;
    esp_err_t err = mlx90614_smbus_read(st->i2c_port, st->address, MLX90614_CMD_TOBJ1, &raw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sensor probe failed at 0x%02X on I2C%d: %d",
                 st->address, st->i2c_port, err);
        return err;
    }

    st->t_object = (float)raw * MLX90614_RAW_SCALE - MLX90614_KELVIN_OFFSET;

    st->initialized = true;
    s_count++;
    ESP_LOGI(TAG, "MLX90614[%d] at 0x%02X on I2C%d, Tobj=%.2f°C",
             s_count - 1, st->address, st->i2c_port, st->t_object);
    return ESP_OK;
}

static esp_err_t mlx90614_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index;
    if (idx >= s_count) idx = 0;
    mlx90614_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    if (field != CAP_TEMPERATURE) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Read object temperature (Tobj1) */
    uint16_t raw = 0;
    esp_err_t err = mlx90614_smbus_read(st->i2c_port, st->address, MLX90614_CMD_TOBJ1, &raw);
    if (err == ESP_ERR_INVALID_CRC) {
        out->error = DRV_ERR_CRC;
        return err;
    }
    if (err != ESP_OK) {
        out->error = DRV_ERR_BUS_FAIL;
        return err;
    }

    float temp_c = (float)raw * MLX90614_RAW_SCALE - MLX90614_KELVIN_OFFSET;
    st->t_object = temp_c;

    out->type         = VAL_TYPE_FLOAT;
    out->f            = temp_c;
    out->capability   = CAP_TEMPERATURE;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->error        = DRV_OK;

    ESP_LOGD(TAG, "MLX90614[%d] Tobj=%.2f°C (raw=%u)", idx, temp_c, raw);
    return ESP_OK;
}

static esp_err_t mlx90614_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    (void)h; (void)cmd;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t mlx90614_deinit(driver_handle_t h)
{
    uint8_t idx = h.driver_index;
    if (idx < s_count) {
        s_state[idx].initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t mlx90614_meta = {
    .name             = "drv_mlx90614",
    .display_name     = "MLX90614 IR Thermometer",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_TEMPERATURE},
    .num_capabilities = 1,
    .max_latency_us   = 5000,
    .min_interval_ms  = 50,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C/SMBus read failure",  {.type = VAL_TYPE_FLOAT, .f = 0.0f}},
        {DRV_ERR_CRC,      "PEC checksum mismatch",   {.type = VAL_TYPE_FLOAT, .f = 0.0f}},
    },
    .num_failure_modes   = 2,
    .internal_state_size = sizeof(mlx90614_state_t),
};

const driver_vtable_t drv_mlx90614_vtable = {
    .init   = mlx90614_init,
    .read   = mlx90614_read,
    .write  = mlx90614_write,
    .deinit = mlx90614_deinit,
    .meta   = &mlx90614_meta,
};
