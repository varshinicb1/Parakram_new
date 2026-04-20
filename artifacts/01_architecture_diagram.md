# Artifact 1 — Architecture Diagram

## Parakram System Architecture — Complete

---

## 1. High-Level System Topology

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              USER DOMAIN                                    │
│                                                                             │
│   ┌───────────────────────────┐         ┌────────────────────────────────┐  │
│   │   Android App (Parakram)  │◄───────►│   Laptop Backend (Rust/Axum)  │  │
│   │                           │  WiFi   │                                │  │
│   │  • Jetpack Compose UI     │  HTTP/  │  • IR Validator                │  │
│   │  • BLE GATT Client        │  WS     │  • Bytecode Compiler           │  │
│   │  • WiFi Deployment        │  JSON   │  • LLM Interface               │  │
│   │  • Telemetry Dashboard    │  JWT    │  • Device Registry (SQLite)    │  │
│   │                           │         │  • Crypto Signer (Ed25519)     │  │
│   └───────────┬───────────────┘         └──────────┬─────────────────────┘  │
│               │                                     │                       │
│               │ BLE GATT                            │ HTTPS (reqwest)       │
│               │ (config push,                       │ JSON                  │
│               │  telemetry notify)                  │ API Key               │
│               │                                     │                       │
│   ┌───────────▼───────────────┐         ┌──────────▼─────────────────────┐  │
│   │   ESP32-S3 Device         │         │   OpenRouter LLM API           │  │
│   │                           │         │                                │  │
│   │  • Bytecode VM            │         │  • Mixtral-8x7B / Gemma-2-9B   │  │
│   │  • Driver Layer           │         │  • Structured JSON output      │  │
│   │  • Event Bus              │         │  • Feasibility + IR Gen        │  │
│   │  • Safety Layer           │         │                                │  │
│   │  • Secure Boot V2         │         └────────────────────────────────┘  │
│   │  • Flash Encryption       │                                             │
│   └───────────────────────────┘                                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Detailed Layer Interaction Diagram

```mermaid
graph TB
    subgraph "Layer 6 — Android App"
        UI["Jetpack Compose UI"]
        BLE_C["BLE GATT Client"]
        HTTP_C["OkHttp Client"]
        VM_APP["ViewModel Layer"]
    end

    subgraph "Layer 4 — Local Backend"
        API["Axum HTTP/WS Server"]
        VAL["IR Validator<br/>(8-step pipeline)"]
        COMP["Bytecode Compiler"]
        SIGN["Ed25519 Signer<br/>(ring crate)"]
        DB["SQLite<br/>(sqlx async)"]
        LLM_C["LLM Client<br/>(reqwest)"]
    end

    subgraph "Layer 5 — LLM"
        LLM["OpenRouter API<br/>Mixtral/Gemma"]
    end

    subgraph "Layer 1 — ESP32-S3 Runtime"
        BLE_S["BLE GATT Server"]
        WIFI_S["WiFi STA + TCP"]
        VM["Bytecode VM<br/>(stack machine)"]
        SCHED["Scheduler<br/>(FreeRTOS tasks)"]
        EBUS["Event Bus<br/>(ring buffer)"]
        DRV["Driver Layer<br/>(static dispatch)"]
        SEC["Security Layer<br/>(Ed25519 verify)"]
        SAFE["Safety Layer<br/>(WDT + rate limit)"]
    end

    subgraph "Layer 3 — Bytecode"
        BC["Signed Bytecode<br/>(8-byte instructions)"]
    end

    subgraph "Layer 2 — IR"
        IR["IR JSON<br/>(DAG of nodes)"]
    end

    %% App → Backend
    UI -->|"User NL input"| VM_APP
    VM_APP -->|"HTTP POST /api/llm/intent<br/>JSON, JWT auth"| HTTP_C
    HTTP_C -->|"WiFi, HTTP/1.1<br/>JSON body, Bearer token"| API

    %% Backend → LLM
    API --> LLM_C
    LLM_C -->|"HTTPS POST<br/>JSON, API key in header"| LLM
    LLM -->|"HTTPS 200<br/>IR JSON body"| LLM_C
    LLM_C --> VAL

    %% Backend internal
    API --> VAL
    VAL -->|"Validated IR"| COMP
    COMP -->|"Raw bytecode"| SIGN
    SIGN -->|"Signed payload"| API
    API --> DB

    %% App ← Backend
    API -->|"HTTP 200<br/>Signed bytecode (binary)<br/>Content-Type: application/octet-stream"| HTTP_C
    HTTP_C --> VM_APP

    %% App → Device (WiFi path)
    VM_APP -->|"TCP socket<br/>Signed bytecode payload<br/>Framed: [len:4][payload][checksum:4]"| WIFI_S

    %% App → Device (BLE path)
    VM_APP -->|"BLE GATT Write<br/>MTU 512B chunks<br/>[seq:2][chunk:510]"| BLE_C
    BLE_C -->|"BLE 5.0, GATT<br/>Config Service<br/>Chunked transfer"| BLE_S

    %% Device internal
    BLE_S --> SEC
    WIFI_S --> SEC
    SEC -->|"Signature verified"| VM
    VM --> SCHED
    SCHED --> EBUS
    EBUS --> DRV
    SAFE -.->|"monitors"| VM
    SAFE -.->|"monitors"| DRV

    %% Telemetry (Device → App)
    DRV -->|"sensor data"| EBUS
    EBUS -->|"BLE GATT Notify<br/>Telemetry Service<br/>[pipeline_id:1][values:variable]"| BLE_S
    BLE_S -->|"BLE notify"| BLE_C
    BLE_C --> VM_APP
    VM_APP --> UI

    %% IR/Bytecode flow
    IR -.->|"validated by"| VAL
    IR -.->|"compiled to"| COMP
    COMP -.->|"produces"| BC
    BC -.->|"executed by"| VM
```

