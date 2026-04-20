# Artifact 10 — End-to-End Data Flow Trace

## Scenario

**User says:** "Turn on fan when temperature > 30°C"

This trace follows the complete data path from the user's finger on the Android app screen to the GPIO pin state change on the ESP32-S3 that powers the fan relay.

---

## Phase 1: User Input → Android App

### Step 1.1 — User types description

```
Location:   NaturalLanguageScreen.kt
Component:  TextField Composable
Action:     User types "Turn on fan when temperature > 30°C"
Data:       String in NaturalLanguageViewModel._uiState.description
```

### Step 1.2 — User taps "Generate"

```
Location:   NaturalLanguageViewModel.onSubmitIntent()
Action:     ViewModel calls ProcessIntentUseCase
Data flow:
  NLBuilderUiState.description = "Turn on fan when temperature > 30°C"
  NLBuilderUiState.selectedDevice.boardSku = "VDYT-S3-R1"
  NLBuilderUiState.selectedDevice.id = "a1b2c3d4-..."
```

### Step 1.3 — App sends HTTP request to backend

```
Location:   LLMRepository.processIntent()
Transport:  WiFi (LAN), HTTP/1.1
Endpoint:   POST http://192.168.1.100:8400/api/llm/intent
Auth:       Authorization: Bearer eyJhbGciOiJIUzI1NiIs... (JWT, 24h expiry)
Headers:    Content-Type: application/json

Wire format (JSON body):
{
  "description": "Turn on fan when temperature > 30°C",
  "board_id": "VDYT-S3-R1",
  "device_id": "a1b2c3d4-5678-9abc-def0-123456789abc"
}

Size: ~150 bytes
```

---

## Phase 2: Backend → LLM (Feasibility Check)

### Step 2.1 — Backend rate limit check

```
Location:   backend/src/api/llm_handler.rs
Action:     RateLimiter.check(user_id)
Result:     Pass (3/10 calls used this minute)
```

### Step 2.2 — Backend loads board descriptor

```
Location:   backend/src/service/llm_service.rs
Query:      SELECT pin_map, default_devices FROM board_skus WHERE sku = 'VDYT-S3-R1'

Result:
{
  "sku": "VDYT-S3-R1",
  "connected_sensors": ["drv_bme280@i2c_0:0x76"],
  "connected_actuators": ["drv_relay@gpio_out0", "drv_fan_pwm@pwm_ch0"]
}
```

### Step 2.3 — Backend calls LLM (Call 1: Feasibility)

```
Location:   backend/src/service/llm_client.rs
Transport:  HTTPS/1.1 (TLS 1.3)
Endpoint:   POST https://openrouter.ai/api/v1/chat/completions
Auth:       Authorization: Bearer sk-or-v1-... (OpenRouter API key)

Wire format:
{
  "model": "mistralai/mixtral-8x7b-instruct",
  "messages": [
    {
      "role": "system",
      "content": "<full system prompt with embedded IR schema, driver registry, and board descriptor>"
    },
    {
      "role": "user",
      "content": "Check feasibility: Turn on fan when temperature > 30°C"
    }
  ],
  "response_format": {"type": "json_object"},
  "temperature": 0.1,
  "max_tokens": 512
}

LLM Response:
{
  "feasible": true,
  "reason": "The board has a BME280 temperature sensor and relay/PWM fan actuator available."
}
```

### Step 2.4 — Feasibility passes → proceed to IR generation

---

## Phase 3: Backend → LLM (IR Generation)

### Step 3.1 — Backend calls LLM (Call 2: IR Generation)

