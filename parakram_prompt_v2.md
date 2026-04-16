# SYSTEM DESIGN PROMPT — Parakram by Vidyuthlabs
# Vibe Code Hardware
# Version 2.0 — Production Grade

---

## ROLE

You are a senior systems architect with deep expertise in:
- Embedded systems (ESP-IDF, FreeRTOS, real-time deterministic runtimes)
- Compiler design (bytecode VMs, IRs, instruction set architecture)
- Android native development (Kotlin, BLE, WiFi)
- Backend systems (Rust, WebSocket, SQLite)
- Hardware security (secure boot, signed payloads, key exchange)

You are NOT a prototype builder. Every decision you make must be defensible for a production system
targeting 72+ hours continuous uptime with zero user-facing crashes.

---

## PRODUCT DEFINITION

**Parakram** is a zero-code hardware programming platform.

The user experience is:
1. Plug in a Vidyuthlabs ESP32-S3 board + sensor shield
2. Open the Android app
3. Describe what they want in plain language OR select from templates
4. System validates feasibility, generates configuration, deploys to device
5. Device executes the behavior — immediately, without any firmware rebuild

The device never runs user-supplied code. It runs a fixed, pre-compiled runtime that
interprets a validated, signed configuration (IR compiled to bytecode).

---

## ABSOLUTE CONSTRAINTS

These are non-negotiable. Violating any of them is a design failure.

| # | Constraint | Why |
|---|---|---|
| C1 | Firmware is fixed — no per-user rebuild | Determinism, security, support cost |
| C2 | No dynamic heap allocation in the execution path | Zero fragmentation, bounded runtime |
| C3 | All instruction counts are bounded at compile time | Prevents infinite loops, guarantees timing |
| C4 | Only Vidyuthlabs-registered drivers run on device | No untrusted hardware abstraction |
| C5 | Only predefined safe pins — no user pin selection | Hardware safety, short-circuit prevention |
| C6 | Device accepts ONLY cryptographically signed IR payloads | Prevents tampering |
| C7 | All stacks must be free and open-source | Licensing, auditability |
| C8 | System must run on a laptop (no cloud dependency) | Local-first, works offline |
| C9 | LLM must never generate code or invent hardware | Correctness boundary |
| C10 | 72+ hour uptime target with watchdog recovery | Industrial reliability standard |

---

## SYSTEM LAYERS

```
┌─────────────────────────────────────┐
│         Android App (Parakram)      │  User-facing
├─────────────────────────────────────┤
│         Local Backend (Rust)        │  Laptop-resident
│   IR Validator · Bytecode Compiler  │
│   LLM Interface · Device Registry  │
├─────────────────────────────────────┤
│     ESP32-S3 Runtime (C + Rust)     │  On-device
│   Bytecode VM · Driver Layer        │
│   Event Bus · Safety Layer          │
└─────────────────────────────────────┘
```

Each layer is specified below with explicit contracts between them.

---

## LAYER 1 — ESP32-S3 RUNTIME

### Hardware target
- SoC: ESP32-S3
- Flash: 16MB (partitioned: firmware | OTA | config | data)
- PSRAM: 8MB (used only for buffers, never for execution state)
- SDK: ESP-IDF 5.x (mandatory, no Arduino HAL)
- RTOS: FreeRTOS (tasks pre-allocated at boot, no dynamic task creation at runtime)

### Memory model (STRICT)
- All runtime state lives in statically allocated pools
- Pool sizes are compile-time constants
- No `malloc`, `new`, or `pvPortMalloc` in any execution path
- PSRAM used only for: WiFi TX/RX buffers, SD write buffer, BLE GATT cache
- All other state in internal SRAM with explicit size budgets

### Module structure (produce full folder tree with file-level responsibilities)

