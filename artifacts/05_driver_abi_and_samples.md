# Artifact 5 — Driver ABI + Sample Drivers

## Part 1: Driver ABI Header (`driver_abi.h`)

```c
/**
 * @file driver_abi.h
 * @brief Parakram Driver Application Binary Interface
 *
 * All Vidyuthlabs-registered drivers MUST conform to this ABI.
 * No exceptions. No extensions. No optional methods.
 *
 * Rules:
 *   - read() and write() must return within max_latency_us
 *   - Must never block — use DMA or pre-staged buffers
 *   - Must declare resource usage: bus, pins, interrupts
 *   - Must declare failure modes and safe fallback values
 *   - No dynamic memory allocation (malloc, calloc, realloc, new)
 *   - No stdio, no printf in production (use ESP_LOG macros only)
 */

#ifndef DRIVER_ABI_H
#define DRIVER_ABI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Version
 * ============================================================ */
#define DRIVER_ABI_VERSION_MAJOR    1
#define DRIVER_ABI_VERSION_MINOR    0

/* ============================================================
 * Limits
 * ============================================================ */
#define DRIVER_MAX_NAME_LEN         32
#define DRIVER_MAX_CAPABILITIES     8
#define DRIVER_MAX_FAILURE_MODES    4
#define DRIVER_MAX_PINS             4
#define DRIVER_MAX_STRING_VAL       32

/* ============================================================
 * Enumerations
 * ============================================================ */

/** @brief Driver type classification */
typedef enum {
    DRIVER_TYPE_SENSOR      = 0,
    DRIVER_TYPE_ACTUATOR    = 1,
    DRIVER_TYPE_DISPLAY     = 2,
    DRIVER_TYPE_COMBO       = 3,    /* Both sensor and actuator (rare) */
} driver_type_t;

/** @brief Hardware bus type */
typedef enum {
    BUS_TYPE_I2C        = 0,
    BUS_TYPE_SPI        = 1,
    BUS_TYPE_UART       = 2,
    BUS_TYPE_ONEWIRE    = 3,
    BUS_TYPE_ADC        = 4,
    BUS_TYPE_GPIO       = 5,
    BUS_TYPE_PWM        = 6,
    BUS_TYPE_RMT        = 7,
} bus_type_t;

/** @brief Capability field identifiers (matches IR schema) */
typedef enum {
    CAP_TEMPERATURE         = 0,
    CAP_HUMIDITY            = 1,
    CAP_PRESSURE            = 2,
    CAP_ALTITUDE            = 3,
    CAP_ACCELERATION_X      = 4,
    CAP_ACCELERATION_Y      = 5,
    CAP_ACCELERATION_Z      = 6,
    CAP_GYROSCOPE_X         = 7,
    CAP_GYROSCOPE_Y         = 8,
    CAP_GYROSCOPE_Z         = 9,
    CAP_DISTANCE            = 10,
    CAP_LIGHT_LUX           = 11,
    CAP_LIGHT_UV            = 12,
    CAP_GAS_PPM             = 13,
    CAP_SMOKE_PPM           = 14,
    CAP_CO2_PPM             = 15,
    CAP_TVOC_PPB            = 16,
    CAP_VOLTAGE             = 17,
    CAP_CURRENT             = 18,
    CAP_POWER               = 19,
    CAP_HEART_RATE          = 20,
    CAP_SPO2                = 21,
    CAP_PROXIMITY           = 22,
    CAP_GESTURE             = 23,
    CAP_COLOR_R             = 24,
    CAP_COLOR_G             = 25,
    CAP_COLOR_B             = 26,
    CAP_WEIGHT              = 27,
    CAP_SOIL_MOISTURE       = 28,
    CAP_RAIN_LEVEL          = 29,
    CAP_TDS_PPM             = 30,
    CAP_PH_LEVEL            = 31,
    CAP_MOTION              = 32,
    CAP_DOOR_STATE          = 33,
    CAP_ON_OFF              = 34,
    CAP_ANGLE_DEGREES       = 35,
    CAP_SPEED_PERCENT       = 36,
    CAP_COLOR_RGB           = 37,
    CAP_TONE_HZ             = 38,
    CAP_DIRECTION           = 39,
    CAP_TEXT_DISPLAY         = 40,
    CAP_PIXEL_DISPLAY       = 41,
    CAP_FLOW_CONTROL        = 42,
    CAP_COUNT               = 43,   /* Sentinel — total number of capabilities */
} capability_t;

/** @brief Value type tag for sensor/actuator values */
typedef enum {
    VAL_TYPE_INT        = 0,
    VAL_TYPE_FLOAT      = 1,
    VAL_TYPE_BOOL       = 2,
    VAL_TYPE_STRING     = 3,
    VAL_TYPE_RGB        = 4,
} value_type_t;

/** @brief Driver error codes (supplementary to esp_err_t) */
typedef enum {
    DRV_OK                  = 0,
    DRV_ERR_NOT_INIT        = 1,
    DRV_ERR_BUS_FAIL        = 2,
    DRV_ERR_TIMEOUT         = 3,
    DRV_ERR_CRC             = 4,
    DRV_ERR_OUT_OF_RANGE    = 5,
    DRV_ERR_HW_FAULT        = 6,
    DRV_ERR_NOT_SUPPORTED   = 7,
    DRV_ERR_BUSY            = 8,
} driver_error_t;

/* ============================================================
 * Data Types
 * ============================================================ */

/** @brief RGB color value */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_value_t;

/** @brief Universal sensor value (tagged union) */
typedef struct {
    value_type_t type;
    union {
        int32_t     i;
        float       f;
        bool        b;
        char        s[DRIVER_MAX_STRING_VAL];
        rgb_value_t rgb;
    };
    capability_t    capability;     /* Which capability this value represents */
    uint32_t        timestamp_ms;   /* Reading timestamp (ms since boot) */
    driver_error_t  error;          /* DRV_OK if valid */
} sensor_value_t;

/** @brief Actuator command (tagged union) */
typedef struct {
    value_type_t type;
    union {
        int32_t     i;
        float       f;
        bool        b;
        char        s[DRIVER_MAX_STRING_VAL];
        rgb_value_t rgb;
    };
    capability_t    capability;     /* Which capability to actuate */
} actuator_cmd_t;

/** @brief Pin resource descriptor */
typedef struct {
    int         gpio_num;           /* GPIO number (-1 if not applicable) */
    uint8_t     adc_channel;        /* ADC channel (0xFF if not applicable) */
    uint8_t     pwm_channel;        /* LEDC channel (0xFF if not applicable) */
    bool        is_output;          /* true = output, false = input */
    bool        uses_interrupt;     /* true if this pin uses ISR */
} pin_resource_t;

/** @brief Driver configuration (passed to init) */
typedef struct {
    bus_type_t      bus_type;
    uint8_t         bus_index;          /* 0, 1, ... for multiple buses of same type */
    uint8_t         i2c_address;        /* I2C device address (7-bit) */
    uint8_t         spi_cs_index;       /* SPI chip select index */
    pin_resource_t  pins[DRIVER_MAX_PINS];
    uint8_t         pin_count;
    float           sample_rate_hz;     /* Desired sample rate */
    uint8_t         resolution_bits;    /* ADC/sensor resolution */
    bool            invert;             /* Invert output logic */
    uint16_t        min_angle;          /* Servo min angle */
    uint16_t        max_angle;          /* Servo max angle */
    uint8_t         led_count;          /* WS2812 LED count */
    uint8_t         display_cols;       /* Display columns */
    uint8_t         display_rows;       /* Display rows */
} driver_config_t;

/** @brief Opaque driver handle */
typedef struct {
    uint8_t         driver_index;       /* Index in driver registry */
    void           *internal;           /* Driver-private state (static allocation) */
} driver_handle_t;

/** @brief Failure mode descriptor */
typedef struct {
    driver_error_t  error;              /* Error code */
    const char     *description;        /* Human-readable description */
    sensor_value_t  safe_fallback;      /* Value to use when this error occurs */
} failure_mode_t;

/** @brief Driver metadata (compile-time constant) */
typedef struct {
    const char     *name;                               /* e.g., "drv_bme280" */
    const char     *display_name;                       /* e.g., "BME280 Environment Sensor" */
    const char     *version;                            /* Semantic version string */
    driver_type_t   type;                               /* Sensor, actuator, display, combo */
    bus_type_t      bus_type;                            /* Primary bus type */
    capability_t    capabilities[DRIVER_MAX_CAPABILITIES]; /* Supported capabilities */
    uint8_t         num_capabilities;
    uint32_t        max_latency_us;                     /* Max time for read()/write() call */
    uint32_t        min_interval_ms;                    /* Minimum time between calls */
    failure_mode_t  failure_modes[DRIVER_MAX_FAILURE_MODES];
    uint8_t         num_failure_modes;
    uint16_t        internal_state_size;                /* Bytes needed for internal state */
} driver_meta_t;

/* ============================================================
 * Driver Virtual Table (vtable) — THE ABI CONTRACT
 * ============================================================ */

/**
 * @brief Driver vtable — every driver must provide one static instance.
 *
 * Lifecycle:
 *   1. init()   — called once at boot, configures hardware
 *   2. read()   — called by VM for sensor drivers
 *   3. write()  — called by VM for actuator drivers
 *   4. deinit() — called on shutdown or driver unload
 *
 * All functions must:
 *   - Return within max_latency_us
 *   - Never call malloc/free/pvPortMalloc
 *   - Never block on I/O (use pre-staged DMA or polling with timeout)
 *   - Log errors via ESP_LOGE, never via printf/stdout
 */
typedef struct {
    /**
     * @brief Initialize the driver hardware.
     * @param cfg   Configuration from board descriptor
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*init)(const driver_config_t *cfg);

    /**
     * @brief Read a sensor value.
     * @param h     Driver handle
     * @param field Which capability field to read
     * @param out   Output value (caller-allocated)
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*read)(driver_handle_t h, capability_t field, sensor_value_t *out);

    /**
     * @brief Write an actuator command.
     * @param h     Driver handle
     * @param cmd   Command to execute
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t (*write)(driver_handle_t h, const actuator_cmd_t *cmd);

    /**
     * @brief De-initialize the driver, release hardware resources.
     * @param h     Driver handle
     * @return ESP_OK on success
     */
    esp_err_t (*deinit)(driver_handle_t h);

    /** @brief Pointer to compile-time metadata */
    const driver_meta_t *meta;
} driver_vtable_t;

/* ============================================================
 * Driver Registration Macro
 * ============================================================ */

/**
 * @brief Macro to declare a driver's vtable as a compile-time constant.
 *
 * Usage in driver source file:
 *   PARAKRAM_REGISTER_DRIVER(drv_bme280, bme280_vtable);
 *
 * This places the vtable pointer in a dedicated linker section
 * so the registry can enumerate all drivers at link time.
 */
#define PARAKRAM_REGISTER_DRIVER(name, vtable_var) \
    __attribute__((used, section(".parakram_drivers"))) \
    static const driver_vtable_t * const _drv_##name##_ptr = &(vtable_var)

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_ABI_H */
```

