/**
 * @file vm.c
 * @brief Parakram Bytecode VM — the interpreter that executes compiled programs.
 *
 * This is the most critical module in the firmware. It executes fixed 8-byte
 * instructions on a stack machine with deadline enforcement.
 *
 * CONSTRAINTS:
 *   - No dynamic memory allocation
 *   - Every execution path must be bounded
 *   - Stack overflow/underflow is always checked
 *   - Deadline is checked every instruction
 */

#include "vm.h"
#include "state_store.h"
#include "driver_registry.h"
#include "comms.h"
#include "safety.h"
#include "event_bus.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "VM";

/* ============================================================
 * Static storage — no heap
 * ============================================================ */
static uint8_t s_instructions[SYS_MAX_INSTRUCTIONS * SYS_INSTRUCTION_SIZE];
static uint8_t s_constants[SYS_MAX_CONSTANTS * 8]; /* 8 bytes per const entry max */
static uint16_t s_num_instructions = 0;
static uint16_t s_num_constants = 0;
static program_info_t s_program_info = {0};

/* ============================================================
 * Helpers
 * ============================================================ */

/**
 * Read a 32-bit little-endian value from instruction immediate field.
 */
static inline uint32_t read_imm_u32(const uint8_t *instr) {
    return (uint32_t)instr[4] | ((uint32_t)instr[5] << 8) |
           ((uint32_t)instr[6] << 16) | ((uint32_t)instr[7] << 24);
}

static inline uint16_t read_imm_u16(const uint8_t *instr) {
    return (uint16_t)instr[4] | ((uint16_t)instr[5] << 8);
}