```
Location:   backend/src/service/llm_client.rs
Transport:  HTTPS/1.1 (TLS 1.3)
Endpoint:   POST https://openrouter.ai/api/v1/chat/completions

Wire format:
{
  "model": "mistralai/mixtral-8x7b-instruct",
  "messages": [
    {
      "role": "system",
      "content": "<full system prompt>"
    },
    {
      "role": "user",
      "content": "Generate IR: Turn on fan when temperature > 30°C"
    }
  ],
  "response_format": {"type": "json_object"},
  "temperature": 0.1,
  "max_tokens": 4096
}

LLM Output (raw JSON):
{
  "version": "1.0",
  "program_id": "b2f3a4c5-6d7e-8f9a-0b1c-2d3e4f5a6b7c",
  "board_id": "VDYT-S3-R1",
  "created_at": "2025-01-15T10:30:00Z",
  "signature": "",
  "devices": [
    {
      "id": "temp_sensor",
      "driver": "drv_bme280",
      "bus": "i2c_0",
      "address": "0x76",
      "capabilities": ["temperature"]
    },
    {
      "id": "relay_fan",
      "driver": "drv_relay",
      "bus": "gpio",
      "pin_slot": "gpio_out0",
      "capabilities": ["on_off"]
    }
  ],
  "state": {
    "temperature": {"type": "float", "initial": 0.0}
  },
  "triggers": [
    {"id": "every_5s", "type": "timer", "interval_ms": 5000}
  ],
  "pipelines": [
    {
      "id": "fan_control",
      "trigger": "every_5s",
      "nodes": [
        {"id": "n1", "type": "sensor.read", "device": "temp_sensor", "field": "temperature", "store_to": "temperature"},
        {"id": "n2", "type": "condition.compare", "left": "$temperature", "op": "gt", "right": 30.0, "if_true": "n3", "if_false": null},
        {"id": "n3", "type": "actuator.write", "device": "relay_fan", "value": true}
      ],
      "max_execution_ms": 200
    }
  ],
  "constraints": {
    "max_total_nodes": 256,
    "max_state_variables": 64,
    "max_pipelines": 16
  }
}
```

---

## Phase 4: Backend Validation Pipeline (8 Steps)

### Step 4.1 — JSON Schema validation
```
Input:   LLM output JSON
Action:  Validate against Parakram IR JSON Schema draft-07
Result:  ✓ PASS — all required fields present, types correct
```

### Step 4.2 — Device reference resolution
```
Input:   devices[].id referenced in nodes[].device
Action:  Check "temp_sensor" exists in devices array → YES
         Check "relay_fan" exists in devices array → YES
         Check drivers exist in registry → drv_bme280 ✓, drv_relay ✓
Result:  ✓ PASS
```

### Step 4.3 — State reference resolution
```
Input:   "$temperature" referenced in node n2.left
Action:  Check "temperature" exists in state object → YES
         No dangling references found
Result:  ✓ PASS
```

### Step 4.4 — DAG cycle detection (Kahn's algorithm)
```
Input:   Pipeline "fan_control" node graph: n1→n2→n3, n2→(null)
Action:  Build adjacency list, run topological sort
         n1: in_degree=0
         n2: in_degree=1 (from n1)
         n3: in_degree=1 (from n2, via if_true)
         Topological order: [n1, n2, n3]
         All 3 nodes visited — no cycle
Result:  ✓ PASS — DAG is valid
```

### Step 4.5 — Resource conflict detection
```
Input:   All pipelines writing to actuators
Action:  Only pipeline "fan_control" writes to "relay_fan"
         No conflicting writes from multiple pipelines
Result:  ✓ PASS — no mutex group needed
```

### Step 4.6 — Timing bound verification
```
Input:   Pipeline max_execution_ms=200, trigger interval_ms=5000
Action:  200 ≤ 5000 → OK
Result:  ✓ PASS
```

### Step 4.7 — Driver compatibility check
```
Input:   Node n1 reads "temperature" from "temp_sensor" (drv_bme280)
Action:  drv_bme280 capabilities = [temperature, humidity, pressure, altitude]
         "temperature" ∈ capabilities → OK
         drv_bme280 bus_type = i2c, specified bus = i2c_0 → OK
         Node n3 writes to "relay_fan" (drv_relay)
         drv_relay capabilities = [on_off]
         Write value=true (bool) → compatible with on_off → OK
Result:  ✓ PASS
```

### Step 4.8 — Safety policy check
```
Input:   All driver call frequencies, stack depth
Action:  drv_bme280 min_interval_ms=500, called every 5000ms → OK
         drv_relay min_interval_ms=100, called every 5000ms → OK
         Max stack depth for pipeline: 2 values (sensor read + comparison) ≤ 16 → OK
Result:  ✓ PASS

Validation complete: 8/8 steps passed, 0 errors, 0 warnings
```

---

## Phase 5: Bytecode Compilation

### Step 5.1 — Index assignment

```
Location:   backend/src/compiler/indexer.rs

Driver indices:   { temp_sensor: 0, relay_fan: 1 }
Variable indices: { temperature: 0 }
Field indices:    { temperature: 0 }
Constant pool:    { [0]: float 30.0 }
```

### Step 5.2 — Instruction emission