---

## Part 2: Driver Types Header (`driver_types.h`)

```c
/**
 * @file driver_types.h
 * @brief Supplementary type definitions for the Parakram driver layer.
 */

#ifndef DRIVER_TYPES_H
#define DRIVER_TYPES_H

#include "driver_abi.h"
#include "system_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Static internal state for I2C-based sensors */
typedef struct {
    uint8_t     bus_index;
    uint8_t     i2c_addr;
    bool        initialized;
    uint32_t    last_read_tick;
    float       calibration_offset;
    float       last_values[DRIVER_MAX_CAPABILITIES];
    uint8_t     consecutive_errors;
} i2c_sensor_state_t;

/** @brief Static internal state for GPIO-based actuators */
typedef struct {
    int         gpio_num;
    bool        initialized;
    bool        current_state;
    bool        inverted;
    uint32_t    last_write_tick;
    uint32_t    toggle_count;
} gpio_actuator_state_t;

/** @brief Static internal state for PWM-based actuators */
typedef struct {
    uint8_t     ledc_channel;
    int         gpio_num;
    bool        initialized;
    float       current_duty;
    uint32_t    frequency_hz;
    uint32_t    last_write_tick;
} pwm_actuator_state_t;

/** @brief Static internal state for ADC-based sensors */
typedef struct {
    uint8_t     adc_channel;
    uint8_t     adc_unit;       /* ADC_UNIT_1 or ADC_UNIT_2 */
    bool        initialized;
    float       calibration_slope;
    float       calibration_offset;
    int         raw_last;
    float       voltage_last;
    float       value_last;
} adc_sensor_state_t;

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_TYPES_H */
```

