/**
 * @file vm.h
 * @brief Parakram bytecode VM — public interface.
 */

#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "system_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Opcodes (matching backend compiler)
 * ============================================================ */
#define OP_NOP          0x00
#define OP_LOAD_IMM_I   0x01
#define OP_LOAD_IMM_F   0x02
#define OP_LOAD_IMM_B   0x03
#define OP_LOAD_VAR     0x04
#define OP_STORE_VAR    0x05
#define OP_LOAD_CONST   0x06
#define OP_DUP          0x07
#define OP_POP          0x08
#define OP_SWAP         0x09
#define OP_INC_VAR      0x0A

#define OP_ADD_I        0x10
#define OP_SUB_I        0x11
#define OP_MUL_I        0x12
#define OP_DIV_I        0x13
#define OP_ADD_F        0x14
#define OP_SUB_F        0x15
#define OP_MUL_F        0x16
#define OP_DIV_F        0x17
#define OP_ABS_F        0x18
#define OP_CLAMP_F      0x19
#define OP_MAP_F        0x1A

#define OP_CMP_EQ       0x20
#define OP_CMP_NEQ      0x21
#define OP_CMP_GT       0x22
#define OP_CMP_LT       0x23
#define OP_CMP_GTE      0x24
#define OP_CMP_LTE      0x25
#define OP_AND          0x26
#define OP_OR           0x27
#define OP_NOT          0x28
#define OP_IN_RANGE     0x29

#define OP_JMP          0x40
#define OP_JMP_IF       0x41
#define OP_JMP_IFNOT    0x42
#define OP_HALT         0x43
#define OP_YIELD        0x44

#define OP_DRV_READ     0x60
#define OP_DRV_WRITE    0x61
#define OP_DRV_PWM      0x62

#define OP_MQTT_PUB     0x70
#define OP_BLE_NOTIFY   0x71
#define OP_LOG          0x72

#define OP_DISP_TEXT    0x80
#define OP_DISP_VAL     0x81

#define OP_DELAY_MS     0x90

#define OP_PIPELINE_START 0xF0
#define OP_PIPELINE_END   0xF1

/* ============================================================
 * Value type tag
 * ============================================================ */
typedef enum {
    VM_TYPE_INT   = 0,
    VM_TYPE_FLOAT = 1,
    VM_TYPE_BOOL  = 2,
} vm_type_t;

/* ============================================================
 * Stack value (tagged union, 8 bytes)
 * ============================================================ */
typedef struct {
    vm_type_t type;
    union {
        int32_t i;
        float   f;
        bool    b;
    };
} vm_value_t;

/* ============================================================
 * VM execution status
 * ============================================================ */
typedef enum {
    VM_IDLE     = 0,
    VM_RUNNING  = 1,
    VM_HALTED   = 2,
    VM_ERROR    = 3,
    VM_TIMEOUT  = 4,
} vm_status_t;

/* ============================================================
 * VM execution context (one per pipeline)
 * ============================================================ */
typedef struct {
    /* Program counter */
    uint16_t    pc;
    /* Operand stack */
    vm_value_t  stack[SYS_STACK_DEPTH];
    int8_t      sp;     /* Stack pointer (-1 = empty) */
    /* Pipeline metadata */
    uint8_t     pipeline_id;
    uint16_t    max_execution_ms;
    uint16_t    num_instructions;
    uint16_t    instruction_offset;     /* Offset in global instruction array */
    /* Execution tracking */
    vm_status_t status;
    uint32_t    start_tick;
    uint32_t    instructions_executed;
    uint32_t    total_executions;
    uint32_t    error_count;
    /* Error info */
    uint8_t     last_error_opcode;
    uint16_t    last_error_pc;
} vm_context_t;

/* ============================================================
 * Program descriptor (loaded from flash)
 * ============================================================ */
typedef struct {
    uint8_t     program_id[16];
    uint16_t    num_instructions;
    uint16_t    num_constants;
    uint16_t    num_pipelines;
    bool        loaded;
} program_info_t;

/* ============================================================
 * Public API
 * ============================================================ */

/** Initialize the VM subsystem. */
esp_err_t vm_init(void);

/** Load a verified program into the VM. */
esp_err_t vm_load_program(const uint8_t *instructions, uint16_t num_instructions,
                          const uint8_t *constants, uint16_t num_constants,
                          const uint8_t program_id[16]);

/** Execute a pipeline context until HALT, YIELD, or deadline. */
vm_status_t vm_execute(vm_context_t *ctx, uint32_t deadline_ms);

/** Reset a pipeline context for re-execution. */
void vm_context_reset(vm_context_t *ctx);

/** Get the current program info. */
const program_info_t *vm_get_program_info(void);

/** Unload the current program. */
esp_err_t vm_unload_program(void);

#ifdef __cplusplus
}
#endif

#endif /* VM_H */