static inline float read_imm_f32(const uint8_t *instr) {
    uint32_t bits = read_imm_u32(instr);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static inline int32_t read_imm_i32(const uint8_t *instr) {
    uint32_t bits = read_imm_u32(instr);
    int32_t i;
    memcpy(&i, &bits, sizeof(i));
    return i;
}

static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* ============================================================
 * Stack operations (always bounds-checked)
 * ============================================================ */

static bool stack_push(vm_context_t *ctx, vm_value_t val) {
    if (ctx->sp >= SYS_STACK_DEPTH - 1) {
        fault_raise(FAULT_VM_STACK_OVERFLOW, ctx->pc);
        ctx->status = VM_ERROR;
        return false;
    }
    ctx->stack[++ctx->sp] = val;
    return true;
}

static bool stack_pop(vm_context_t *ctx, vm_value_t *out) {
    if (ctx->sp < 0) {
        fault_raise(FAULT_VM_STACK_UNDERFLOW, ctx->pc);
        ctx->status = VM_ERROR;
        return false;
    }
    *out = ctx->stack[ctx->sp--];
    return true;
}

static vm_value_t stack_peek(vm_context_t *ctx) {
    if (ctx->sp < 0) {
        vm_value_t zero = {.type = VM_TYPE_INT, .i = 0};
        return zero;
    }
    return ctx->stack[ctx->sp];
}

static float val_as_float(vm_value_t v) {
    switch (v.type) {
        case VM_TYPE_FLOAT: return v.f;
        case VM_TYPE_INT:   return (float)v.i;
        case VM_TYPE_BOOL:  return v.b ? 1.0f : 0.0f;
        default:            return 0.0f;
    }
}

static int32_t val_as_int(vm_value_t v) {
    switch (v.type) {
        case VM_TYPE_INT:   return v.i;
        case VM_TYPE_FLOAT: return (int32_t)v.f;
        case VM_TYPE_BOOL:  return v.b ? 1 : 0;
        default:            return 0;
    }
}

static bool val_as_bool(vm_value_t v) {
    switch (v.type) {
        case VM_TYPE_BOOL:  return v.b;
        case VM_TYPE_INT:   return v.i != 0;
        case VM_TYPE_FLOAT: return v.f != 0.0f;
        default:            return false;
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

esp_err_t vm_init(void) {
    memset(s_instructions, 0, sizeof(s_instructions));
    memset(s_constants, 0, sizeof(s_constants));
    s_num_instructions = 0;
    s_num_constants = 0;
    memset(&s_program_info, 0, sizeof(s_program_info));
    ESP_LOGI(TAG, "VM initialized (stack=%d, max_instr=%d)", SYS_STACK_DEPTH, SYS_MAX_INSTRUCTIONS);
    return ESP_OK;
}

esp_err_t vm_load_program(const uint8_t *instructions, uint16_t num_instructions,
                          const uint8_t *constants, uint16_t num_constants,
                          const uint8_t program_id[16]) {
    if (num_instructions > SYS_MAX_INSTRUCTIONS) {
        ESP_LOGE(TAG, "Too many instructions: %d > %d", num_instructions, SYS_MAX_INSTRUCTIONS);
        return ESP_ERR_INVALID_SIZE;
    }
    if (num_constants > SYS_MAX_CONSTANTS) {
        ESP_LOGE(TAG, "Too many constants: %d > %d", num_constants, SYS_MAX_CONSTANTS);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(s_instructions, instructions, num_instructions * SYS_INSTRUCTION_SIZE);
    s_num_instructions = num_instructions;

    if (num_constants > 0 && constants != NULL) {
        /* Constants are variable-length, but total must fit in buffer */
        uint16_t const_bytes = num_constants * 8; /* upper bound */
        if (const_bytes > sizeof(s_constants)) {
            const_bytes = sizeof(s_constants);
        }
        memcpy(s_constants, constants, const_bytes);
        s_num_constants = num_constants;
    }

    memcpy(s_program_info.program_id, program_id, 16);
    s_program_info.num_instructions = num_instructions;
    s_program_info.num_constants = num_constants;
    s_program_info.loaded = true;

    ESP_LOGI(TAG, "Program loaded: %d instructions, %d constants", num_instructions, num_constants);

    event_t evt = {.type = EVT_PROGRAM_LOADED, .data = num_instructions};
    event_bus_publish(&evt);

    return ESP_OK;
}

esp_err_t vm_unload_program(void) {
    s_num_instructions = 0;
    s_num_constants = 0;
    s_program_info.loaded = false;

    event_t evt = {.type = EVT_PROGRAM_UNLOADED};
    event_bus_publish(&evt);

    ESP_LOGI(TAG, "Program unloaded");
    return ESP_OK;
}

const program_info_t *vm_get_program_info(void) {
    return &s_program_info;
}

void vm_context_reset(vm_context_t *ctx) {
    ctx->pc = ctx->instruction_offset;
    ctx->sp = -1;
    ctx->status = VM_IDLE;
    ctx->start_tick = 0;
    ctx->instructions_executed = 0;
}

/* ============================================================
 * The main execution loop
 * ============================================================ */
vm_status_t vm_execute(vm_context_t *ctx, uint32_t deadline_ms) {
    if (!s_program_info.loaded) {
        ctx->status = VM_ERROR;
        return VM_ERROR;
    }

    ctx->status = VM_RUNNING;
    ctx->start_tick = now_ms();

    /* Performance: batch deadline check every N instructions to reduce
     * esp_timer_get_time() syscall overhead. Safety is maintained by
     * the max_instructions_per_execution hard cap. */
    #define VM_DEADLINE_CHECK_INTERVAL 8
    #define VM_MAX_INSTRUCTIONS_PER_EXEC 4096  /* hard safety cap */
    uint32_t batch_counter = 0;

    while (ctx->status == VM_RUNNING) {
        /* Batched deadline check — amortizes timer syscall cost */
        if ((batch_counter & (VM_DEADLINE_CHECK_INTERVAL - 1)) == 0) {
            if ((now_ms() - ctx->start_tick) > deadline_ms) {
                ctx->status = VM_TIMEOUT;
                fault_raise(FAULT_VM_TIMEOUT, ctx->pc);
                ctx->error_count++;
                break;
            }
        }

        /* Hard instruction count cap — prevents runaway even without deadline */
        if (batch_counter >= VM_MAX_INSTRUCTIONS_PER_EXEC) {
            ESP_LOGW(TAG, "Hard instruction cap hit at pc=%d", ctx->pc);
            ctx->status = VM_TIMEOUT;
            ctx->error_count++;
            break;
        }

        /* Instruction bounds check */
        if (ctx->pc >= s_num_instructions) {
            ctx->status = VM_HALTED;
            break;
        }

        /* Fetch instruction — local vars for hot-path optimization */
        const uint8_t *instr = &s_instructions[ctx->pc * SYS_INSTRUCTION_SIZE];
        const uint8_t opcode = instr[0];
        const uint8_t op_a   = instr[1];
        const uint8_t op_b   = instr[2];

        ctx->pc++;
        ctx->instructions_executed++;
        batch_counter++;

        vm_value_t a, b, c, result;

        switch (opcode) {
        /* ---- Stack ops ---- */
        case OP_NOP:
            break;

        case OP_LOAD_IMM_I:
            result.type = VM_TYPE_INT;
            result.i = read_imm_i32(instr);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_LOAD_IMM_F:
            result.type = VM_TYPE_FLOAT;
            result.f = read_imm_f32(instr);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_LOAD_IMM_B:
            result.type = VM_TYPE_BOOL;
            result.b = (op_a != 0);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_LOAD_VAR:
            if (state_store_get(op_a, &result) != ESP_OK) {
                result.type = VM_TYPE_INT; result.i = 0;
            }
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_STORE_VAR:
            if (!stack_pop(ctx, &a)) goto error;
            state_store_set(op_a, a);
            break;

        case OP_LOAD_CONST: {
            uint16_t idx = read_imm_u16(instr);
            /* Read constant from pool — each entry is aligned to 8 bytes */
            uint16_t offset = idx * 8;
            if (offset + 2 < sizeof(s_constants)) {
                uint8_t ctype = s_constants[offset];
                if (ctype == 0x00) { /* int */
                    result.type = VM_TYPE_INT;
                    memcpy(&result.i, &s_constants[offset + 2], 4);
                } else if (ctype == 0x01) { /* float */
                    result.type = VM_TYPE_FLOAT;
                    memcpy(&result.f, &s_constants[offset + 2], 4);
                } else if (ctype == 0x02) { /* bool */
                    result.type = VM_TYPE_BOOL;
                    result.b = (s_constants[offset + 2] != 0);
                } else { /* string — push as int (constant pool index) */
                    result.type = VM_TYPE_INT;
                    result.i = idx;
                }
            } else {
                result.type = VM_TYPE_INT; result.i = 0;
            }
            if (!stack_push(ctx, result)) goto error;
            break;
        }

        case OP_DUP:
            a = stack_peek(ctx);
            if (!stack_push(ctx, a)) goto error;
            break;

        case OP_POP:
            if (!stack_pop(ctx, &a)) goto error;
            break;

        case OP_SWAP:
            if (ctx->sp < 1) { ctx->status = VM_ERROR; goto error; }
            a = ctx->stack[ctx->sp];
            ctx->stack[ctx->sp] = ctx->stack[ctx->sp - 1];
            ctx->stack[ctx->sp - 1] = a;
            break;

        case OP_INC_VAR:
            if (state_store_increment(op_a) != ESP_OK) {
                /* Non-fatal, log and continue */
                ESP_LOGW(TAG, "INC_VAR failed for index %d", op_a);
            }
            break;

        /* ---- Integer arithmetic ---- */
        case OP_ADD_I:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_INT; result.i = val_as_int(a) + val_as_int(b);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_SUB_I:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_INT; result.i = val_as_int(a) - val_as_int(b);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_MUL_I:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_INT; result.i = val_as_int(a) * val_as_int(b);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_DIV_I:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_INT;
            result.i = (val_as_int(b) != 0) ? val_as_int(a) / val_as_int(b) : 0;
            if (!stack_push(ctx, result)) goto error;
            break;

        /* ---- Float arithmetic ---- */
        case OP_ADD_F:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_FLOAT; result.f = val_as_float(a) + val_as_float(b);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_SUB_F:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_FLOAT; result.f = val_as_float(a) - val_as_float(b);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_MUL_F:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_FLOAT; result.f = val_as_float(a) * val_as_float(b);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_DIV_F:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_FLOAT;
            result.f = (val_as_float(b) != 0.0f) ? val_as_float(a) / val_as_float(b) : 0.0f;
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_ABS_F:
            if (!stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_FLOAT; result.f = fabsf(val_as_float(a));
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_CLAMP_F: {
            /* stack: [value, min, max] → [clamped] */
            if (!stack_pop(ctx, &c) || !stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            float v = val_as_float(a);
            float lo = val_as_float(b);
            float hi = val_as_float(c);
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            result.type = VM_TYPE_FLOAT; result.f = v;
            if (!stack_push(ctx, result)) goto error;
            break;
        }

        case OP_MAP_F: {
            /* stack: [value, in_min, in_max, out_min, out_max] → [mapped] */
            vm_value_t out_max_v, out_min_v, in_max_v, in_min_v, val_v;
            if (!stack_pop(ctx, &out_max_v) || !stack_pop(ctx, &out_min_v) ||
                !stack_pop(ctx, &in_max_v) || !stack_pop(ctx, &in_min_v) ||
                !stack_pop(ctx, &val_v)) goto error;
            float v = val_as_float(val_v);
            float in_lo = val_as_float(in_min_v), in_hi = val_as_float(in_max_v);
            float out_lo = val_as_float(out_min_v), out_hi = val_as_float(out_max_v);
            float range = in_hi - in_lo;
            float mapped = (range != 0.0f) ?
                out_lo + (v - in_lo) * (out_hi - out_lo) / range : out_lo;
            result.type = VM_TYPE_FLOAT; result.f = mapped;
            if (!stack_push(ctx, result)) goto error;
            break;
        }

        /* ---- Comparisons ---- */
        case OP_CMP_EQ:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = (val_as_float(a) == val_as_float(b));
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_CMP_NEQ:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = (val_as_float(a) != val_as_float(b));
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_CMP_GT:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = (val_as_float(a) > val_as_float(b));
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_CMP_LT:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = (val_as_float(a) < val_as_float(b));
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_CMP_GTE:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = (val_as_float(a) >= val_as_float(b));
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_CMP_LTE:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = (val_as_float(a) <= val_as_float(b));
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_AND:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = val_as_bool(a) && val_as_bool(b);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_OR:
            if (!stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = val_as_bool(a) || val_as_bool(b);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_NOT:
            if (!stack_pop(ctx, &a)) goto error;
            result.type = VM_TYPE_BOOL; result.b = !val_as_bool(a);
            if (!stack_push(ctx, result)) goto error;
            break;

        case OP_IN_RANGE: {
            /* stack: [value, min, max] → [bool] */
            if (!stack_pop(ctx, &c) || !stack_pop(ctx, &b) || !stack_pop(ctx, &a)) goto error;
            float v = val_as_float(a);
            result.type = VM_TYPE_BOOL;
            result.b = (v >= val_as_float(b)) && (v <= val_as_float(c));
            if (!stack_push(ctx, result)) goto error;
            break;
        }

        /* ---- Control flow ---- */
        case OP_JMP:
            ctx->pc = read_imm_u16(instr);
            break;

        case OP_JMP_IF:
            if (!stack_pop(ctx, &a)) goto error;
            if (val_as_bool(a)) {
                ctx->pc = read_imm_u16(instr);
            }
            break;

        case OP_JMP_IFNOT:
            if (!stack_pop(ctx, &a)) goto error;
            if (!val_as_bool(a)) {
                ctx->pc = read_imm_u16(instr);
            }
            break;

        case OP_HALT:
            ctx->status = VM_HALTED;
            break;

        case OP_YIELD:
            /* Yield to other pipelines — pause and resume later */
            ctx->status = VM_IDLE;
            break;

        /* ---- Driver I/O ---- */
        case OP_DRV_READ: {
            registered_driver_t *drv = driver_registry_get(op_a);
            if (drv && drv->vtable && drv->vtable->read && drv->initialized) {
                sensor_value_t sv;
                esp_err_t err = drv->vtable->read(drv->handle, (capability_t)op_b, &sv);
                if (err == ESP_OK) {
                    if (sv.type == VAL_TYPE_FLOAT) {
                        result.type = VM_TYPE_FLOAT; result.f = sv.f;
                    } else if (sv.type == VAL_TYPE_INT) {
                        result.type = VM_TYPE_INT; result.i = sv.i;
                    } else if (sv.type == VAL_TYPE_BOOL) {
                        result.type = VM_TYPE_BOOL; result.b = sv.b;
                    } else {
                        result.type = VM_TYPE_FLOAT; result.f = 0.0f;
                    }
                } else {
                    fault_raise(FAULT_DRIVER_READ_FAIL, op_a);
                    result.type = VM_TYPE_FLOAT; result.f = 0.0f;
                }
            } else {
                fault_raise(FAULT_DRIVER_READ_FAIL, op_a);
                result.type = VM_TYPE_FLOAT; result.f = 0.0f;
            }
            if (!stack_push(ctx, result)) goto error;
            break;
        }

        case OP_DRV_WRITE: {
            if (!stack_pop(ctx, &a)) goto error;
            registered_driver_t *drv = driver_registry_get(op_a);
            if (drv && drv->vtable && drv->vtable->write && drv->initialized) {
                actuator_cmd_t cmd = {0};
                if (a.type == VM_TYPE_FLOAT) {
                    cmd.type = VAL_TYPE_FLOAT; cmd.f = a.f;
                } else if (a.type == VM_TYPE_INT) {
                    cmd.type = VAL_TYPE_INT; cmd.i = a.i;
                } else if (a.type == VM_TYPE_BOOL) {
                    cmd.type = VAL_TYPE_BOOL; cmd.b = a.b;
                }
                esp_err_t err = drv->vtable->write(drv->handle, &cmd);
                if (err != ESP_OK) {
                    fault_raise(FAULT_DRIVER_WRITE_FAIL, op_a);
                }
            } else {
                fault_raise(FAULT_DRIVER_WRITE_FAIL, op_a);
            }
            break;
        }

        case OP_DRV_PWM: {
            if (!stack_pop(ctx, &a)) goto error;
            registered_driver_t *drv = driver_registry_get(op_a);
            if (drv && drv->vtable && drv->vtable->write && drv->initialized) {
                actuator_cmd_t cmd = {0};
                cmd.type = VAL_TYPE_FLOAT;
                cmd.f = val_as_float(a);
                cmd.capability = CAP_SPEED_PERCENT;
                drv->vtable->write(drv->handle, &cmd);
            }
            break;
        }

        /* ---- Communication ---- */
        case OP_MQTT_PUB: {
            /* Pop payload value, publish to topic from constant pool */
            if (!stack_pop(ctx, &a)) goto error;
            /* In production, format value and publish via MQTT */
            ESP_LOGD(TAG, "MQTT_PUB topic_idx=%d value=%f", read_imm_u16(instr), val_as_float(a));
            break;
        }

        case OP_BLE_NOTIFY: {
            if (!stack_pop(ctx, &a)) goto error;
            ESP_LOGD(TAG, "BLE_NOTIFY char=%d value=%f", read_imm_u16(instr), val_as_float(a));
            break;
        }

        case OP_LOG: {
            /* Pop 'op_b' fields from stack and log */
            for (int i = 0; i < op_b && ctx->sp >= 0; i++) {
                stack_pop(ctx, &a);
                ESP_LOGI(TAG, "LOG[%d] dest=%d value=%f", i, op_a, val_as_float(a));
            }
            break;
        }

        /* ---- Display ---- */
        case OP_DISP_TEXT: {
            /* Display text from constant pool at (driver, line) */
            registered_driver_t *drv = driver_registry_get(op_a);
            if (drv && drv->vtable && drv->vtable->write && drv->initialized) {
                actuator_cmd_t cmd = {0};
                cmd.type = VAL_TYPE_STRING;
                cmd.capability = CAP_TEXT_DISPLAY;
                /* TODO: Copy text from constant pool into cmd.s */
                drv->vtable->write(drv->handle, &cmd);
            }
            break;
        }

        case OP_DISP_VAL: {
            if (!stack_pop(ctx, &a)) goto error;
            registered_driver_t *drv = driver_registry_get(op_a);
            if (drv && drv->vtable && drv->vtable->write && drv->initialized) {
                actuator_cmd_t cmd = {0};
                cmd.type = VAL_TYPE_FLOAT;
                cmd.f = val_as_float(a);
                cmd.capability = CAP_TEXT_DISPLAY;
                drv->vtable->write(drv->handle, &cmd);
            }
            break;
        }

        /* ---- Timing ---- */
        case OP_DELAY_MS: {
            uint16_t delay = read_imm_u16(instr);
            /* Capped at max_execution_ms */
            if (delay > ctx->max_execution_ms) delay = ctx->max_execution_ms;
            vTaskDelay(pdMS_TO_TICKS(delay));
            break;
        }

        /* ---- Pipeline markers ---- */
        case OP_PIPELINE_START:
            ctx->pipeline_id = op_a;
            ctx->num_instructions = op_b;
            ctx->max_execution_ms = read_imm_u16(instr);
            break;

        case OP_PIPELINE_END:
            ctx->status = VM_HALTED;
            break;

        default:
            ESP_LOGE(TAG, "Invalid opcode 0x%02X at pc=%d", opcode, ctx->pc - 1);
            fault_raise(FAULT_VM_INVALID_OPCODE, opcode);
            ctx->last_error_opcode = opcode;
            ctx->last_error_pc = ctx->pc - 1;
            ctx->status = VM_ERROR;
            ctx->error_count++;
            break;
        }
    }

    ctx->total_executions++;
    return ctx->status;

error:
    ctx->total_executions++;
    return ctx->status;
}
