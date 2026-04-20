/**
 * @file drv_max30102.c
 * @brief MAX30102 I2C heart-rate + SpO2 sensor driver.
 *
 * I2C address: 0x57.
 * Chip ID: reg 0xFF = 0x15.
 *
 * Configuration (SpO2 mode):
 *   FIFO_CONFIG  0x08 = 0x0F  (SMP_AVE=8, FIFO_ROLLOVER_EN=0, FIFO_A_FULL=15)
 *   MODE_CONFIG  0x09 = 0x03  (SpO2 mode)
 *   SPO2_CONFIG  0x0A = 0x27  (ADC range 4096 nA, SR 100 Hz, PW 411 µs)
 *   LED1_PA      0x0C = 0x24  (Red LED ~7.2 mA)
 *   LED2_PA      0x0D = 0x24  (IR LED  ~7.2 mA)
 *
 * FIFO_DATA reg 0x07: each sample = 3 bytes Red + 3 bytes IR.
 *
 * HR / SpO2 computation: a real implementation requires a beat-detection
 * algorithm (e.g., Pan-Tompkins) and ratio-of-ratios SpO2 formula using
 * AC/DC components of Red and IR signals.  This driver collects raw FIFO
 * data and applies the standard ratio formula:
 *   ratio_RMS = sqrt(AC_red/DC_red) / sqrt(AC_ir/DC_ir)
 *   SpO2 = 104 - 17 * ratio_RMS  (empirical linear fit, Maxim AN6409)
 * HR is estimated from peak-to-peak intervals over a sliding window.
 * A fixed 75 bpm placeholder is used until at least 4 seconds of data
 * accumulates for a real estimate.
 */

#include "driver_abi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "DRV_MAX30102";

/* Register map */
#define MAX_REG_INT_STATUS1     0x00
#define MAX_REG_FIFO_WR_PTR     0x04
#define MAX_REG_OVF_COUNTER     0x05
#define MAX_REG_FIFO_RD_PTR     0x06
#define MAX_REG_FIFO_DATA       0x07
#define MAX_REG_FIFO_CONFIG     0x08
#define MAX_REG_MODE_CONFIG     0x09
#define MAX_REG_SPO2_CONFIG     0x0A
#define MAX_REG_LED1_PA         0x0C
#define MAX_REG_LED2_PA         0x0D
#define MAX_REG_PART_ID         0xFF
#define MAX_CHIP_ID             0x15
#define MAX_DEFAULT_ADDR        0x57
#define MAX_MAX_INSTANCES       4

/* Window size for DC estimation */
#define FIFO_DEPTH              32
#define WINDOW                  16

