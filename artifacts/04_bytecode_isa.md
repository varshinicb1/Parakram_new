# Artifact 4 — Bytecode ISA (Instruction Set Architecture)

## Design Parameters

| Parameter | Value |
|-----------|-------|
| Instruction width | 8 bytes (fixed) |
| Max program size | 1024 instructions (8 KB) |
| Operand stack depth | 16 values per pipeline |
| Stack value width | 8 bytes (union: int32, float32, bool, const_index) |
| Endianness | Little-endian |
| Constant pool | Max 128 entries, separately appended |

---

## Instruction Format

Every instruction is exactly 8 bytes:

```
Byte:   0         1         2-3           4-5           6-7
      ┌─────────┬─────────┬─────────────┬─────────────┬─────────────┐
      │ opcode  │  flags  │  operand_a  │  operand_b  │  operand_c  │
      │  (1B)   │  (1B)   │   (2B LE)   │   (2B LE)   │   (2B LE)   │
      └─────────┴─────────┴─────────────┴─────────────┴─────────────┘
```

### Flags byte

```
Bit 7 (MSB): type_a  ─┐ Operand A type: 00=int, 01=float, 10=bool, 11=const_ref
Bit 6:       type_a  ─┘
Bit 5:       type_b  ─┐ Operand B type (same encoding)
Bit 4:       type_b  ─┘
Bit 3:       signed    Operand A is signed integer
Bit 2:       wide      Operand A uses operand_b as upper 16 bits (32-bit immediate)
Bit 1:       reserved
Bit 0:       reserved
```

---

## Complete Opcode Table

### Category: Data Operations (0x00 – 0x0F)

| Opcode | Hex  | Mnemonic    | Operand A        | Operand B        | Operand C  | Stack Effect       | Description |
|--------|------|-------------|------------------|------------------|------------|--------------------|-------------|
| 0      | 0x00 | NOP         | —                | —                | —          | (—)                | No operation |
| 1      | 0x01 | LOAD_IMM_I  | imm16 (value)    | imm16 (ext, if wide) | —    | (— → val)         | Push immediate int16/int32 |
| 2      | 0x02 | LOAD_IMM_F  | imm16 (mantissa) | imm16 (exponent) | —          | (— → val)         | Push immediate float (custom encoding) |
| 3      | 0x03 | LOAD_IMM_B  | bool (0 or 1)    | —                | —          | (— → val)         | Push immediate boolean |
| 4      | 0x04 | LOAD_VAR    | var_index        | —                | —          | (— → val)         | Push state variable by index |
| 5      | 0x05 | STORE_VAR   | var_index        | —                | —          | (val →)            | Pop TOS, store to state variable |
| 6      | 0x06 | LOAD_CONST  | const_index      | —                | —          | (— → val)         | Push value from constant pool |
| 7      | 0x07 | DUP         | —                | —                | —          | (val → val, val)   | Duplicate top of stack |
| 8      | 0x08 | DROP        | —                | —                | —          | (val →)            | Discard top of stack |
| 9      | 0x09 | SWAP        | —                | —                | —          | (a, b → b, a)     | Swap top two values |
| 10     | 0x0A | LOAD_IMM_FP | ieee754_hi       | ieee754_lo       | —          | (— → val)         | Push IEEE 754 float (full precision) |

### Category: Arithmetic (0x10 – 0x1F)