---

## Part 3: Complete BME280 Driver (`drv_bme280.c`)

```c
/**
 * @file drv_bme280.c
 * @brief Parakram driver for Bosch BME280 environment sensor.
 *
 * Capabilities: temperature, humidity, pressure, altitude
 * Bus: I2C (address 0x76 or 0x77)
 * Datasheet: BST-BME280-DS002
 *
 * This driver conforms exactly to the Parakram Driver ABI v1.0.
 * - No dynamic memory allocation
 * - All state in static struct
 * - read() returns within 2ms (I2C burst read)
 * - Supports oversampling and IIR filter configuration
 */

#include "drv_bme280.h"
#include "driver_abi.h"
#include "driver_types.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "drv_bme280";

/* ============================================================
 * BME280 Register Map
 * ============================================================ */
#define BME280_REG_CHIP_ID          0xD0
#define BME280_REG_RESET            0xE0
#define BME280_REG_CTRL_HUM         0xF2
#define BME280_REG_STATUS           0xF3
#define BME280_REG_CTRL_MEAS        0xF4
#define BME280_REG_CONFIG           0xF5
#define BME280_REG_PRESS_MSB        0xF7
#define BME280_REG_PRESS_LSB        0xF8
#define BME280_REG_PRESS_XLSB       0xF9
#define BME280_REG_TEMP_MSB         0xFA
#define BME280_REG_TEMP_LSB         0xFB
#define BME280_REG_TEMP_XLSB        0xFC
#define BME280_REG_HUM_MSB          0xFD
#define BME280_REG_HUM_LSB          0xFE

/* Calibration data registers */
#define BME280_REG_CALIB_00         0x88    /* dig_T1 .. dig_H1 (0x88-0xA1) */
#define BME280_REG_CALIB_26         0xE1    /* dig_H2 .. dig_H6 (0xE1-0xE7) */

#define BME280_CHIP_ID_VALUE        0x60
#define BME280_RESET_VALUE          0xB6

/* Oversampling settings */
#define BME280_OSRS_SKIP            0x00
#define BME280_OSRS_X1              0x01
#define BME280_OSRS_X2              0x02
#define BME280_OSRS_X4              0x03
#define BME280_OSRS_X8              0x04
#define BME280_OSRS_X16             0x05

/* Mode settings */
#define BME280_MODE_SLEEP           0x00
#define BME280_MODE_FORCED          0x01
#define BME280_MODE_NORMAL          0x03

/* IIR filter */
#define BME280_FILTER_OFF           0x00
#define BME280_FILTER_2             0x01
#define BME280_FILTER_4             0x02
#define BME280_FILTER_8             0x03
#define BME280_FILTER_16            0x04

/* Standby time */
#define BME280_STANDBY_0_5MS        0x00
#define BME280_STANDBY_62_5MS       0x01
#define BME280_STANDBY_125MS        0x02
#define BME280_STANDBY_250MS        0x03
#define BME280_STANDBY_500MS        0x04
#define BME280_STANDBY_1000MS       0x05

/* Sea-level pressure for altitude calculation (Pa) */
#define BME280_SEA_LEVEL_PRESSURE   101325.0f

/* ============================================================
 * Calibration Data Structure
 * ============================================================ */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
} bme280_calib_t;

/* ============================================================
 * Static Driver State (NO HEAP ALLOCATION)
 * ============================================================ */
typedef struct {
    bool            initialized;
    uint8_t         bus_index;
    uint8_t         i2c_addr;
    bme280_calib_t  calib;
    int32_t         t_fine;             /* Temperature fine resolution (shared) */
    float           last_temperature;
    float           last_humidity;
    float           last_pressure;
    float           last_altitude;
    uint32_t        last_read_tick;
    uint8_t         consecutive_errors;
} bme280_state_t;

/* Single static instance — one BME280 per system */
static bme280_state_t s_state = {
    .initialized = false,
    .consecutive_errors = 0,
};

/* ============================================================
 * I2C Helpers (bounded-time, non-blocking)
 * ============================================================ */

static esp_err_t bme280_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_bus_read(s_state.bus_index, s_state.i2c_addr, reg, data, len, 50);
}

static esp_err_t bme280_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_bus_write_byte(s_state.bus_index, s_state.i2c_addr, reg, value, 50);
}

/* ============================================================
 * Calibration Data Load
 * ============================================================ */

static esp_err_t bme280_load_calibration(void)
{
    uint8_t buf[26];
    esp_err_t ret;

    /* Read temperature and pressure calibration (0x88-0xA1, 26 bytes) */
    ret = bme280_read_reg(BME280_REG_CALIB_00, buf, 26);
    if (ret != ESP_OK) return ret;

    s_state.calib.dig_T1 = (uint16_t)(buf[1] << 8) | buf[0];
    s_state.calib.dig_T2 = (int16_t)((buf[3] << 8) | buf[2]);
    s_state.calib.dig_T3 = (int16_t)((buf[5] << 8) | buf[4]);
    s_state.calib.dig_P1 = (uint16_t)(buf[7] << 8) | buf[6];
    s_state.calib.dig_P2 = (int16_t)((buf[9] << 8) | buf[8]);
    s_state.calib.dig_P3 = (int16_t)((buf[11] << 8) | buf[10]);
    s_state.calib.dig_P4 = (int16_t)((buf[13] << 8) | buf[12]);
    s_state.calib.dig_P5 = (int16_t)((buf[15] << 8) | buf[14]);
    s_state.calib.dig_P6 = (int16_t)((buf[17] << 8) | buf[16]);
    s_state.calib.dig_P7 = (int16_t)((buf[19] << 8) | buf[18]);
    s_state.calib.dig_P8 = (int16_t)((buf[21] << 8) | buf[20]);
    s_state.calib.dig_P9 = (int16_t)((buf[23] << 8) | buf[22]);

    /* dig_H1 is at 0xA1 (buf[25]) */
    s_state.calib.dig_H1 = buf[25];

    /* Read humidity calibration (0xE1-0xE7, 7 bytes) */
    uint8_t hbuf[7];
    ret = bme280_read_reg(BME280_REG_CALIB_26, hbuf, 7);
    if (ret != ESP_OK) return ret;

    s_state.calib.dig_H2 = (int16_t)((hbuf[1] << 8) | hbuf[0]);
    s_state.calib.dig_H3 = hbuf[2];
    s_state.calib.dig_H4 = (int16_t)((hbuf[3] << 4) | (hbuf[4] & 0x0F));
    s_state.calib.dig_H5 = (int16_t)(((hbuf[4] >> 4) & 0x0F) | (hbuf[5] << 4));
    s_state.calib.dig_H6 = (int8_t)hbuf[6];

    return ESP_OK;
}

/* ============================================================
 * Compensation Formulas (from Bosch datasheet, integer version)
 * ============================================================ */

static float bme280_compensate_temperature(int32_t adc_T)
{
    int32_t var1, var2;
    bme280_calib_t *c = &s_state.calib;

    var1 = ((((adc_T >> 3) - ((int32_t)c->dig_T1 << 1))) * ((int32_t)c->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)c->dig_T1)) * ((adc_T >> 4) - ((int32_t)c->dig_T1))) >> 12) * ((int32_t)c->dig_T3)) >> 14;

    s_state.t_fine = var1 + var2;
    int32_t T = (s_state.t_fine * 5 + 128) >> 8;
    return (float)T / 100.0f;
}

static float bme280_compensate_pressure(int32_t adc_P)
{
    int64_t var1, var2, p;
    bme280_calib_t *c = &s_state.calib;

    var1 = ((int64_t)s_state.t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)c->dig_P6;
    var2 = var2 + ((var1 * (int64_t)c->dig_P5) << 17);
    var2 = var2 + (((int64_t)c->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)c->dig_P3) >> 8) + ((var1 * (int64_t)c->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)c->dig_P1) >> 33;

    if (var1 == 0) return 0.0f;  /* Avoid division by zero */

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)c->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)c->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)c->dig_P7) << 4);

    return (float)((uint32_t)p) / 256.0f;  /* Result in Pa */
}

static float bme280_compensate_humidity(int32_t adc_H)
{
    int32_t v_x1_u32r;
    bme280_calib_t *c = &s_state.calib;

    v_x1_u32r = (s_state.t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)c->dig_H4) << 20) - (((int32_t)c->dig_H5) * v_x1_u32r)) +
                   ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)c->dig_H6)) >> 10) *
                      (((v_x1_u32r * ((int32_t)c->dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
                    ((int32_t)2097152)) * ((int32_t)c->dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)c->dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);

    return (float)((uint32_t)(v_x1_u32r >> 12)) / 1024.0f;  /* Result in %RH */
}

static float bme280_calculate_altitude(float pressure_pa)
{
    /* Hypsometric formula */
    return 44330.0f * (1.0f - powf(pressure_pa / BME280_SEA_LEVEL_PRESSURE, 0.1903f));
}

/* ============================================================
 * ABI Implementation
 * ============================================================ */

static esp_err_t bme280_init(const driver_config_t *cfg)
{
    if (s_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    s_state.bus_index = cfg->bus_index;
    s_state.i2c_addr = cfg->i2c_address;

    /* Verify chip ID */
    uint8_t chip_id = 0;
    esp_err_t ret = bme280_read_reg(BME280_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
        return ret;
    }
    if (chip_id != BME280_CHIP_ID_VALUE) {
        ESP_LOGE(TAG, "Invalid chip ID: 0x%02X (expected 0x%02X)", chip_id, BME280_CHIP_ID_VALUE);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Soft reset */
    ret = bme280_write_reg(BME280_REG_RESET, BME280_RESET_VALUE);
    if (ret != ESP_OK) return ret;

    /* Wait for reset completion (max 2ms per datasheet) */
    vTaskDelay(pdMS_TO_TICKS(5));

    /* Wait for NVM copy to complete */
    uint8_t status = 0;
    int retries = 10;
    do {
        ret = bme280_read_reg(BME280_REG_STATUS, &status, 1);
        if (ret != ESP_OK) return ret;
        if (!(status & 0x01)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (--retries > 0);

    /* Load calibration data */
    ret = bme280_load_calibration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load calibration: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure: humidity oversampling x1 (must be set before ctrl_meas) */
    ret = bme280_write_reg(BME280_REG_CTRL_HUM, BME280_OSRS_X1);
    if (ret != ESP_OK) return ret;

    /* Configure: standby 500ms, IIR filter x4 */
    ret = bme280_write_reg(BME280_REG_CONFIG,
        (BME280_STANDBY_500MS << 5) | (BME280_FILTER_4 << 2));
    if (ret != ESP_OK) return ret;

    /* Configure: temp x2, pressure x2, normal mode */
    ret = bme280_write_reg(BME280_REG_CTRL_MEAS,
        (BME280_OSRS_X2 << 5) | (BME280_OSRS_X2 << 2) | BME280_MODE_NORMAL);
    if (ret != ESP_OK) return ret;

    s_state.initialized = true;
    s_state.consecutive_errors = 0;
    ESP_LOGI(TAG, "BME280 initialized at I2C%d:0x%02X", s_state.bus_index, s_state.i2c_addr);

    return ESP_OK;
}

static esp_err_t bme280_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    if (!s_state.initialized) {
        out->error = DRV_ERR_NOT_INIT;
        return ESP_ERR_INVALID_STATE;
    }

    /* Burst read: press(3) + temp(3) + hum(2) = 8 bytes starting at 0xF7 */
    uint8_t raw[8];
    esp_err_t ret = bme280_read_reg(BME280_REG_PRESS_MSB, raw, 8);
    if (ret != ESP_OK) {
        s_state.consecutive_errors++;
        ESP_LOGE(TAG, "I2C read failed (%d consecutive)", s_state.consecutive_errors);

        /* Fallback to last known good values */
        out->error = DRV_ERR_BUS_FAIL;
        out->type = VAL_TYPE_FLOAT;
        out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

        switch (field) {
            case CAP_TEMPERATURE: out->f = s_state.last_temperature; break;
            case CAP_HUMIDITY:    out->f = s_state.last_humidity; break;
            case CAP_PRESSURE:    out->f = s_state.last_pressure; break;
            case CAP_ALTITUDE:    out->f = s_state.last_altitude; break;
            default:              out->error = DRV_ERR_NOT_SUPPORTED; return ESP_ERR_NOT_SUPPORTED;
        }
        out->capability = field;
        return ESP_OK;  /* Return OK with error flag — caller checks out->error */
    }

    s_state.consecutive_errors = 0;

    /* Parse raw ADC values (20-bit for T/P, 16-bit for H) */
    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | ((int32_t)raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | ((int32_t)raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8) | (int32_t)raw[7];

    /* Compensate (temperature must be first — sets t_fine) */
    s_state.last_temperature = bme280_compensate_temperature(adc_T);
    s_state.last_pressure = bme280_compensate_pressure(adc_P);
    s_state.last_humidity = bme280_compensate_humidity(adc_H);
    s_state.last_altitude = bme280_calculate_altitude(s_state.last_pressure);
    s_state.last_read_tick = (uint32_t)(esp_timer_get_time() / 1000);

    /* Fill output */
    out->type = VAL_TYPE_FLOAT;
    out->error = DRV_OK;
    out->timestamp_ms = s_state.last_read_tick;
    out->capability = field;

    switch (field) {
        case CAP_TEMPERATURE:
            out->f = s_state.last_temperature;
            break;
        case CAP_HUMIDITY:
            out->f = s_state.last_humidity;
            break;
        case CAP_PRESSURE:
            out->f = s_state.last_pressure;
            break;
        case CAP_ALTITUDE:
            out->f = s_state.last_altitude;
            break;
        default:
            out->error = DRV_ERR_NOT_SUPPORTED;
            return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

static esp_err_t bme280_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    /* BME280 is a sensor-only device — write is not supported */
    ESP_LOGW(TAG, "BME280 does not support write operations");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t bme280_deinit(driver_handle_t h)
{
    if (!s_state.initialized) return ESP_OK;

    /* Put sensor into sleep mode */
    esp_err_t ret = bme280_write_reg(BME280_REG_CTRL_MEAS, BME280_MODE_SLEEP);
    s_state.initialized = false;
    ESP_LOGI(TAG, "BME280 de-initialized");
    return ret;
}

/* ============================================================
 * Metadata + Vtable (compile-time constants)
 * ============================================================ */

static const driver_meta_t bme280_meta = {
    .name           = "drv_bme280",
    .display_name   = "BME280 Environment Sensor",
    .version        = "1.0.0",
    .type           = DRIVER_TYPE_SENSOR,
    .bus_type       = BUS_TYPE_I2C,
    .capabilities   = { CAP_TEMPERATURE, CAP_HUMIDITY, CAP_PRESSURE, CAP_ALTITUDE },
    .num_capabilities = 4,
    .max_latency_us = 2000,     /* 2ms — I2C burst read at 400kHz */
    .min_interval_ms = 500,     /* Reflect BME280 standby time */
    .failure_modes  = {
        {
            .error = DRV_ERR_BUS_FAIL,
            .description = "I2C bus communication failure",
            .safe_fallback = { .type = VAL_TYPE_FLOAT, .f = 0.0f, .error = DRV_ERR_BUS_FAIL },
        },
        {
            .error = DRV_ERR_CRC,
            .description = "Sensor data CRC mismatch",
            .safe_fallback = { .type = VAL_TYPE_FLOAT, .f = 0.0f, .error = DRV_ERR_CRC },
        },
    },
    .num_failure_modes = 2,
    .internal_state_size = sizeof(bme280_state_t),
};

static const driver_vtable_t bme280_vtable = {
    .init   = bme280_init,
    .read   = bme280_read,
    .write  = bme280_write,
    .deinit = bme280_deinit,
    .meta   = &bme280_meta,
};

PARAKRAM_REGISTER_DRIVER(drv_bme280, bme280_vtable);
```