```
firmware/
├── main/
│   ├── runtime/
│   │   ├── vm.c / vm.h          # Bytecode fetch-decode-execute loop
│   │   ├── scheduler.c          # Fixed-interval task dispatcher
│   │   ├── event_bus.c          # Static ring-buffer pub/sub
│   │   └── state_store.c        # Typed variable pool (no strings at runtime)
│   ├── drivers/
│   │   ├── driver_registry.c    # Static dispatch table, populated at compile time
│   │   ├── i2c_bus.c            # Shared I2C bus with per-device locking
│   │   ├── spi_bus.c
│   │   ├── adc_hal.c
│   │   ├── pwm_hal.c
│   │   ├── uart_hal.c
│   │   ├── sensors/             # One file per sensor family
│   │   │   ├── drv_dht22.c
│   │   │   ├── drv_bme280.c
│   │   │   ├── drv_mpu6050.c
│   │   │   └── ...              # All 30+ sensors, same ABI
│   │   └── actuators/
│   │       ├── drv_relay.c
│   │       ├── drv_servo.c
│   │       ├── drv_ws2812.c
│   │       └── ...
│   ├── comms/
│   │   ├── wifi_mgr.c           # STA mode, reconnect FSM
│   │   ├── mqtt_client.c        # MQTT 3.1.1 over TLS
│   │   ├── ble_mgr.c            # GATT server: config + telemetry services
│   │   ├── lora_mgr.c           # SX127x via SPI
│   │   └── esp_now_mgr.c
│   ├── security/
│   │   ├── payload_verify.c     # Ed25519 signature check on incoming bytecode
│   │   ├── secure_storage.c     # NVS with flash encryption
│   │   └── device_identity.c    # Device keypair, bound user ID
│   └── safety/
│       ├── watchdog.c           # Two-level: task WDT + system WDT
│       ├── rate_limiter.c       # Per-driver max call frequency
│       └── fault_handler.c      # Panic → log → safe state → reset
```

### Driver ABI (ALL drivers must conform exactly)

```c
typedef struct {
    esp_err_t (*init)(const driver_config_t *cfg);
    esp_err_t (*read)(driver_handle_t h, sensor_value_t *out);
    esp_err_t (*write)(driver_handle_t h, const actuator_cmd_t *cmd);
    esp_err_t (*deinit)(driver_handle_t h);
    const driver_meta_t *meta;   // name, version, capability flags
} driver_vtable_t;
```

Rules:
- `read()` and `write()` must return within their declared `max_latency_us`
- Must never block — use DMA or pre-staged buffers
- Must declare resource usage: which bus, which pins, which interrupt lines
- Must declare failure modes and the safe fallback value for each

---

## LAYER 2 — INTERMEDIATE REPRESENTATION (IR)

The IR is the contract between the backend compiler and the device runtime.
It is a JSON document that describes a complete program as a directed acyclic graph of nodes.

### Design principles
- IR is **declarative**, not imperative — it describes what should happen, not how
- IR has no loops, no recursion, no unbounded iteration
- IR is **fully bounded**: max nodes, max edges, max state variables all have fixed limits
- IR is **device-agnostic within the Vidyuthlabs ecosystem** — the backend resolves physical pins; IR uses logical device IDs

### IR JSON Schema (complete — produce full JSON Schema draft-07 document)

Top-level keys:

```json
{
  "$schema": "https://parakram.vidyuthlabs.com/ir/v1/schema.json",
  "version": "1.0",
  "program_id": "<uuid>",
  "board_id": "<vidyuthlabs_board_sku>",
  "created_at": "<iso8601>",
  "signature": "<ed25519_b64>",

  "devices": [ ... ],       // Physical hardware on this board (from board descriptor)
  "state": { ... },         // Named typed variables (int, float, bool, string[32])
  "triggers": [ ... ],      // Events that activate pipelines
  "pipelines": [ ... ],     // Ordered node sequences (DAG, not graph)
  "constraints": { ... }    // Timing, resource, and safety bounds
}
```

#### `devices` array — each entry:
```json
{
  "id": "temp_sensor",           // Logical name used throughout IR
  "driver": "drv_bme280",        // Must exist in Vidyuthlabs driver registry
  "bus": "i2c_0",                // Resolved by backend, not user
  "address": "0x76",             // From board descriptor
  "capabilities": ["temperature", "humidity", "pressure"]
}
```

#### `state` object:
```json
{
  "temperature": { "type": "float", "initial": 0.0 },
  "alert_sent":  { "type": "bool",  "initial": false },
  "log_count":   { "type": "int",   "initial": 0, "max": 10000 }
}
```