```
Location:   backend/src/compiler/emitter.rs

Pipeline "fan_control" compiles to 10 instructions:

[0] DRV_READ   drv=0, field=0               → Read temp from BME280
[1] STORE_VAR  var=0                         → Store to state[0] (temperature)
[2] LOAD_VAR   var=0                         → Push temperature
[3] LOAD_CONST const=0                       → Push 30.0
[4] CMP_GT                                   → Compare: temperature > 30.0?
[5] JMP_IF     target=7                      → If true, jump to instruction 7
[6] HALT                                     → False path: end (fan stays as-is)
[7] LOAD_IMM_B value=1                       → Push true
[8] DRV_WRITE  drv=1                         → Write true to relay
[9] HALT                                     → True path: end

Raw bytecode (80 bytes):
60 00 00 00 00 00 00 00
05 00 00 00 00 00 00 00
04 00 00 00 00 00 00 00
06 40 00 00 00 00 00 00
22 00 00 00 00 00 00 00
41 00 07 00 00 00 00 00
43 00 00 00 00 00 00 00
03 00 01 00 00 00 00 00
61 00 01 00 00 00 00 00
43 00 00 00 00 00 00 00
```

### Step 5.3 — Constant pool serialization

```
Constant pool (8 bytes):
[0] type=float, length=4, value=30.0f (IEEE 754: 0x41F00000)
    01 04 00 00 00 00 F0 41
```

### Step 5.4 — Payload signing

```
Location:   backend/src/compiler/signer.rs

Signed content = header (32B) + instructions (80B) + constants (8B) = 120 bytes

Ed25519 signature computed:
  input = sha256(signed_content)  [ring crate handles this internally]
  key = backend_signing_private_key (from OS keychain)
  output = 64-byte Ed25519 signature

Complete payload = 120 + 72 (sig block) = 192 bytes
```

---

## Phase 6: Backend → App Response

```
Location:   backend/src/api/llm_handler.rs
Transport:  HTTP/1.1 (WiFi LAN)
Response:   200 OK

Wire format:
{
  "feasible": true,
  "ir": { ... (full IR JSON) ... },
  "ir_preview": {
    "summary": "Read temperature every 5 seconds. Turn on fan when above 30°C.",
    "triggers": [
      {"description": "Every 5 seconds", "interval": "5s"}
    ],
    "actions": [
      {"condition": "Temperature > 30°C", "action": "Turn on fan relay"}
    ],
    "sensors_used": ["Temperature sensor (BME280)"],
    "actuators_used": ["Fan relay"]
  },
  "validation": {"valid": true, "errors": [], "steps_completed": 8},
  "llm_model": "mistralai/mixtral-8x7b-instruct",
  "generation_time_ms": 2340
}

Size: ~2KB
```

---

## Phase 7: User Confirms → Compilation → Transfer

### Step 7.1 — User sees preview, taps "Deploy"

```
Location:   NaturalLanguageScreen.kt
Action:     User sees plain-English preview:
              "Every 5 seconds: If temperature > 30°C → Turn on fan"
            User taps "Deploy" button
            → NaturalLanguageViewModel.onConfirmDeploy()
```

### Step 7.2 — App requests compilation

```
Transport:  HTTP/1.1
Endpoint:   POST http://192.168.1.100:8400/api/ir/compile
Body:       { "ir": {...}, "device_id": "a1b2c3d4-..." }
Response:   
{
  "bytecode_b64": "UFJLTQEAAAAAAA...",    (base64 of 192 bytes)
  "bytecode_size": 192,
  "bytecode_hash": "a5f3e7b2c4d6...",
  "num_instructions": 10,
  "num_constants": 1,
  "num_pipelines": 1
}
```

### Step 7.3 — App determines transfer method

```
Location:   DeployProjectUseCase
Decision:   Device has WiFi connectivity → use WiFi TCP transfer
            Device IP: 192.168.1.42, Port: 8423
```

### Step 7.4 — WiFi TCP transfer to device

```
Transport:  TCP socket to 192.168.1.42:8423
Protocol:   Parakram Config Protocol (custom framing)

Wire format:
┌────────────────────────────────────────────────┐
│ Magic "PRKM"  │ 0x50 0x52 0x4B 0x4D  (4 bytes)│
│ Version       │ 0x01 0x00             (2 bytes)│
│ Payload length│ 0xC0 0x00 0x00 0x00   (4 bytes)│  = 192
│ Payload       │ [192 bytes of signed bytecode] │
│ CRC32         │ 0xXX 0xXX 0xXX 0xXX  (4 bytes)│
└────────────────────────────────────────────────┘

Total TCP payload: 4 + 2 + 4 + 192 + 4 = 206 bytes
Transfer time at WiFi LAN speed: < 1ms
```

---

## Phase 8: Device Receives, Verifies, Executes

### Step 8.1 — TCP receive