| Opcode | Hex  | Mnemonic | Operand A | Operand B | Operand C | Stack Effect          | Description |
|--------|------|----------|-----------|-----------|-----------|----------------------|-------------|
| 16     | 0x10 | ADD_I    | —         | —         | —         | (a, b → a+b)        | Integer addition |
| 17     | 0x11 | SUB_I    | —         | —         | —         | (a, b → a-b)        | Integer subtraction |
| 18     | 0x12 | MUL_I    | —         | —         | —         | (a, b → a*b)        | Integer multiplication |
| 19     | 0x13 | DIV_I    | —         | —         | —         | (a, b → a/b)        | Integer division (ABORT on div-by-zero) |
| 20     | 0x14 | MOD_I    | —         | —         | —         | (a, b → a%b)        | Integer modulo |
| 21     | 0x15 | NEG_I    | —         | —         | —         | (a → -a)            | Integer negate |
| 22     | 0x16 | ABS_I    | —         | —         | —         | (a → |a|)           | Integer absolute value |
| 24     | 0x18 | ADD_F    | —         | —         | —         | (a, b → a+b)        | Float addition |
| 25     | 0x19 | SUB_F    | —         | —         | —         | (a, b → a-b)        | Float subtraction |
| 26     | 0x1A | MUL_F    | —         | —         | —         | (a, b → a*b)        | Float multiplication |
| 27     | 0x1B | DIV_F    | —         | —         | —         | (a, b → a/b)        | Float division (ABORT on div-by-zero) |
| 28     | 0x1C | NEG_F    | —         | —         | —         | (a → -a)            | Float negate |
| 29     | 0x1D | ABS_F    | —         | —         | —         | (a → |a|)           | Float absolute value |

### Category: Comparison (0x20 – 0x2F)

| Opcode | Hex  | Mnemonic | Operand A | Operand B | Operand C | Stack Effect       | Description |
|--------|------|----------|-----------|-----------|-----------|-------------------|-------------|
| 32     | 0x20 | CMP_EQ   | —         | —         | —         | (a, b → bool)    | Equal |
| 33     | 0x21 | CMP_NEQ  | —         | —         | —         | (a, b → bool)    | Not equal |
| 34     | 0x22 | CMP_GT   | —         | —         | —         | (a, b → bool)    | Greater than |
| 35     | 0x23 | CMP_LT   | —         | —         | —         | (a, b → bool)    | Less than |
| 36     | 0x24 | CMP_GTE  | —         | —         | —         | (a, b → bool)    | Greater than or equal |
| 37     | 0x25 | CMP_LTE  | —         | —         | —         | (a, b → bool)    | Less than or equal |
| 38     | 0x26 | CMP_RANGE| —         | —         | —         | (val, lo, hi → bool) | val ≥ lo AND val ≤ hi |

### Category: Logic (0x30 – 0x3F)

| Opcode | Hex  | Mnemonic | Operand A | Operand B | Operand C | Stack Effect       | Description |
|--------|------|----------|-----------|-----------|-----------|-------------------|-------------|
| 48     | 0x30 | AND      | —         | —         | —         | (a, b → a&&b)    | Logical AND |
| 49     | 0x31 | OR       | —         | —         | —         | (a, b → a||b)    | Logical OR |
| 50     | 0x32 | NOT      | —         | —         | —         | (a → !a)          | Logical NOT |
| 51     | 0x33 | XOR      | —         | —         | —         | (a, b → a^b)     | Logical XOR |

### Category: Control Flow (0x40 – 0x4F)

| Opcode | Hex  | Mnemonic  | Operand A      | Operand B | Operand C | Stack Effect  | Description |
|--------|------|-----------|----------------|-----------|-----------|--------------|-------------|
| 64     | 0x40 | JMP       | target_pc      | —         | —         | (—)          | Unconditional jump to instruction index |
| 65     | 0x41 | JMP_IF    | target_pc      | —         | —         | (bool →)     | Jump if TOS is true, otherwise fall-through |
| 66     | 0x42 | JMP_IFNOT | target_pc     | —         | —         | (bool →)     | Jump if TOS is false |
| 67     | 0x43 | HALT      | —              | —         | —         | (—)          | Normal pipeline end, trigger cleanup |
| 68     | 0x44 | ABORT     | error_code     | —         | —         | (—)          | Fault condition, safe state, log error |
| 69     | 0x45 | YIELD     | —              | —         | —         | (—)          | Yield execution to scheduler (cooperative) |

### Category: Type Conversion (0x50 – 0x5F)