---

## 3. Data Flow Annotations — Every Arrow

### 3.1 Android App → Local Backend

| Path | Protocol | Direction | Data Format | Auth |
|------|----------|-----------|-------------|------|
| NL Intent | HTTP POST `/api/llm/intent` | App → Backend | `{"description": "...", "board_id": "..."}` JSON | JWT Bearer token (24h expiry) |
| IR Validate | HTTP POST `/api/ir/validate` | App → Backend | IR JSON body | JWT Bearer token |
| IR Compile | HTTP POST `/api/ir/compile` | App → Backend | IR JSON body | JWT Bearer token |
| Deploy | HTTP POST `/api/ir/deploy/:device_id` | App → Backend | `{"bytecode_b64": "...", "device_id": "..."}` | JWT Bearer token |
| Project CRUD | HTTP GET/POST `/api/projects` | Bidirectional | Project JSON | JWT Bearer token |
| Device List | HTTP GET `/api/devices` | Backend → App | Device JSON array | JWT Bearer token |
| Telemetry Stream | WebSocket `/api/devices/:id/telemetry` | Backend → App | JSON frames: `{"ts": ..., "pipeline": "...", "values": {...}}` | JWT on upgrade |

### 3.2 Local Backend → OpenRouter LLM

| Path | Protocol | Direction | Data Format | Auth |
|------|----------|-----------|-------------|------|
| Feasibility Check | HTTPS POST `openrouter.ai/api/v1/chat/completions` | Backend → LLM | `{"messages": [...], "response_format": {"type": "json_object"}}` | `Authorization: Bearer <api_key>` |
| IR Generation | HTTPS POST (same) | Backend → LLM | Same structure, augmented prompt | Same |
| Response | HTTPS 200 | LLM → Backend | `{"choices": [{"message": {"content": "<IR JSON>"}}]}` | N/A |

### 3.3 Android App → ESP32-S3 Device