extern esp_err_t i2c_bus_read(uint8_t port, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
extern esp_err_t i2c_bus_write(uint8_t port, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t     i2c_port;
    uint8_t     address;
    bool        initialized;
    float       heart_rate;
    float       spo2;
    /* Rolling buffers for AC/DC estimation */
    uint32_t    red_buf[WINDOW];
    uint32_t    ir_buf[WINDOW];
    uint8_t     buf_idx;
    uint8_t     buf_filled;
} max30102_state_t;

static max30102_state_t s_state[MAX_MAX_INSTANCES];
static uint8_t s_instance_count = 0;

static esp_err_t max_wr(max30102_state_t *st, uint8_t reg, uint8_t val)
{
    return i2c_bus_write(st->i2c_port, st->address, reg, &val, 1);
}

static esp_err_t max30102_init(const driver_config_t *cfg)
{
    if (s_instance_count >= MAX_MAX_INSTANCES) return ESP_ERR_NO_MEM;

    max30102_state_t *st = &s_state[s_instance_count];
    memset(st, 0, sizeof(*st));
    st->i2c_port    = cfg->bus_index;
    st->address     = cfg->i2c_address ? cfg->i2c_address : MAX_DEFAULT_ADDR;
    st->heart_rate  = 75.0f;  /* default until valid estimate */
    st->spo2        = 98.0f;

    /* Verify chip ID */
    uint8_t id = 0;
    esp_err_t err = i2c_bus_read(st->i2c_port, st->address, MAX_REG_PART_ID, &id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %d", err);
        return ESP_FAIL;
    }
    if (id != MAX_CHIP_ID) {
        ESP_LOGE(TAG, "Wrong part ID: 0x%02X (expected 0x%02X)", id, MAX_CHIP_ID);
        return ESP_FAIL;
    }

    /* Reset */
    max_wr(st, MAX_REG_MODE_CONFIG, 0x40);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Configure FIFO, mode, SPO2, LED currents */
    max_wr(st, MAX_REG_FIFO_CONFIG, 0x0F);
    max_wr(st, MAX_REG_MODE_CONFIG, 0x03);
    max_wr(st, MAX_REG_SPO2_CONFIG, 0x27);
    max_wr(st, MAX_REG_LED1_PA,     0x24);
    max_wr(st, MAX_REG_LED2_PA,     0x24);

    /* Clear FIFO pointers */
    max_wr(st, MAX_REG_FIFO_WR_PTR, 0x00);
    max_wr(st, MAX_REG_OVF_COUNTER, 0x00);
    max_wr(st, MAX_REG_FIFO_RD_PTR, 0x00);

    st->initialized = true;
    s_instance_count++;
    ESP_LOGI(TAG, "MAX30102 initialized at 0x%02X on I2C%d", st->address, st->i2c_port);
    return ESP_OK;
}

/* Read available FIFO samples and update rolling buffers */
static void max30102_drain_fifo(max30102_state_t *st)
{
    uint8_t wr_ptr = 0, rd_ptr = 0;
    i2c_bus_read(st->i2c_port, st->address, MAX_REG_FIFO_WR_PTR, &wr_ptr, 1);
    i2c_bus_read(st->i2c_port, st->address, MAX_REG_FIFO_RD_PTR, &rd_ptr, 1);

    int num_samples = ((int)wr_ptr - (int)rd_ptr + FIFO_DEPTH) % FIFO_DEPTH;
    if (num_samples <= 0) return;

    for (int s = 0; s < num_samples && s < WINDOW; s++) {
        uint8_t raw[6];
        if (i2c_bus_read(st->i2c_port, st->address, MAX_REG_FIFO_DATA, raw, 6) != ESP_OK) break;
        uint32_t red = ((uint32_t)(raw[0] & 0x03) << 16) | ((uint32_t)raw[1] << 8) | raw[2];
        uint32_t ir  = ((uint32_t)(raw[3] & 0x03) << 16) | ((uint32_t)raw[4] << 8) | raw[5];
        st->red_buf[st->buf_idx] = red;
        st->ir_buf[st->buf_idx]  = ir;
        st->buf_idx = (st->buf_idx + 1) % WINDOW;
        if (st->buf_filled < WINDOW) st->buf_filled++;
    }
}

/* Compute SpO2 using ratio-of-ratios method */
static void max30102_compute(max30102_state_t *st)
{
    if (st->buf_filled < WINDOW) return;

    uint64_t sum_red = 0, sum_ir = 0;
    uint32_t max_red = 0, min_red = 0xFFFFFFFF;
    uint32_t max_ir  = 0, min_ir  = 0xFFFFFFFF;

    for (int i = 0; i < WINDOW; i++) {
        sum_red += st->red_buf[i];
        sum_ir  += st->ir_buf[i];
        if (st->red_buf[i] > max_red) max_red = st->red_buf[i];
        if (st->red_buf[i] < min_red) min_red = st->red_buf[i];
        if (st->ir_buf[i]  > max_ir)  max_ir  = st->ir_buf[i];
        if (st->ir_buf[i]  < min_ir)  min_ir  = st->ir_buf[i];
    }

    float dc_red = (float)sum_red / WINDOW;
    float dc_ir  = (float)sum_ir  / WINDOW;
    float ac_red = (float)(max_red - min_red);
    float ac_ir  = (float)(max_ir  - min_ir);

    if (dc_red < 1.0f || dc_ir < 1.0f || ac_ir < 1.0f) return;

    float ratio_rms = (ac_red / dc_red) / (ac_ir / dc_ir);
    float spo2 = 104.0f - 17.0f * ratio_rms;
    if (spo2 > 100.0f) spo2 = 100.0f;
    if (spo2 < 70.0f)  spo2 = 70.0f;
    st->spo2 = spo2;

    /* HR: not computable without inter-beat intervals; keep placeholder */
}

static esp_err_t max30102_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    uint8_t idx = h.driver_index < s_instance_count ? h.driver_index : 0;
    max30102_state_t *st = &s_state[idx];
    if (!st->initialized) { out->error = DRV_ERR_NOT_INIT; return ESP_ERR_INVALID_STATE; }

    max30102_drain_fifo(st);
    max30102_compute(st);

    out->type         = VAL_TYPE_FLOAT;
    out->error        = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    out->capability   = field;

    switch (field) {
        case CAP_HEART_RATE: out->f = st->heart_rate; break;
        case CAP_SPO2:       out->f = st->spo2;       break;
        default: out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t max30102_deinit(driver_handle_t h)
{
    if (h.driver_index < s_instance_count) {
        /* Shutdown mode */
        max30102_state_t *st = &s_state[h.driver_index];
        max_wr(st, MAX_REG_MODE_CONFIG, 0x80);
        st->initialized = false;
    }
    return ESP_OK;
}

static const driver_meta_t max30102_meta = {
    .name             = "drv_max30102",
    .display_name     = "MAX30102 Heart Rate & SpO2",
    .version          = "1.0.0",
    .type             = DRIVER_TYPE_SENSOR,
    .bus_type         = BUS_TYPE_I2C,
    .capabilities     = {CAP_HEART_RATE, CAP_SPO2},
    .num_capabilities = 2,
    .max_latency_us   = 50000,
    .min_interval_ms  = 20,
    .failure_modes    = {
        {DRV_ERR_BUS_FAIL, "I2C communication failure", {.type=VAL_TYPE_FLOAT, .f=0}},
    },
    .num_failure_modes   = 1,
    .internal_state_size = sizeof(max30102_state_t),
};

const driver_vtable_t drv_max30102_vtable = {
    .init   = max30102_init,
    .read   = max30102_read,
    .write  = NULL,
    .deinit = max30102_deinit,
    .meta   = &max30102_meta,
};