| Opcode | Hex  | Mnemonic | Operand A | Operand B | Operand C | Stack Effect    | Description |
|--------|------|----------|-----------|-----------|-----------|----------------|-------------|
| 80     | 0x50 | I2F      | —         | —         | —         | (int → float)  | Convert integer to float |
| 81     | 0x51 | F2I      | —         | —         | —         | (float → int)  | Convert float to integer (truncate) |
| 82     | 0x52 | B2I      | —         | —         | —         | (bool → int)   | Convert bool to int (0 or 1) |
| 83     | 0x53 | I2B      | —         | —         | —         | (int → bool)   | Convert int to bool (0=false, else true) |

### Category: I/O — Driver Operations (0x60 – 0x6F)

| Opcode | Hex  | Mnemonic   | Operand A      | Operand B      | Operand C  | Stack Effect          | Description |
|--------|------|------------|----------------|----------------|------------|-----------------------|-------------|
| 96     | 0x60 | DRV_READ   | driver_index   | field_index    | —          | (— → val)            | Read sensor value, push to stack |
| 97     | 0x61 | DRV_WRITE  | driver_index   | —              | —          | (val →)               | Pop value, write to actuator |
| 98     | 0x62 | DRV_WRITE_I| driver_index   | imm16 (value)  | —          | (—)                   | Write immediate value to actuator |
| 99     | 0x63 | DRV_PWM    | driver_index   | duty_x100      | —          | (—)                   | Set PWM duty (operand_b = duty% * 100) |
| 100    | 0x64 | DRV_PWM_S  | driver_index   | —              | —          | (val →)               | Set PWM duty from stack (float 0-100) |
| 101    | 0x65 | BUS_LOCK   | bus_index      | timeout_ms     | —          | (— → bool)           | Acquire bus mutex, push success |
| 102    | 0x66 | BUS_UNLOCK | bus_index      | —              | —          | (—)                   | Release bus mutex |
| 103    | 0x67 | DRV_STATUS | driver_index   | —              | —          | (— → int)            | Push driver status (0=OK, else error) |

### Category: Communications (0x70 – 0x7F)

| Opcode | Hex  | Mnemonic   | Operand A      | Operand B      | Operand C  | Stack Effect          | Description |
|--------|------|------------|----------------|----------------|------------|-----------------------|-------------|
| 112    | 0x70 | MQTT_PUB   | topic_c_idx    | payload_c_idx  | qos        | (—)                   | Publish MQTT message (const pool refs) |
| 113    | 0x71 | MQTT_PUB_V | topic_c_idx    | var_index      | qos        | (—)                   | Publish state variable as MQTT payload |
| 114    | 0x72 | BLE_NOTIFY | char_index     | —              | —          | (val →)               | Notify BLE characteristic with TOS value |
| 115    | 0x73 | BLE_NOTIF_V| char_index     | var_index      | —          | (—)                   | Notify BLE with state variable value |
| 116    | 0x74 | LOG_SD     | num_fields     | field_start_idx| —          | (—)                   | Log state variables to SD card |
| 117    | 0x75 | LOG_FLASH  | num_fields     | field_start_idx| —          | (—)                   | Log state variables to flash partition |
| 118    | 0x76 | ESPNOW_SEND| peer_index     | payload_c_idx  | —          | (—)                   | Send ESP-NOW message |
| 119    | 0x77 | LORA_SEND  | payload_c_idx  | —              | —          | (—)                   | Send LoRa message |

### Category: Display (0x80 – 0x8F)