| Path | Protocol | Direction | Data Format | Auth |
|------|----------|-----------|-------------|------|
| BLE Discovery | BLE 5.0 Advertising | Device → App | Advertising packet: manufacturer data with Vidyuthlabs ID (0xVDYT) | None (discovery) |
| BLE Pairing | BLE GATT Connect | Bidirectional | GATT Service Discovery | BLE Bonding (LE Secure Connections, numeric comparison) |
| Config Push (BLE) | BLE GATT Write (Config Service) | App → Device | Chunked: `[seq:2B][total_chunks:2B][payload_chunk:508B]` | Payload is Ed25519-signed bytecode |
| Config Push (WiFi) | TCP socket, port 8423 | App → Device | Framed: `[magic:4B "PRKM"][version:2B][length:4B][payload][crc32:4B]` | Payload is Ed25519-signed bytecode |
| Config ACK | BLE GATT Notify / TCP response | Device → App | `[status:1B][error_code:2B][program_id:16B]` | Implicit (bonded/connected) |
| Telemetry | BLE GATT Notify (Telemetry Service) | Device → App | `[pipeline_id:1B][tick:4B][num_values:1B][value_entries:variable]` | Implicit (bonded) |
| Status | BLE GATT Read (Status Service) | App → Device (read) | `[state:1B][uptime_s:4B][fw_ver:4B][error_count:2B][active_pipelines:1B]` | Implicit (bonded) |

### 3.4 Device Internal Data Flow

| Path | Mechanism | Data Format |
|------|-----------|-------------|
| Bytecode → VM | Direct memory read (flash-mapped) | 8-byte instructions, sequential |
| VM → Driver | Static dispatch table call | `driver_vtable_t.read()` / `.write()` with typed values |
| Driver → Event Bus | `event_bus_publish()` | Ring buffer entry: `[event_type:1B][source_id:1B][timestamp:4B][value:8B]` |
| Event Bus → Scheduler | `event_bus_subscribe()` callback | Same ring buffer entry |
| Scheduler → VM | Task resume (FreeRTOS `xTaskNotify`) | Pipeline index as notification value |
| VM → State Store | `state_store_set()` | Index + typed value union |
| Safety → VM | Watchdog interrupt / abort injection | `ABORT` opcode injected at current PC |
| Comms → MQTT | `mqtt_client_publish()` | Topic string (const pool) + payload bytes |
| Comms → SD | `log_sd_write()` | CSV row: `timestamp,pipeline_id,field1,field2,...` |

---

## 4. Security Boundary Diagram

```
┌────────────────────────────────────────────────────────────┐
│                    TRUST BOUNDARY 1                         │
│              (Laptop — physically controlled)               │
│                                                             │
│    ┌──────────────────┐      ┌─────────────────────┐       │
│    │   Android App    │      │   Local Backend      │       │
│    │                  │◄────►│                      │       │
│    │  Stores: JWT     │ WiFi │  Stores:             │       │
│    │  No secrets      │ LAN  │  • Backend signing   │       │
│    │                  │      │    key (OS keychain)  │       │
│    └────────┬─────────┘      │  • Device pubkeys    │       │
│             │                │  • User sessions     │       │
│             │ BLE            └─────────┬────────────┘       │
│             │                          │ HTTPS              │
│ ┌───────────▼────────────┐             │                    │
│ │    TRUST BOUNDARY 2    │    ┌────────▼────────────┐       │
│ │  (Device — tamper-     │    │  TRUST BOUNDARY 3   │       │
│ │   resistant via        │    │  (External — LLM    │       │
│ │   secure boot)         │    │   API, untrusted    │       │
│ │                        │    │   output)            │       │
│ │  Stores:               │    │                     │       │
│ │  • Device keypair      │    │  LLM output is      │       │
│ │    (eFuse, read-only)  │    │  ALWAYS validated    │       │
│ │  • Backend verify key  │    │  before use          │       │
│ │    (eFuse, read-only)  │    └─────────────────────┘       │
│ │  • Bound user_id       │                                  │
│ │    (encrypted NVS)     │                                  │
│ └────────────────────────┘                                  │
└────────────────────────────────────────────────────────────┘
```

---

## 5. Protocol Stack Summary

