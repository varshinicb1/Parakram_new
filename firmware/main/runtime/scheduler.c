/**
 * @file scheduler.c
 * @brief Pipeline scheduler — trigger evaluation and dispatch.
 */

#include "scheduler.h"
#include "state_store.h"
#include "driver_registry.h"
#include "event_bus.h"
#include "safety.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SCHED";

static scheduler_state_t s_state = {0};
static TaskHandle_t s_task_handle = NULL;

static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool evaluate_trigger(trigger_config_t *trigger) {
    switch (trigger->type) {
    case TRIGGER_TIMER: {
        uint32_t elapsed = now_ms() - trigger->last_fire_tick;
        if (elapsed >= trigger->interval_ms) {
            trigger->last_fire_tick = now_ms();
            return true;
        }
        return false;
    }

    case TRIGGER_SENSOR_THRESHOLD: {
        registered_driver_t *drv = driver_registry_get(trigger->device_index);
        if (!drv || !drv->initialized) return false;
        sensor_value_t sv;
        if (drv->vtable->read(drv->handle, (capability_t)trigger->field_index, &sv) != ESP_OK)
            return false;
        float val = (sv.type == VAL_TYPE_FLOAT) ? sv.f : (float)sv.i;
        bool condition = false;
        switch (trigger->comparison) {
            case 0: condition = val > trigger->threshold; break;
            case 1: condition = val < trigger->threshold; break;
            case 2: condition = val >= trigger->threshold; break;
            case 3: condition = val <= trigger->threshold; break;
            case 4: condition = (val > trigger->threshold - trigger->hysteresis &&
                                 val < trigger->threshold + trigger->hysteresis); break;
        }
        /* Hysteresis: only fire on transition */
        if (condition && !trigger->was_active) {
            trigger->was_active = true;
            return true;
        }
        if (!condition && trigger->was_active) {
            float hyst_clear = false;
            switch (trigger->comparison) {
                case 0: hyst_clear = val < (trigger->threshold - trigger->hysteresis); break;
                case 1: hyst_clear = val > (trigger->threshold + trigger->hysteresis); break;
                default: hyst_clear = true; break;
            }
            if (hyst_clear) trigger->was_active = false;
        }
        return false;
    }

    case TRIGGER_STARTUP:
        if (trigger->armed) {
            trigger->armed = false;
            return true;
        }
        return false;

    default:
        return false;
    }
}

static void scheduler_task(void *arg) {
    ESP_LOGI(TAG, "Scheduler task started (%d pipelines, %d triggers)",
             s_state.num_pipelines, s_state.num_triggers);

    /* Arm startup triggers */
    for (int i = 0; i < s_state.num_triggers; i++) {
        if (s_state.triggers[i].type == TRIGGER_STARTUP)
            s_state.triggers[i].armed = true;
    }

    while (s_state.running) {
        s_state.tick_count++;
        watchdog_feed();

        /* Evaluate triggers and dispatch pipelines */
        for (int i = 0; i < s_state.num_pipelines; i++) {
            pipeline_config_t *pl = &s_state.pipelines[i];
            if (!pl->enabled) continue;

            /* Find matching trigger */
            trigger_config_t *trig = NULL;
            for (int t = 0; t < s_state.num_triggers; t++) {
                if (s_state.triggers[t].id == pl->trigger_id) {
                    trig = &s_state.triggers[t];
                    break;
                }
            }
            if (!trig) continue;

            if (evaluate_trigger(trig)) {
                /* Reset and execute pipeline */
                vm_context_reset(&pl->ctx);
                pl->ctx.instruction_offset = pl->instruction_offset;
                pl->ctx.pc = pl->instruction_offset;
                pl->ctx.max_execution_ms = pl->max_execution_ms;

                vm_status_t result = vm_execute(&pl->ctx, pl->max_execution_ms);

                event_t evt;
                if (result == VM_HALTED) {
                    evt.type = EVT_PIPELINE_COMPLETE;
                    evt.data = pl->id;
                } else {
                    evt.type = EVT_PIPELINE_ERROR;
                    evt.data = pl->id | ((uint32_t)result << 16);
                    ESP_LOGW(TAG, "Pipeline %d ended with status %d", pl->id, result);
                }
                event_bus_publish(&evt);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SYS_SCHEDULER_TICK_MS));
    }

    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t scheduler_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    ESP_LOGI(TAG, "Scheduler initialized");
    return ESP_OK;
}

esp_err_t scheduler_start(void) {
    if (s_state.running) return ESP_OK;
    s_state.running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        scheduler_task, "scheduler", STACK_SIZE_SCHEDULER,
        NULL, TASK_PRIORITY_SCHEDULER, &s_task_handle, 1);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t scheduler_stop(void) {
    s_state.running = false;
    while (s_task_handle != NULL) vTaskDelay(10);
    ESP_LOGI(TAG, "Scheduler stopped");
    return ESP_OK;
}

const scheduler_state_t *scheduler_get_state(void) {
    return &s_state;
}

esp_err_t scheduler_add_trigger(const trigger_config_t *trigger) {
    if (s_state.num_triggers >= SYS_MAX_PIPELINES) return ESP_ERR_NO_MEM;
    s_state.triggers[s_state.num_triggers++] = *trigger;
    return ESP_OK;
}

esp_err_t scheduler_add_pipeline(const pipeline_config_t *pipeline) {
    if (s_state.num_pipelines >= SYS_MAX_PIPELINES) return ESP_ERR_NO_MEM;
    s_state.pipelines[s_state.num_pipelines] = *pipeline;
    vm_context_reset(&s_state.pipelines[s_state.num_pipelines].ctx);
    s_state.num_pipelines++;
    return ESP_OK;
}