| Opcode | Hex  | Mnemonic   | Operand A      | Operand B      | Operand C  | Stack Effect          | Description |
|--------|------|------------|----------------|----------------|------------|-----------------------|-------------|
| 128    | 0x80 | DISP_TEXT  | driver_index   | text_c_idx     | line_num   | (—)                   | Display text from const pool on line |
| 129    | 0x81 | DISP_VAR   | driver_index   | var_index      | line_num   | (—)                   | Display state variable value on line |
| 130    | 0x82 | DISP_CLEAR | driver_index   | —              | —          | (—)                   | Clear display |
| 131    | 0x83 | LED_SET    | driver_index   | led_index      | —          | (r, g, b →)          | Set WS2812 LED color (pop RGB) |
| 132    | 0x84 | LED_SET_I  | driver_index   | led_index      | rgb_packed | (—)                   | Set LED color from immediate (R5G6B5) |
| 133    | 0x85 | LED_FILL   | driver_index   | —              | —          | (r, g, b →)          | Fill all LEDs with color |
| 134    | 0x86 | LED_SHOW   | driver_index   | —              | —          | (—)                   | Flush LED buffer to strip |

### Category: Time (0x90 – 0x9F)

| Opcode | Hex  | Mnemonic  | Operand A | Operand B | Operand C | Stack Effect       | Description |
|--------|------|-----------|-----------|-----------|-----------|-------------------|-------------|
| 144    | 0x90 | GET_TIME  | —         | —         | —         | (— → int)        | Push Unix timestamp (seconds) |
| 145    | 0x91 | GET_TICKS | —         | —         | —         | (— → int)        | Push system ticks (ms since boot) |
| 146    | 0x92 | DELAY_MS  | duration  | —         | —         | (—)               | Cooperative delay (yield + resume) |
| 147    | 0x93 | DELAY_MS_S| —         | —         | —         | (ms →)            | Delay by TOS milliseconds |

### Category: State Manipulation (0xA0 – 0xAF)

| Opcode | Hex  | Mnemonic   | Operand A  | Operand B | Operand C | Stack Effect   | Description |
|--------|------|------------|------------|-----------|-----------|---------------|-------------|
| 160    | 0xA0 | VAR_INC    | var_index  | imm16     | —         | (—)           | Increment state variable by imm16 |
| 161    | 0xA1 | VAR_DEC    | var_index  | imm16     | —         | (—)           | Decrement state variable by imm16 |
| 162    | 0xA2 | VAR_RESET  | var_index  | —         | —         | (—)           | Reset variable to initial value |
| 163    | 0xA3 | VAR_CLAMP  | var_index  | min_c_idx | max_c_idx | (—)           | Clamp variable to [min, max] from const pool |
| 164    | 0xA4 | VAR_MAP    | var_index  | map_c_idx | —         | (—)           | Apply mapping function (const pool entry) |

---

## Bytecode Binary Format

### Program header (32 bytes)

```
Offset  Size  Field           Description
──────  ────  ─────           ───────────
0x00    4     magic           0x50524B4D ("PRKM")
0x04    2     version         Bytecode format version (1)
0x06    2     flags           Bit flags: [0]=has_const_pool, [1]=has_debug_info
0x08    16    program_id      UUID (128bit, binary)
0x18    4     device_id_hash  CRC32 of target device UUID
0x1C    2     num_instructions Total instruction count
0x1E    2     num_constants   Constant pool entry count
```

### Instruction block

Immediately follows header. Each instruction is 8 bytes.
Total size: `num_instructions * 8` bytes.

### Constant pool

Follows instruction block. Each entry:

```
Offset  Size  Field           Description
──────  ────  ─────           ───────────
0x00    1     type            0=int32, 1=float32, 2=string, 3=topic
0x01    1     length          Byte length of value (for strings)
0x02    2     reserved        Alignment padding
0x04    4-36  value           Value data (4 bytes for int/float, up to 32+4 for strings)
```

String entries are padded to 36 bytes (4-byte header + 32-byte max string).
Numeric entries are padded to 8 bytes.

### Signature block (72 bytes)

Appended after constant pool:

```
Offset  Size  Field           Description
──────  ────  ─────           ───────────
0x00    4     sig_magic       0x53494730 ("SIG0")
0x04    4     signed_length   Number of bytes covered by signature (header + code + consts)
0x08    64    signature       Ed25519 signature (512 bits)
```