```
┌──────────────────────────────────────────────────────┐
│ Application    │ IR JSON │ Bytecode │ Telemetry JSON │
├──────────────────────────────────────────────────────┤
│ Presentation   │ JSON    │ Binary   │ JSON           │
├──────────────────────────────────────────────────────┤
│ Session        │ JWT     │ Ed25519  │ JWT/BLE Bond   │
├──────────────────────────────────────────────────────┤
│ Transport      │ HTTP    │ TCP/GATT │ WS/GATT        │
├──────────────────────────────────────────────────────┤
│ Network        │ WiFi    │ WiFi/BLE │ WiFi/BLE       │
├──────────────────────────────────────────────────────┤
│ Physical       │ 802.11  │ 802.11   │ 802.15.1       │
│                │  n/ac   │  /BLE5   │  BLE5          │
└──────────────────────────────────────────────────────┘
```

---

## 6. Deployment Sequence Diagram

```mermaid
sequenceDiagram
    participant User
    participant App as Android App
    participant Backend as Local Backend
    participant LLM as OpenRouter LLM
    participant Device as ESP32-S3

    User->>App: "Turn on fan when temp > 30°C"
    App->>Backend: POST /api/llm/intent<br/>{description, board_id}<br/>JWT auth

    Backend->>LLM: POST /chat/completions<br/>Feasibility check prompt<br/>API key auth
    LLM-->>Backend: {feasible: true}

    Backend->>LLM: POST /chat/completions<br/>IR generation prompt<br/>API key auth
    LLM-->>Backend: IR JSON draft

    Backend->>Backend: 8-step validation pipeline
    Backend->>Backend: Bytecode compilation
    Backend->>Backend: Ed25519 sign(bytecode)

    Backend-->>App: 200 OK<br/>{ir_preview, bytecode_b64, signature}

    App->>App: Show IR preview in plain English
    User->>App: Confirm deploy

    alt WiFi available
        App->>Device: TCP:8423<br/>[PRKM][v1][len][signed_bytecode][crc32]
    else BLE only
        App->>Device: GATT Write (Config Service)<br/>[seq][total][chunk] × N
    end

    Device->>Device: Reassemble payload
    Device->>Device: Ed25519 verify(backend_pubkey, bytecode)

    alt Signature valid
        Device->>Device: Atomic program swap
        Device-->>App: ACK {status: OK, program_id}
        App-->>User: "Deployed successfully ✓"
    else Signature invalid
        Device-->>App: NACK {status: REJECTED, error: INVALID_SIGNATURE}
        App-->>User: "Deployment failed: security check failed"
    end

    loop Every trigger interval
        Device->>Device: VM executes pipeline
        Device-->>App: BLE Notify (Telemetry)<br/>[pipeline_id][tick][values]
        App-->>User: Live dashboard update
    end
```

---

## 7. Component Dependency Map

```mermaid
graph LR
    subgraph "Build Dependencies"
        IR_SCHEMA["IR JSON Schema<br/>(Artifact 3)"]
        ISA["Bytecode ISA<br/>(Artifact 4)"]
        DRIVER_ABI["Driver ABI<br/>(Artifact 5)"]
    end

    subgraph "Implementations"
        FW["Firmware<br/>(Artifact 2)"]
        BACKEND["Backend API<br/>(Artifact 6)"]
        LLM_PROMPT["LLM Prompt<br/>(Artifact 7)"]
        ANDROID["Android App<br/>(Artifact 8)"]
    end

    subgraph "Cross-cutting"
        SECURITY["Security<br/>(Artifact 9)"]
        E2E["E2E Trace<br/>(Artifact 10)"]
    end

    IR_SCHEMA --> BACKEND
    IR_SCHEMA --> LLM_PROMPT
    IR_SCHEMA --> FW
    ISA --> FW
    ISA --> BACKEND
    DRIVER_ABI --> FW
    DRIVER_ABI --> IR_SCHEMA
    SECURITY --> FW
    SECURITY --> BACKEND
    SECURITY --> ANDROID
    E2E --> FW
    E2E --> BACKEND
    E2E --> ANDROID
```