---

## Part 4: Complete Relay Driver (`drv_relay.c`)

```c
/**
 * @file drv_relay.c
 * @brief Parakram driver for GPIO relay actuator.
 *
 * Capabilities: on_off
 * Bus: GPIO (digital output)
 * Supports: active-high and active-low (inverted) relays
 *
 * This driver conforms exactly to the Parakram Driver ABI v1.0.
 * - No dynamic memory allocation
 * - All state in static struct
 * - write() returns within 10μs (GPIO set level)
 * - read() returns current state
 */

#include "drv_relay.h"
#include "driver_abi.h"
#include "gpio_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rate_limiter.h"

static const char *TAG = "drv_relay";

/* ============================================================
 * Static Driver State (NO HEAP ALLOCATION)
 * ============================================================ */
typedef struct {
    bool        initialized;
    int         gpio_num;
    bool        inverted;       /* If true, HIGH=off, LOW=on */
    bool        current_state;  /* Logical state (true=on, false=off) */
    uint32_t    last_write_tick;
    uint32_t    on_count;       /* Total number of ON transitions */
    uint32_t    off_count;      /* Total number of OFF transitions */
    uint32_t    total_on_ms;    /* Cumulative ON time */
    uint32_t    last_on_tick;   /* Tick when relay turned ON */
} relay_state_t;

static relay_state_t s_state = {
    .initialized = false,
    .current_state = false,
    .on_count = 0,
    .off_count = 0,
    .total_on_ms = 0,
};

/* ============================================================
 * Internal Helpers
 * ============================================================ */

/**
 * @brief Set the physical GPIO state based on logical state + inversion.
 */
static esp_err_t relay_set_physical(bool logical_on)
{
    bool physical_level = s_state.inverted ? !logical_on : logical_on;
    return gpio_hal_set_level(s_state.gpio_num, physical_level ? 1 : 0);
}

/* ============================================================
 * ABI Implementation
 * ============================================================ */

static esp_err_t relay_init(const driver_config_t *cfg)
{
    if (s_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (cfg->pin_count < 1 || cfg->pins[0].gpio_num < 0) {
        ESP_LOGE(TAG, "No GPIO pin configured for relay");
        return ESP_ERR_INVALID_ARG;
    }

    s_state.gpio_num = cfg->pins[0].gpio_num;
    s_state.inverted = cfg->invert;

    /* Configure GPIO as output */
    esp_err_t ret = gpio_hal_configure_output(s_state.gpio_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", s_state.gpio_num, esp_err_to_name(ret));
        return ret;
    }

    /* Start in OFF state (safe default) */
    ret = relay_set_physical(false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial state: %s", esp_err_to_name(ret));
        return ret;
    }

    s_state.current_state = false;
    s_state.on_count = 0;
    s_state.off_count = 0;
    s_state.total_on_ms = 0;
    s_state.last_write_tick = (uint32_t)(esp_timer_get_time() / 1000);
    s_state.initialized = true;

    ESP_LOGI(TAG, "Relay initialized on GPIO %d (inverted=%d)", s_state.gpio_num, s_state.inverted);
    return ESP_OK;
}

static esp_err_t relay_read(driver_handle_t h, capability_t field, sensor_value_t *out)
{
    if (!s_state.initialized) {
        out->error = DRV_ERR_NOT_INIT;
        return ESP_ERR_INVALID_STATE;
    }

    if (field != CAP_ON_OFF) {
        out->error = DRV_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->type = VAL_TYPE_BOOL;
    out->b = s_state.current_state;
    out->capability = CAP_ON_OFF;
    out->error = DRV_OK;
    out->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    return ESP_OK;
}

static esp_err_t relay_write(driver_handle_t h, const actuator_cmd_t *cmd)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (cmd->capability != CAP_ON_OFF) {
        ESP_LOGW(TAG, "Unsupported capability: %d", cmd->capability);
        return ESP_ERR_NOT_SUPPORTED;
    }

    bool desired_state;

    /* Accept bool or int (0/1) */
    switch (cmd->type) {
        case VAL_TYPE_BOOL:
            desired_state = cmd->b;
            break;
        case VAL_TYPE_INT:
            desired_state = (cmd->i != 0);
            break;
        case VAL_TYPE_FLOAT:
            desired_state = (cmd->f > 0.5f);
            break;
        default:
            ESP_LOGW(TAG, "Unsupported value type for relay: %d", cmd->type);
            return ESP_ERR_INVALID_ARG;
    }

    /* Only actuate if state is changing (reduce relay wear) */
    if (desired_state == s_state.current_state) {
        return ESP_OK;  /* No change needed */
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    /* Set physical pin */
    esp_err_t ret = relay_set_physical(desired_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO %d: %s", s_state.gpio_num, esp_err_to_name(ret));
        return ret;
    }

    /* Update tracking */
    if (desired_state) {
        /* Turning ON */
        s_state.on_count++;
        s_state.last_on_tick = now;
    } else {
        /* Turning OFF */
        s_state.off_count++;
        if (s_state.last_on_tick > 0) {
            s_state.total_on_ms += (now - s_state.last_on_tick);
        }
    }

    s_state.current_state = desired_state;
    s_state.last_write_tick = now;

    ESP_LOGD(TAG, "Relay %s (GPIO %d, on_count=%lu)",
             desired_state ? "ON" : "OFF", s_state.gpio_num, (unsigned long)s_state.on_count);

    return ESP_OK;
}

static esp_err_t relay_deinit(driver_handle_t h)
{
    if (!s_state.initialized) return ESP_OK;

    /* Turn off relay (safe state) before de-initializing */
    relay_set_physical(false);
    s_state.current_state = false;

    /* Reset GPIO to input (high-impedance) */
    gpio_hal_configure_input(s_state.gpio_num);

    s_state.initialized = false;
    ESP_LOGI(TAG, "Relay de-initialized (GPIO %d, total_on_time=%lums, cycles=%lu)",
             s_state.gpio_num,
             (unsigned long)s_state.total_on_ms,
             (unsigned long)s_state.on_count);

    return ESP_OK;
}

/* ============================================================
 * Metadata + Vtable (compile-time constants)
 * ============================================================ */

static const driver_meta_t relay_meta = {
    .name           = "drv_relay",
    .display_name   = "Relay Switch",
    .version        = "1.0.0",
    .type           = DRIVER_TYPE_ACTUATOR,
    .bus_type       = BUS_TYPE_GPIO,
    .capabilities   = { CAP_ON_OFF },
    .num_capabilities = 1,
    .max_latency_us = 10,       /* 10μs — GPIO level set */
    .min_interval_ms = 100,     /* Debounce: don't toggle faster than 10Hz */
    .failure_modes  = {
        {
            .error = DRV_ERR_HW_FAULT,
            .description = "GPIO pin fault (stuck high/low)",
            .safe_fallback = { .type = VAL_TYPE_BOOL, .b = false, .error = DRV_ERR_HW_FAULT },
        },
    },
    .num_failure_modes = 1,
    .internal_state_size = sizeof(relay_state_t),
};

static const driver_vtable_t relay_vtable = {
    .init   = relay_init,
    .read   = relay_read,
    .write  = relay_write,
    .deinit = relay_deinit,
    .meta   = &relay_meta,
};

PARAKRAM_REGISTER_DRIVER(drv_relay, relay_vtable);
```
