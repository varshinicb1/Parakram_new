/**
 * @file scheduler.h
 * @brief Pipeline scheduler — manages trigger evaluation and pipeline dispatch.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "vm.h"
#include "driver_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRIGGER_TIMER = 0, TRIGGER_SENSOR_THRESHOLD = 1, TRIGGER_GPIO_EDGE = 2,
    TRIGGER_MQTT = 3, TRIGGER_BLE_EVENT = 4, TRIGGER_STARTUP = 5,
    TRIGGER_TIME_WINDOW = 6,
} trigger_type_t;

typedef struct {
    uint8_t         id;
    trigger_type_t  type;
    uint32_t        interval_ms;
    uint32_t        last_fire_tick;
    uint8_t         device_index;
    uint8_t         field_index;
    float           threshold;
    float           hysteresis;
    uint8_t         comparison;  /* 0=gt, 1=lt, 2=gte, 3=lte, 4=eq */
    bool            was_active;  /* For hysteresis tracking */
    bool            armed;
} trigger_config_t;

typedef struct {
    uint8_t             id;
    uint8_t             trigger_id;
    uint8_t             priority;
    bool                enabled;
    uint16_t            instruction_offset;
    uint16_t            instruction_count;
    uint16_t            max_execution_ms;
    vm_context_t        ctx;
} pipeline_config_t;

typedef struct {
    trigger_config_t    triggers[SYS_MAX_PIPELINES];
    pipeline_config_t   pipelines[SYS_MAX_PIPELINES];
    uint8_t             num_triggers;
    uint8_t             num_pipelines;
    uint32_t            tick_count;
    bool                running;
} scheduler_state_t;

esp_err_t scheduler_init(void);
esp_err_t scheduler_start(void);
esp_err_t scheduler_stop(void);
void      scheduler_tick(void);
const scheduler_state_t *scheduler_get_state(void);
esp_err_t scheduler_add_trigger(const trigger_config_t *trigger);
esp_err_t scheduler_add_pipeline(const pipeline_config_t *pipeline);

#ifdef __cplusplus
}
#endif
#endif /* SCHEDULER_H */