### Total payload structure

```
┌──────────────────────────────┐
│ Header (32 bytes)            │
├──────────────────────────────┤
│ Instructions (N × 8 bytes)   │
├──────────────────────────────┤
│ Constant Pool (M entries)    │
├──────────────────────────────┤
│ Signature Block (72 bytes)   │
└──────────────────────────────┘
```

Max total payload size: 32 + (1024 × 8) + (128 × 36) + 72 = **12,904 bytes** (~12.6 KB)

---

## Encoding Example — Sample Pipeline

### Source IR Pipeline: "Turn on fan when temperature > 30°C"

```json
{
  "id": "temp_fan_control",
  "trigger": "every_30s",
  "nodes": [
    { "id": "n1", "type": "sensor.read", "device": "temp_sensor", "field": "temperature", "store_to": "temperature" },
    { "id": "n2", "type": "condition.compare", "left": "$temperature", "op": "gt", "right": 30.0, "if_true": "n3", "if_false": null },
    { "id": "n3", "type": "actuator.write", "device": "relay_fan", "value": true }
  ],
  "max_execution_ms": 200
}
```

### Index assignments (resolved by compiler)

| Entity | Type | Index |
|--------|------|-------|
| temp_sensor | driver | 0 |
| relay_fan | driver | 1 |
| temperature (field) | field | 0 (temperature capability) |
| temperature (state var) | variable | 0 |
| 30.0 | constant (float) | 0 |

### Compiled bytecode (6 instructions + HALT)

```
Addr  Hex Dump                        Mnemonic        Operands                    Stack After
────  ────────────────────────────────  ──────────────  ─────────────────────────  ───────────
0x00  60 00 00 00 00 00 00 00          DRV_READ        drv=0, field=0             [temp_val]
0x08  05 00 00 00 00 00 00 00          STORE_VAR       var=0                      []
0x10  04 00 00 00 00 00 00 00          LOAD_VAR        var=0                      [temp_val]
0x18  06 40 00 00 00 00 00 00          LOAD_CONST      const=0 (type=float)       [temp_val, 30.0]
0x20  22 00 00 00 00 00 00 00          CMP_GT                                     [bool_result]
0x28  41 00 05 00 00 00 00 00          JMP_IF          target=5                   []
0x30  43 00 00 00 00 00 00 00          HALT            (skipped if jump taken)    []
0x38  03 00 01 00 00 00 00 00          LOAD_IMM_B      true (1)                   [true]
0x40  61 00 01 00 00 00 00 00          DRV_WRITE       drv=1                      []
0x48  43 00 00 00 00 00 00 00          HALT                                       []
```

### Detailed instruction breakdown

#### Instruction 0: `DRV_READ drv=0, field=0`
```
Byte 0: 0x60  → opcode = DRV_READ (96)
Byte 1: 0x00  → flags = 0 (no type modifiers)
Byte 2-3: 0x0000 → operand_a = 0 (driver index: temp_sensor)
Byte 4-5: 0x0000 → operand_b = 0 (field index: temperature)
Byte 6-7: 0x0000 → operand_c = unused
Effect: Calls temp_sensor.read(field=temperature), pushes float value to stack
```

#### Instruction 1: `STORE_VAR var=0`
```
Byte 0: 0x05  → opcode = STORE_VAR (5)
Byte 1: 0x00  → flags = 0
Byte 2-3: 0x0000 → operand_a = 0 (state variable index: temperature)
Byte 4-5: 0x0000 → unused
Byte 6-7: 0x0000 → unused
Effect: Pops TOS (temperature reading), stores to state_store[0]
```

#### Instruction 2: `LOAD_VAR var=0`
```
Byte 0: 0x04  → opcode = LOAD_VAR (4)
Byte 1: 0x00  → flags = 0
Byte 2-3: 0x0000 → operand_a = 0 (variable: temperature)
Effect: Pushes state_store[0] (temperature value) to stack
```

