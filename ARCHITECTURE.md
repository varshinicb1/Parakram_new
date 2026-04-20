# 🪐 Parakram Zero-Code Platform Architecture

This document provides a highly technical, deep-dive map into the Parakram hardware-software ecosystem. It is intended for collaborating developers (and internal AI systems) to immediately understand what powers the abstraction logic that translates natural language into direct ESP32-S3 microcontroller hardware execution.

---

## 🏗️ 1. High-Level Blueprint

At its core, **Parakram** lets high-paying commercial users configure complex embedded applications (e.g., HVAC automation, drone telemetry) exclusively via mobile texting. 

**The Pipeline:**
1. User types command in **Android App**.
2. **Rust Backend** forwards the English array to highly-tuned custom LLM prompts via OpenRouter (Mistral 8x7B/Claude).
3. The LLM generates a strictly formatted JSON logic map representing the AST (Abstract Syntax Tree), known as the **Intermediate Representation (IR)**.
4. The Backend structurally and mathematically validates the IR, compiles it into heavily compressed **byte-instructions**, and signs it cryptographically.
5. The **ESP32-S3** board receives the bytecode over Bluetooth (BLE) or Wi-Fi MQTT and executes the binary payloads autonomously using its custom C-Runtime Virtual Machine.

---

## 💻 2. The Cloud Backend (Rust)
The backend is the brain of the translation operation. It runs a blazing-fast `axum` web server.

### Critical Directories:
- `backend/src/llm/`
  - **`prompt.rs`**: Contains the enormous, hyper-specialized pseudo-code dictionary we use to constrain the LLM. *If you add new hardware sensors, you MUST register them here so the LLM knows they exist.*
  - **`client.rs`**: Orchestrates `serde_json` to forcefully extract LLM output. It is heavily fortified with regex to strip out conversational markdown that AI models hallucinate. 
- `backend/src/ir/`
  - **`types.rs`**: Houses the strict data schemas for things like `IRConstraints` and `IRNode`. It utilizes powerful `#[serde(default)]` and custom deserializers (`deserialize_optional_string`) to elegantly catch formatting mistakes if the LLM hallucinates an integer instead of a string.
  - **`validator.rs`**: Executes `Kahn's Algorithm` to ensure the logic graph the LLM produced is acyclic and variable references aren't mismatched.
- `backend/src/compiler/`
  - **`emitter.rs`**: Maps the parsed JSON Nodes into strict binary `Instruction` ops (e.g., `$temp_sensor.temperature` becomes `OP_DRV_READ`). *Contains robust fallback handlers if `load_from` objects don't spawn correctly.*

### Deployment Infrastructure:
- **`docker-compose.yml` & `backend/Dockerfile`**: Encapsulates the Rust compiler for the cloud, ensuring total parity. 
- **`nginx/nginx.conf`**: The production Reverse Proxy. Crucial for scaling—it enforces 10r/s `limit_req` rate-locking against the `/api/llm/compile` endpoint so bots cannot bankrupt our API key pipelines.

---

## ⚡ 3. The Firmware (ESP32-S3 / C / FreeRTOS)
The firmware acts as the runtime emulator that processes the backend bytecode payload. 

### Critical Fixes & Architecture:
- **`firmware/sdkconfig.defaults`**: 
  - **PSRAM Offloading:** The `wpa_supplicant` networking stack and BLE `nimBLE` contexts must be heavily offloaded to PSRAM. Default configurations will cause the board to panic and crash during hardware startup (`LoadProhibited` internal DRAM fragmentation) if you attempt to launch Wi-Fi and Bluetooth stacks concurrently.
- **`firmware/main/app_main.c`**:
  - The board prioritizes BLE initialization over WiFi, serving as a Fail-Safe. If network buffers run dry, BLE boots up cleanly so we never lose commissioning access to the device.
  - Registers the execution pipeline gracefully into the hardware Task Watchdog Timer (`twdt`). *If making complex changes to infinite evaluation loops, you must occasionally feed the watchdog (`esp_task_wdt_reset()`) to avoid safety resets.*

### Production Manufacturing:
- **`external_tools/mass_flash.sh`**: A bash threading script that cycles across mapped `/dev/ttyUSB*` ports to flash 50+ VDYT-S3-R1 boards in parallel natively across factory USB hubs.

---

## 📱 4. Mobile & Clients (Android)
- **`android/app/src/main/java.../NetworkModule.kt`**: Connects Retrofit HTTP clients to the production backend `https://api.parakram.com/` cleanly wrapped in singletons.

---

### 🗺️ Mental Checklist For Editing Flow
If an AI agent needs to add a new sensor capability (e.g., "Air Quality Sensor") to this zero-code matrix, follow this loop:
1. Update `llm/prompt.rs` so the AI knows the device exists.
2. Update the C-drivers in firmware so the board can read the module (`OP_DRV_READ`).
3. Add any specific semantic logic to `compiler/emitter.rs` if the hardware requires distinct binary opcodes.
