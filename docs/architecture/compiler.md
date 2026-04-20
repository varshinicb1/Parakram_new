# Bytecode Compiler

The Parakram compiler translates validated IR documents into compact, memory-safe bytecode that runs on the ESP32's FreeRTOS virtual machine.

## Instruction Set Architecture

Every instruction is exactly **8 bytes**:

| Byte | Field | Description |
|------|-------|-------------|
| 0 | `opcode` | Operation code (see table below) |
| 1 | `operand_a` | First operand (driver index, variable slot, etc.) |
| 2 | `operand_b` | Second operand |
| 3 | `operand_c` | Third operand |
| 4-7 | `immediate` | 32-bit immediate value (i32, f32, or u32) |

## Opcode Categories

| Range | Category | Examples |
|-------|----------|---------|
| `0x00-0x0A` | Stack ops | NOP, LOAD_IMM, STORE_VAR, DUP, POP |
| `0x10-0x1C` | Arithmetic | ADD, SUB, MUL, DIV (integer + float) |
| `0x20-0x29` | Comparison | EQ, NEQ, GT, LT, AND, OR, NOT |
| `0x40-0x44` | Control flow | JMP, JMP_IF, JMP_IFNOT, HALT, YIELD |
| `0x60-0x62` | Driver I/O | DRV_READ, DRV_WRITE, DRV_PWM |
| `0x70-0x72` | Comms | MQTT_PUB, BLE_NOTIFY, LOG |
| `0x80-0x85` | Display/UI | DISP_TEXT, DISP_VAL, IMG_BLOB, CREATE_OBJ, SET_PARENT, SET_FLEX |
| `0x90` | Timing | DELAY_MS |
| `0xF0-0xF1` | Pipeline | PIPELINE_START, PIPELINE_END |

## Security

Every compiled payload includes:
- A 4-byte magic header (`PRKM`)
- A 12-byte proprietary watermark (`PARAKRAM_NC`)
- An Ed25519 digital signature for tamper detection
- A CRC32 checksum for data integrity