#### Instruction 3: `LOAD_CONST const=0`
```
Byte 0: 0x06  → opcode = LOAD_CONST (6)
Byte 1: 0x40  → flags = 0b01000000 (type_a = 01 = float)
Byte 2-3: 0x0000 → operand_a = 0 (constant pool index)
Effect: Pushes const_pool[0] (30.0f) to stack
```

#### Instruction 4: `CMP_GT`
```
Byte 0: 0x22  → opcode = CMP_GT (34)
Byte 1: 0x00  → flags = 0
Effect: Pops two values (temperature, 30.0), pushes (temperature > 30.0) as bool
```

#### Instruction 5: `JMP_IF target=7`
```
Byte 0: 0x41  → opcode = JMP_IF (65)
Byte 1: 0x00  → flags = 0
Byte 2-3: 0x0007 → operand_a = 7 (instruction index of LOAD_IMM_B true)
Effect: Pops bool. If true, jump to instruction 7. If false, fall through to HALT (6).
```

#### Instruction 6: `HALT` (false path)
```
Byte 0: 0x43  → opcode = HALT (67)
Effect: Pipeline ends normally (temperature was ≤ 30°C, fan stays off)
```

#### Instruction 7: `LOAD_IMM_B true`
```
Byte 0: 0x03  → opcode = LOAD_IMM_B (3)
Byte 2-3: 0x0001 → operand_a = 1 (true)
Effect: Pushes boolean true to stack
```

#### Instruction 8: `DRV_WRITE drv=1`
```
Byte 0: 0x61  → opcode = DRV_WRITE (97)
Byte 2-3: 0x0001 → operand_a = 1 (driver index: relay_fan)
Effect: Pops TOS (true), writes to relay_fan → GPIO HIGH → fan ON
```

#### Instruction 9: `HALT` (true path)
```
Byte 0: 0x43  → opcode = HALT (67)
Effect: Pipeline ends normally (fan turned on)
```

### Constant pool for this program

```
Index  Type    Value     Hex
─────  ──────  ────────  ────────────────
0      float   30.0      01 04 00 00 00 00 F0 41
                          ↑  ↑         ↑──────────── IEEE 754 LE: 30.0 = 0x41F00000
                          │  └── length=4
                          └── type=1 (float)
```

### Complete binary dump (example program)

```
Header (32 bytes):
4D 52 4B 50 01 00 00 00  ← magic "PRKM" (LE), version=1, flags=0
XX XX XX XX XX XX XX XX  ← program_id UUID bytes 0-7
XX XX XX XX XX XX XX XX  ← program_id UUID bytes 8-15
YY YY YY YY             ← device_id CRC32
0A 00                    ← num_instructions = 10
01 00                    ← num_constants = 1

Instructions (80 bytes):
60 00 00 00 00 00 00 00  ← DRV_READ(0, 0)
05 00 00 00 00 00 00 00  ← STORE_VAR(0)
04 00 00 00 00 00 00 00  ← LOAD_VAR(0)
06 40 00 00 00 00 00 00  ← LOAD_CONST(0)
22 00 00 00 00 00 00 00  ← CMP_GT
41 00 07 00 00 00 00 00  ← JMP_IF(7)
43 00 00 00 00 00 00 00  ← HALT
03 00 01 00 00 00 00 00  ← LOAD_IMM_B(1)
61 00 01 00 00 00 00 00  ← DRV_WRITE(1)
43 00 00 00 00 00 00 00  ← HALT

Constant pool (8 bytes):
01 04 00 00 00 00 F0 41  ← float 30.0

Signature block (72 bytes):
30 47 49 53             ← sig_magic "SIG0"
78 00 00 00             ← signed_length = 120 (32+80+8)
XX ... XX               ← Ed25519 signature (64 bytes)

Total: 192 bytes
```

---

## VM Execution Pseudocode