```
Location:   firmware/main/comms/wifi_mgr.c → tcp_config_task()
Action:     TCP server on port 8423 accepts connection
            Reads framed payload (206 bytes)
            Validates CRC32 of payload
            Strips framing → 192 bytes of signed bytecode
```

### Step 8.2 — Signature verification

```
Location:   firmware/main/security/payload_verify.c → payload_verify()

Check 1: Size ≥ 104 (32 header + 72 sig block minimum)    → 192 ≥ 104 ✓
Check 2: Magic = "PRKM"                                   → ✓
Check 3: Version = 1                                       → ✓
Check 4: device_id_hash matches CRC32(our_uuid)           → ✓
Check 5: num_instructions = 10, ≤ 1024                    → ✓
Check 6: Sig block magic = "SIG0"                          → ✓
Check 7: signed_length + 72 = 192 → signed_length = 120   → ✓

Ed25519 verify:
  public_key = backend_pubkey (from eFuse block 6)
  message = payload[0..120]  (header + instructions + constants)
  signature = payload[128..192]  (64 bytes)
  
  mbedtls_ed25519_verify() → SUCCESS ✓

Result: VERIFY_OK
  program_id = b2f3a4c5-6d7e-8f9a-0b1c-2d3e4f5a6b7c
  instructions_offset = 32
  constants_offset = 112
```

### Step 8.3 — Atomic program swap

```
Location:   firmware/main/runtime/vm.c → vm_load_program()

Action sequence:
  1. Scheduler suspends all pipeline tasks
  2. Copy signed bytecode to "program" flash partition (64KB)
  3. Parse header, map instruction block to flash address
  4. Parse constant pool into constant_pool module
  5. Reset all state variables to initial values
  6. Configure triggers in scheduler (register timer: 5000ms)
  7. Create/reuse pipeline FreeRTOS task for "fan_control"
  8. Resume scheduler
  9. Send ACK over TCP: {status: 0x04 (SUCCESS), program_id: b2f3...}
  
Time: ~50ms (flash write + verify)
```

### Step 8.4 — Timer fires (first execution at T+5000ms)

```
Location:   firmware/main/runtime/scheduler.c

Event:      esp_timer callback fires at t=5000ms
Action:     scheduler posts event to event bus:
            {type: TRIGGER_TIMER, pipeline_id: 0, tick: 5000}
            
            Scheduler task resumes pipeline 0's VM task via xTaskNotifyGive()
```

### Step 8.5 — VM executes pipeline "fan_control"

```
Location:   firmware/main/runtime/vm.c → vm_execute()

Pipeline task resumes. VM context:
  pc = 0, sp = -1, start_tick = current_tick

Instruction 0: DRV_READ (opcode=0x60, drv=0, field=0)
  → driver_registry_read(0, 0, &val)
  → drv_bme280.read(handle, CAP_TEMPERATURE, &val)
  → I2C burst read: i2c_bus_read(0, 0x76, 0xF7, raw, 8, 50)
    → I2C master sends: START, 0x76<<1|W, 0xF7, REPEATED_START, 0x76<<1|R, read 8 bytes, STOP
    → I2C wire: SDA/SCL signals on GPIO 8/9 at 400kHz
    → raw bytes: press[3], temp[3], hum[2]
    → Compensation: bme280_compensate_temperature(adc_T) = 31.2°C
  → val = {type: FLOAT, f: 31.2, capability: TEMPERATURE, error: OK}
  → stack: [31.2]               sp=0

Instruction 1: STORE_VAR (opcode=0x05, var=0)
  → state_store_set(0, {f: 31.2})
  → state_pool[0].f = 31.2
  → stack: []                   sp=-1

Instruction 2: LOAD_VAR (opcode=0x04, var=0)
  → state_store_get(0) = {f: 31.2}
  → stack: [31.2]               sp=0

Instruction 3: LOAD_CONST (opcode=0x06, const=0)
  → constant_pool_get(0) = {f: 30.0}
  → stack: [31.2, 30.0]         sp=1

Instruction 4: CMP_GT (opcode=0x22)
  → 31.2 > 30.0 = TRUE
  → stack: [true]               sp=0

Instruction 5: JMP_IF (opcode=0x41, target=7)
  → pop TOS = true → JUMP to pc=7
  → stack: []                   sp=-1

Instruction 7: LOAD_IMM_B (opcode=0x03, value=1)
  → stack: [true]               sp=0

Instruction 8: DRV_WRITE (opcode=0x61, drv=1)
  → pop TOS = true
  → driver_registry_write(1, &cmd)
  → cmd = {type: BOOL, b: true, capability: ON_OFF}
  → drv_relay.write(handle, &cmd)
  
  ┌─────────────────────────────────────────────┐
  │     THIS IS WHERE THE PIN STATE CHANGES     │
  │                                              │
  │  relay_set_physical(true)                    │
  │    inverted = false → physical_level = true  │
  │    gpio_hal_set_level(GPIO_NUM_38, 1)        │
  │      → gpio_set_level(38, 1)                 │
  │        → ESP32-S3 GPIO matrix write          │
  │          → GPIO 38 output register bit SET   │
  │            → Pin voltage: 0V → 3.3V          │ ← ELECTRICAL CHANGE
  │              → Relay coil energizes           │
  │                → Relay contact CLOSES         │
  │                  → Fan power circuit ON       │ ← FAN SPINS UP
  └─────────────────────────────────────────────┘
  
  → stack: []                   sp=-1

Instruction 9: HALT (opcode=0x43)
  → ctx->status = VM_HALTED
  → Pipeline execution complete
  → Execution time: ~3ms (well within 200ms deadline)
  → watchdog_feed_pipeline(0)
```

