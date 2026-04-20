/**
 * @file driver_abi.h
 * @brief Parakram Driver ABI — the contract all drivers must satisfy.
 */

#ifndef DRIVER_ABI_H
#define DRIVER_ABI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DRIVER_ABI_VERSION_MAJOR    1
#define DRIVER_ABI_VERSION_MINOR    0
#define DRIVER_MAX_NAME_LEN         32
#define DRIVER_MAX_CAPABILITIES     8
#define DRIVER_MAX_FAILURE_MODES    4
#define DRIVER_MAX_PINS             4
#define DRIVER_MAX_STRING_VAL       32

typedef enum {
    DRIVER_TYPE_SENSOR   = 0,
    DRIVER_TYPE_ACTUATOR = 1,
    DRIVER_TYPE_DISPLAY  = 2,
    DRIVER_TYPE_COMBO    = 3,
} driver_type_t;

typedef enum {
    BUS_TYPE_I2C     = 0, BUS_TYPE_SPI     = 1, BUS_TYPE_UART    = 2,
    BUS_TYPE_ONEWIRE = 3, BUS_TYPE_ADC     = 4, BUS_TYPE_GPIO    = 5,
    BUS_TYPE_PWM     = 6, BUS_TYPE_RMT     = 7,
} bus_type_t;

typedef enum {
    CAP_TEMPERATURE = 0, CAP_HUMIDITY = 1, CAP_PRESSURE = 2, CAP_ALTITUDE = 3,
    CAP_ACCELERATION_X = 4, CAP_ACCELERATION_Y = 5, CAP_ACCELERATION_Z = 6,
    CAP_GYROSCOPE_X = 7, CAP_GYROSCOPE_Y = 8, CAP_GYROSCOPE_Z = 9,
    CAP_DISTANCE = 10, CAP_LIGHT_LUX = 11, CAP_GAS_PPM = 13, CAP_SMOKE_PPM = 14,
    CAP_CO2_PPM = 15, CAP_TVOC_PPB = 16, CAP_VOLTAGE = 17, CAP_CURRENT = 18,
    CAP_POWER = 19, CAP_HEART_RATE = 20, CAP_SPO2 = 21, CAP_PROXIMITY = 22,
    CAP_MOTION = 32, CAP_DOOR_STATE = 33, CAP_ON_OFF = 34,
    CAP_ANGLE_DEGREES = 35, CAP_SPEED_PERCENT = 36, CAP_COLOR_RGB = 37,
    CAP_TONE_HZ = 38, CAP_TEXT_DISPLAY = 40, CAP_SOIL_MOISTURE = 28,
    CAP_RAIN_LEVEL = 29, CAP_TDS_PPM = 30, CAP_PH_LEVEL = 31,
    CAP_FLOW_CONTROL = 42, CAP_COUNT = 43,
} capability_t;

typedef enum {
    VAL_TYPE_INT = 0, VAL_TYPE_FLOAT = 1, VAL_TYPE_BOOL = 2, VAL_TYPE_STRING = 3,
} value_type_t;

typedef enum {
    DRV_OK = 0, DRV_ERR_NOT_INIT = 1, DRV_ERR_BUS_FAIL = 2,
    DRV_ERR_TIMEOUT = 3, DRV_ERR_CRC = 4, DRV_ERR_OUT_OF_RANGE = 5,
    DRV_ERR_HW_FAULT = 6, DRV_ERR_NOT_SUPPORTED = 7, DRV_ERR_BUSY = 8,
} driver_error_t;

typedef struct { uint8_t r, g, b; } rgb_value_t;

typedef struct {
    value_type_t    type;
    union { int32_t i; float f; bool b; char s[DRIVER_MAX_STRING_VAL]; rgb_value_t rgb; };
    capability_t    capability;
    uint32_t        timestamp_ms;
    driver_error_t  error;
} sensor_value_t;

typedef struct {
    value_type_t    type;
    union { int32_t i; float f; bool b; char s[DRIVER_MAX_STRING_VAL]; rgb_value_t rgb; };
    capability_t    capability;
} actuator_cmd_t;

typedef struct {
    int gpio_num; uint8_t adc_channel; uint8_t pwm_channel;
    bool is_output; bool uses_interrupt;
} pin_resource_t;

typedef struct {
    bus_type_t      bus_type;
    uint8_t         bus_index;
    uint8_t         i2c_address;
    uint8_t         spi_cs_index;
    pin_resource_t  pins[DRIVER_MAX_PINS];
    uint8_t         pin_count;
    float           sample_rate_hz;
    uint8_t         resolution_bits;
    bool            invert;
    uint16_t        min_angle;
    uint16_t        max_angle;
    uint8_t         led_count;
    uint8_t         display_cols;
    uint8_t         display_rows;
} driver_config_t;

typedef struct {
    uint8_t driver_index;
    void   *internal;
} driver_handle_t;

typedef struct {
    driver_error_t  error;
    const char     *description;
    sensor_value_t  safe_fallback;
} failure_mode_t;

typedef struct {
    const char     *name;
    const char     *display_name;
    const char     *version;
    driver_type_t   type;
    bus_type_t      bus_type;
    capability_t    capabilities[DRIVER_MAX_CAPABILITIES];
    uint8_t         num_capabilities;
    uint32_t        max_latency_us;
    uint32_t        min_interval_ms;
    failure_mode_t  failure_modes[DRIVER_MAX_FAILURE_MODES];
    uint8_t         num_failure_modes;
    uint16_t        internal_state_size;
} driver_meta_t;

typedef struct {
    esp_err_t (*init)(const driver_config_t *cfg);
    esp_err_t (*read)(driver_handle_t h, capability_t field, sensor_value_t *out);
    esp_err_t (*write)(driver_handle_t h, const actuator_cmd_t *cmd);
    esp_err_t (*deinit)(driver_handle_t h);
    const driver_meta_t *meta;
} driver_vtable_t;

#define PARAKRAM_REGISTER_DRIVER(name, vtable_var) \
    __attribute__((used, section(".parakram_drivers"))) \
    static const driver_vtable_t * const _drv_##name##_ptr = &(vtable_var)

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_ABI_H */