#### `triggers` array — supported trigger types:
- `timer` — fixed interval or cron expression (bounded: min 100ms)
- `sensor_threshold` — value crosses a threshold (requires hysteresis)
- `gpio_edge` — rising / falling / both
- `mqtt_message` — topic + optional payload match
- `ble_event` — connect / disconnect / characteristic write
- `time_window` — wall-clock window (requires NTP or RTC device)
- `startup` — runs once on boot (for initialization pipelines)

#### `pipelines` array — each pipeline:
```json
{
  "id": "log_temperature",
  "trigger": "every_30s",
  "nodes": [
    { "id": "n1", "type": "sensor.read",       "device": "temp_sensor", "field": "temperature", "store_to": "temperature" },
    { "id": "n2", "type": "condition.compare",  "left": "$temperature", "op": "gt", "right": 35.0, "if_true": "n3", "if_false": null },
    { "id": "n3", "type": "actuator.write",     "device": "relay_fan", "value": true },
    { "id": "n4", "type": "mqtt.publish",       "topic": "sensor/temp", "payload": "$temperature" },
    { "id": "n5", "type": "storage.log",        "fields": ["temperature"], "destination": "sd_card" }
  ],
  "max_execution_ms": 200      // Hard deadline — VM aborts if exceeded
}
```

#### Validation rules (backend enforces ALL of these before compiling):
1. All `device` references must exist in `devices`
2. All `store_to` and `$variable` references must exist in `state`
3. Pipeline node graph must be a DAG (no cycles)
4. `if_true` / `if_false` targets must be forward references only (no back-edges)
5. Total nodes across all pipelines ≤ 256
6. Total state variables ≤ 64
7. No two pipelines write to the same actuator without declaring a `mutex_group`
8. `max_execution_ms` must be ≤ trigger interval
9. All string values must be ≤ 32 bytes

---

## LAYER 3 — BYTECODE ENGINE

The IR is compiled to a custom bytecode for execution on the device VM.
The VM is a simple stack machine with a fixed-size operand stack.

### Design goals
- Fixed-size instructions: exactly 8 bytes each
- No heap usage — all state in pre-allocated arrays
- Constant-time execution per instruction (no variable-length operands)
- Max program size: 1024 instructions (fits in 8KB)
- Max operand stack depth: 16 values

### Instruction format (8 bytes)

```
[opcode: 1B] [flags: 1B] [operand_a: 2B] [operand_b: 2B] [operand_c: 2B]
```

### Instruction set (produce full ISA table with all opcodes)

Category: Data
- `LOAD_IMM`   — push immediate integer/float literal
- `LOAD_VAR`   — push state variable by index
- `STORE_VAR`  — pop and store to state variable
- `LOAD_CONST` — push from read-only constant pool

Category: Arithmetic / Logic
- `ADD`, `SUB`, `MUL`, `DIV` — typed float/int
- `CMP_EQ`, `CMP_GT`, `CMP_LT`, `CMP_GTE`, `CMP_LTE`
- `AND`, `OR`, `NOT`

Category: Control
- `JMP`        — unconditional jump to instruction index
- `JMP_IF`     — conditional jump (pops bool from stack)
- `HALT`       — normal pipeline end
- `ABORT`      — fault, trigger safe state

Category: I/O
- `DRV_READ`   — invoke driver read(), push result (operand_a = driver index)
- `DRV_WRITE`  — invoke driver write(), pop value
- `BUS_LOCK`   — acquire bus mutex (operand_a = bus index)
- `BUS_UNLOCK` — release bus mutex

Category: Comms
- `MQTT_PUB`   — publish to topic (topic index in constant pool)
- `BLE_NOTIFY` — notify BLE characteristic
- `LOG_SD`     — write to SD log buffer

Category: Time
- `GET_TIME`   — push Unix timestamp (from RTC or NTP)
- `DELAY_MS`   — yield for N ms (non-blocking via scheduler)

### VM execution model
- Each pipeline maps to a FreeRTOS task, pre-allocated at boot
- Tasks are suspended until their trigger fires
- VM executes instructions sequentially, no branching out of pipeline
- Watchdog resets instruction counter if `max_execution_ms` exceeded
- Stack overflow → ABORT instruction injected by safety layer

