/**
 * @file system_config.h
 * @brief Parakram firmware system-wide configuration constants.
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

/* ============================================================
 * Program Limits (matching bytecode ISA)
 * ============================================================ */
#define SYS_MAX_INSTRUCTIONS        1024
#define SYS_INSTRUCTION_SIZE        8       /* bytes per instruction */
#define SYS_MAX_PIPELINES           16
#define SYS_MAX_DEVICES             32
#define SYS_MAX_STATE_VARIABLES     64
#define SYS_MAX_CONSTANTS           128
#define SYS_MAX_NODES_PER_PIPELINE  64
#define SYS_STACK_DEPTH             16

/* ============================================================
 * Timing
 * ============================================================ */
#define SYS_MIN_TRIGGER_INTERVAL_MS 100
#define SYS_MAX_EXECUTION_MS        5000
#define SYS_WATCHDOG_TIMEOUT_S      10
#define SYS_SCHEDULER_TICK_MS       1

/* ============================================================
 * Communication
 * ============================================================ */
#define SYS_WIFI_CONFIG_PORT        8423
#define SYS_BLE_MTU                 517
#define SYS_BLE_CHUNK_SIZE          508

/* ============================================================
 * Storage
 * ============================================================ */
#define SYS_PROGRAM_PARTITION       "program"
#define SYS_PROGRAM_MAX_SIZE        (64 * 1024)  /* 64KB */
#define SYS_TELEMETRY_BUFFER_SIZE   32

/* ============================================================
 * Bytecode Format
 * ============================================================ */
#define SYS_BYTECODE_MAGIC_0        0x50  /* 'P' */
#define SYS_BYTECODE_MAGIC_1        0x52  /* 'R' */
#define SYS_BYTECODE_MAGIC_2        0x4B  /* 'K' */
#define SYS_BYTECODE_MAGIC_3        0x4D  /* 'M' */
#define SYS_BYTECODE_VERSION        1
#define SYS_HEADER_SIZE             32
#define SYS_SIG_BLOCK_SIZE          72
#define SYS_SIGNATURE_SIZE          64
#define SYS_PUBKEY_SIZE             32

/* ============================================================
 * Task Priorities (higher = more important)
 * ============================================================ */
#define TASK_PRIORITY_WATCHDOG      (configMAX_PRIORITIES - 1)
#define TASK_PRIORITY_SCHEDULER     (configMAX_PRIORITIES - 2)
#define TASK_PRIORITY_VM            (configMAX_PRIORITIES - 3)
#define TASK_PRIORITY_COMMS         (configMAX_PRIORITIES - 4)
#define TASK_PRIORITY_TELEMETRY     (configMAX_PRIORITIES - 5)

/* ============================================================
 * Task Stack Sizes
 * ============================================================ */
#define STACK_SIZE_SCHEDULER        4096
#define STACK_SIZE_VM               4096
#define STACK_SIZE_WIFI             4096
#define STACK_SIZE_BLE              4096
#define STACK_SIZE_TELEMETRY        2048

#endif /* SYSTEM_CONFIG_H */