```c
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t operand_a;
    uint16_t operand_b;
    uint16_t operand_c;
} instruction_t;

typedef union {
    int32_t  i;
    float    f;
    bool     b;
    uint32_t raw;
} stack_value_t;

typedef struct {
    instruction_t  *code;           // Pointer to instruction block (flash-mapped)
    uint16_t        code_len;       // Number of instructions
    uint16_t        pc;             // Program counter
    stack_value_t   stack[16];      // Operand stack
    int8_t          sp;             // Stack pointer (-1 = empty)
    uint32_t        start_tick;     // Execution start time (for deadline check)
    uint16_t        max_exec_ms;    // Deadline from pipeline config
    uint8_t         pipeline_id;    // Which pipeline this VM instance runs
    vm_status_t     status;         // RUNNING, HALTED, ABORTED, YIELDED
} vm_context_t;

vm_status_t vm_execute(vm_context_t *ctx) {
    while (ctx->pc < ctx->code_len) {
        // Deadline check
        if ((xTaskGetTickCount() - ctx->start_tick) > pdMS_TO_TICKS(ctx->max_exec_ms)) {
            ctx->status = VM_ABORTED;
            fault_handler_report(FAULT_DEADLINE_EXCEEDED, ctx->pipeline_id);
            return VM_ABORTED;
        }

        instruction_t *inst = &ctx->code[ctx->pc];
        ctx->pc++;

        switch (inst->opcode) {
            case OP_NOP:
                break;

            case OP_LOAD_VAR:
                if (ctx->sp >= 15) { ctx->status = VM_ABORTED; return VM_ABORTED; }
                ctx->stack[++ctx->sp] = state_store_get(inst->operand_a);
                break;

            case OP_STORE_VAR:
                if (ctx->sp < 0) { ctx->status = VM_ABORTED; return VM_ABORTED; }
                state_store_set(inst->operand_a, ctx->stack[ctx->sp--]);
                break;

            case OP_DRV_READ: {
                if (ctx->sp >= 15) { ctx->status = VM_ABORTED; return VM_ABORTED; }
                sensor_value_t val;
                esp_err_t err = driver_registry_read(inst->operand_a, inst->operand_b, &val);
                if (err != ESP_OK) {
                    fault_handler_report(FAULT_DRIVER_READ, inst->operand_a);
                    ctx->stack[++ctx->sp].f = state_store_get_last_known(inst->operand_a);
                } else {
                    ctx->stack[++ctx->sp].f = val.f;
                }
                break;
            }

            case OP_DRV_WRITE: {
                if (ctx->sp < 0) { ctx->status = VM_ABORTED; return VM_ABORTED; }
                actuator_cmd_t cmd = { .value = ctx->stack[ctx->sp--] };
                esp_err_t err = driver_registry_write(inst->operand_a, &cmd);
                if (err != ESP_OK) {
                    fault_handler_report(FAULT_DRIVER_WRITE, inst->operand_a);
                }
                break;
            }

            case OP_CMP_GT:
                if (ctx->sp < 1) { ctx->status = VM_ABORTED; return VM_ABORTED; }
                ctx->stack[ctx->sp - 1].b = (ctx->stack[ctx->sp - 1].f > ctx->stack[ctx->sp].f);
                ctx->sp--;
                break;

            case OP_JMP_IF:
                if (ctx->sp < 0) { ctx->status = VM_ABORTED; return VM_ABORTED; }
                if (ctx->stack[ctx->sp--].b) {
                    ctx->pc = inst->operand_a;
                }
                break;

            case OP_HALT:
                ctx->status = VM_HALTED;
                return VM_HALTED;

            case OP_ABORT:
                ctx->status = VM_ABORTED;
                fault_handler_report(FAULT_EXPLICIT_ABORT, inst->operand_a);
                return VM_ABORTED;

            // ... all other opcodes follow same pattern
        }

        watchdog_feed_pipeline(ctx->pipeline_id);
    }

    ctx->status = VM_HALTED;
    return VM_HALTED;
}
```