---

## LAYER 4 — LOCAL BACKEND (LAPTOP)

### Tech stack
- Language: Rust (stable toolchain)
- HTTP/WebSocket: Axum 0.7
- Database: SQLite via sqlx (async)
- Crypto: ring (Ed25519, AES-256-GCM)
- LLM client: reqwest → OpenRouter API

### API surface (produce full OpenAPI 3.1 spec for all endpoints)

Core endpoint groups:
- `POST /api/projects` — create project
- `GET /api/projects/:id` — get project with current IR
- `POST /api/ir/validate` — validate IR JSON, return errors
- `POST /api/ir/compile` — compile IR → bytecode, return binary + signature
- `POST /api/ir/deploy/:device_id` — push signed bytecode to device via BLE or WiFi
- `POST /api/llm/intent` — send NL description, receive IR JSON draft
- `GET /api/devices` — list paired devices with status
- `GET /api/devices/:id/telemetry` — WebSocket stream

### IR validation pipeline (implement in this exact order)
1. JSON Schema validation (jsonschema crate)
2. Device reference resolution (all IDs exist in device registry)
3. State reference resolution (no dangling $variable refs)
4. DAG cycle detection (Kahn's algorithm)
5. Resource conflict detection (mutex groups)
6. Timing bound verification (execution_ms ≤ trigger interval)
7. Driver compatibility check (driver supports declared capabilities)
8. Safety policy check (rate limits, stack size bounds)

Any failure at any step returns a structured error with: step, field path, human-readable message.

### Bytecode compiler pipeline
Input: validated IR JSON
Output: binary blob + Ed25519 signature

Steps:
1. Assign indices to all devices, state variables, constants
2. Emit constant pool (strings, topic names, float literals)
3. For each pipeline: emit instruction sequence
4. Backpatch forward JMP targets
5. Append constant pool
6. Sign: `Ed25519.sign(device_pubkey, sha256(bytecode))`
7. Prepend header: magic bytes, version, device_id, program_id, timestamp, signature

### Device registry (SQLite schema — produce full DDL)

Tables:
- `drivers` — id, name, version, bus_type, capabilities JSON, max_latency_us, resource_requirements JSON
- `board_skus` — sku, name, pin_map JSON, default_devices JSON
- `devices` — device_uuid, sku, user_id, pubkey, firmware_version, bound_at
- `projects` — project_id, user_id, name, ir_json, bytecode_hash, deployed_at
- `telemetry` — device_id, timestamp, pipeline_id, values JSON

---

## LAYER 5 — LLM INTEGRATION

The LLM has one job: take a natural language description and produce a valid IR JSON draft.
It must operate strictly within the constraint envelope provided in its context.

### Model selection
Use OpenRouter with a model that supports structured JSON output reliably.
Recommended: `mistralai/mixtral-8x7b-instruct` or `google/gemma-2-9b-it`
Fallback: any model ≥ 7B with instruction tuning and JSON mode.

### System prompt (produce the COMPLETE system prompt — this is the most critical output)

The system prompt must include:
1. Role definition: "You are a hardware configuration generator for the Parakram platform."
2. The full IR JSON Schema (embedded verbatim)
3. The complete driver registry as a structured list: for each driver — name, capabilities, input/output types, constraints
4. The board descriptor for the currently connected board: logical device IDs, what's available
5. Explicit forbidden outputs (listed with examples):
   - Do NOT invent device IDs not in the board descriptor
   - Do NOT generate pin numbers
   - Do NOT generate code in any language
   - Do NOT add node types not in the schema
   - Do NOT set max_execution_ms to a value greater than the trigger interval
6. Required output format: raw JSON only, no explanation, no markdown fences, no preamble
7. Feasibility check instruction: if the user's idea requires hardware not in the board descriptor,
   return `{"feasible": false, "reason": "..."}` instead of IR

### Two-call pattern (backend implements this)
Call 1 — feasibility check:
- Input: user description + board descriptor
- Output: `{"feasible": true/false, "reason": "...", "clarifications": [...]}`
- If feasible: proceed to call 2
- If not: return error to app with human-readable explanation

Call 2 — IR generation:
- Input: feasibility-confirmed description + board descriptor + IR schema
- Output: IR JSON
- Backend immediately validates output through the full validation pipeline
- If validation fails: retry once with the validation errors appended to the prompt
- If retry fails: return structured error, do not deploy

### LLM output hardening
- Parse LLM output with strict JSON parser — reject any response that isn't valid JSON
- Run full IR validation pipeline on LLM output before returning to client
- Log all LLM inputs/outputs to SQLite for debugging and improvement
- Rate limit LLM calls: max 10 per user per minute

---

## LAYER 6 — ANDROID APP

### Tech stack
- Language: Kotlin (no cross-platform frameworks — native only)
- Min SDK: API 26 (Android 8.0)
- BLE: Android BluetoothLE API + custom GATT profile
- WiFi: WifiManager for provisioning, OkHttp for backend API
- UI: Jetpack Compose

### User-facing abstraction model

The user sees ONLY:
- Sensor names in plain English ("Temperature sensor", "Motion detector")
- Behavior descriptions ("Turn on fan when temperature exceeds 30°C")
- Status indicators (Connected, Running, Error with plain description)
- Telemetry as readable values with units ("28.4°C", "Motion detected")

The user NEVER sees:
- Pin numbers
- I2C addresses
- Protocol names (I2C, SPI, UART, MQTT)
- Variable names
- Bytecode
- IR JSON

### Screen map (produce wireframe descriptions for all screens)

1. **Splash / onboarding** — brand, first-time setup
2. **Device discovery** — BLE scan, auto-detect Vidyuthlabs boards, one-tap pair
3. **Project home** — list of saved projects for this device
4. **Template browser** — categorized starter behaviors (Environment monitoring, Motion alarm, Data logger, etc.)
5. **Natural language builder** — text input → feasibility check → IR preview in plain English → deploy
6. **Project editor** — visual behavior builder (trigger → condition → action cards, no code)
7. **Live dashboard** — real-time telemetry, active pipeline status, error indicators
8. **Device settings** — firmware version, rename device, unpair

### BLE GATT profile (produce full service/characteristic UUID table)

Services:
- **Parakram Config Service** (UUID: define one) — write signed bytecode payload in chunks
- **Parakram Telemetry Service** — notify characteristics per pipeline for live data
- **Parakram Status Service** — device state, error codes, uptime, firmware version

### Deployment flow (implement exactly)
1. App sends IR JSON to local backend via WiFi
2. Backend validates + compiles → returns signed bytecode
3. App checks device is on same WiFi network → prefer WiFi for large payloads
4. If WiFi unavailable → fall back to BLE chunked transfer (MTU 512B chunks with sequence numbers)
5. Device receives all chunks → verifies signature → if valid: swap program atomically → ACK
6. App polls status service for 5s → shows success or error

---

## LAYER 7 — SECURITY MODEL

### Trust hierarchy
```
Vidyuthlabs CA (root, offline)
    └── Backend signing key (online, rotatable)
            └── Signed bytecode payloads (per-deploy)
    └── Device keypair (provisioned at factory)
```

### Implementation requirements

**Firmware level:**
- Enable ESP32-S3 secure boot V2 (RSA-PSS signature on firmware image)
- Enable flash encryption (AES-XTS 256-bit)
- Burn eFuses at factory: device UUID, device pubkey, backend verification pubkey
- Device ONLY executes bytecode signed by the backend signing key
- Payload format: `[header][bytecode][Ed25519 signature]`

**Backend level:**
- Backend signing key stored in OS keychain (never in source, never in DB)
- All API endpoints require session token (JWT, 24h expiry)
- Device-to-backend auth: device presents its device_id + HMAC(device_secret, timestamp)
- Telemetry data encrypted in transit: TLS 1.3 minimum

**Device binding:**
- First pairing: app generates pairing token → backend records `(device_id, user_id, paired_at)`
- Device stores bound user_id in encrypted NVS
- Device rejects configurations signed for a different user_id
- Factory reset clears NVS and requires re-pairing

---

## LAYER 8 — FAILURE HANDLING

Produce a complete fault state machine for each failure class:

| Failure | Detection | Recovery Action | Safe State |
|---|---|---|---|
| WiFi drop | MQTT disconnect callback | Retry with exponential backoff, max 5 min | Continue local execution |
| BLE disconnect | GATT callback | Passive — device continues current program | No change |
| Sensor read fail | `ESP_ERR` from driver | Substitute last known good value, increment error counter | Disable pipeline if 5 consecutive failures |
| SD card fail | Mount error | Log to PSRAM ring buffer, attempt remount every 60s | Continue without logging |
| Watchdog timeout | HW watchdog interrupt | Log fault + current instruction + state snapshot to NVS → reset | Reboot to known state |
| Invalid bytecode signature | Signature check fail | Reject, keep running current program, notify app | No change |
| Stack overflow in VM | Stack depth check | Inject ABORT instruction, log pipeline ID | Disable that pipeline only |
| Power fluctuation | Brownout detector | Save critical state to NVS → reset | Full reboot |
| PSRAM unavailable | Boot check | Reduce buffer sizes, log warning | Degraded mode |

---

## OUTPUT REQUIREMENTS

You must produce the following artifacts. Each must be complete — no stubs, no TODOs,
no "implement this later". If a section is too large for one response, say so and continue
in the next message.

**Artifact 1 — Architecture diagram**
Full system diagram showing all layers, data flows, and protocol boundaries.
Label every arrow with: protocol, direction, data format, and authentication method.

**Artifact 2 — Firmware source tree**
Complete folder + file listing with one-line responsibility description per file.
Include: CMakeLists.txt structure, Kconfig options, partition table.

**Artifact 3 — Complete IR JSON Schema**
Full JSON Schema draft-07 document. Must be machine-validatable.
Include: all node types, all field types, all constraints as schema-level rules where possible.

**Artifact 4 — Bytecode ISA**
Complete instruction table: opcode (hex), mnemonic, operand fields, stack effect, description.
Include: encoding examples for a 3-node sample pipeline.

**Artifact 5 — Driver ABI + two complete sample drivers**
Full C header for the ABI. Complete implementation of `drv_bme280.c` (I2C sensor)
and `drv_relay.c` (GPIO actuator) conforming to the ABI.

**Artifact 6 — Backend API**
Full OpenAPI 3.1 YAML spec. Include: all endpoints, all request/response schemas,
all error codes, authentication scheme.

**Artifact 7 — LLM system prompt**
The complete, verbatim system prompt string that the backend sends to the LLM.
Include a worked example: input description → output IR JSON.

**Artifact 8 — Android architecture**
Package structure, ViewModel/Repository pattern, BLE GATT service UUID table,
deployment state machine (states, transitions, guards).

**Artifact 9 — Security implementation plan**
Step-by-step: factory provisioning procedure, backend key management procedure,
payload signing code (Rust), payload verification code (C on device).

**Artifact 10 — End-to-end data flow trace**
Trace a single user action ("Turn on fan when temperature > 30°C") from
natural language input to hardware pin state change. Include every system
boundary crossed, every transformation applied, every validation performed,
and the exact wire format at each step.

---

## QUALITY BAR

Every design decision must be justified against this checklist:

- [ ] Does it respect all 10 absolute constraints?
- [ ] Is it deterministic? (same input always produces same output)
- [ ] Is it bounded? (no unbounded memory, time, or recursion)
- [ ] Is it recoverable? (defined behavior for every failure mode)
- [ ] Is it auditable? (every state transition is logged)
- [ ] Would it pass a code review at a safety-critical embedded shop?

If any component introduces non-determinism, unbounded behavior, or an unhandled
failure mode — redesign it. Do not note it as a "future improvement".

---

## WHAT NOT TO DO

- Do not suggest phasing or MVPs — design the complete system
- Do not use pseudocode — all code artifacts must be syntactically correct
- Do not use placeholder values like `TODO`, `YOUR_KEY_HERE`, or `/* implement */`
- Do not invent hardware that isn't in the ESP32-S3 datasheet
- Do not use deprecated ESP-IDF APIs (target IDF 5.x)
- Do not use dynamic allocation anywhere in the firmware execution path
- Do not suggest cloud services — the system runs locally on a laptop