### Step 8.6 — Telemetry notification

```
Location:   firmware/main/comms/ble_gatt_profile.c

After pipeline execution:
  → Event bus publishes: {type: PIPELINE_COMPLETE, pipeline_id: 0, values: {temperature: 31.2}}
  → BLE telemetry task receives event
  → Encodes notification payload:
    [pipeline_id: 0x00] [tick: 5000 as uint32 LE] [num_values: 0x01]
    [var_index: 0x00] [type: 0x01 (float)] [value: 31.2 as IEEE754 LE]
    
    Bytes: 00 88 13 00 00 01 00 01 9A 99 F9 41
    
  → BLE GATT notify on Pipeline 0 Data characteristic
    UUID: F47AC10B-58CC-4372-A567-2000B2C3D480
```

### Step 8.7 — App receives telemetry, updates dashboard

```
Location:   BleTelemetryReceiver.kt → onCharacteristicChanged()
  → Parses notification: pipeline=0, tick=5000, temperature=31.2
  → TelemetryRepository.onTelemetryReceived(frame)
  → LiveDashboardViewModel observes TelemetryRepository.telemetryFlow
  → UI State updates:
    - Temperature gauge: 31.2°C (needle moves, color shifts to warm)
    - Pipeline status: "fan_control" → "Running" badge
    - Chart: new data point appended at x=5000ms
```

---

## Summary: Complete Path

```
User's finger tap
  → Kotlin ViewModel 
    → OkHttp POST (WiFi, HTTP, JSON, JWT)
      → Axum handler (Rust)
        → reqwest POST (HTTPS, JSON, API key)
          → OpenRouter LLM
        ← JSON response (IR draft)
        → 8-step IR validation
        → Bytecode compiler (10 instructions, 80 bytes)
        → Ed25519 signer (ring crate, key from OS keychain)
      ← HTTP 200 (compile result, 192 bytes payload)
    → TCP socket (WiFi, custom framing, 206 bytes)
      → ESP32-S3 wifi_mgr.c
        → payload_verify.c (Ed25519 verify, eFuse pubkey)
        → vm_load_program() (flash write, task setup)
        → scheduler starts timer (5000ms)
          → vm_execute() (10 instructions, ~3ms)
            → drv_bme280.c: I2C read (SDA=GPIO8, SCL=GPIO9)
              ← 31.2°C
            → CMP_GT: 31.2 > 30.0 = true
            → drv_relay.c: GPIO write
              → gpio_set_level(GPIO 38, HIGH)
                → Pin 38: 0V → 3.3V               ← VOLTAGE CHANGE
                  → Relay coil energizes
                    → Fan power contact closes
                      → FAN TURNS ON                ← REAL-WORLD EFFECT

Total transformations:    14
System boundaries crossed: 7
  (App→Backend, Backend→LLM, LLM→Backend, Backend→App, App→Device, Device→App, Device→Hardware)
Validations performed:    10
  (Rate limit, JSON parse, 8-step IR validation, signature verify)
Protocols used:           5
  (HTTP/JSON, HTTPS/JSON, TCP/custom, BLE/GATT, I2C)
Crypto operations:        2
  (Ed25519 sign on backend, Ed25519 verify on device)
Time from "Deploy" to fan spinning: ~6 seconds
  (Compile: ~100ms, Transfer: ~1ms, Verify: ~50ms, Program swap: ~50ms, First timer: 5000ms, Execution: ~3ms)
```
