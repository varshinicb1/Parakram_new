# Welcome to Parakram

**Parakram** is an open-source, autonomous hardware compilation engine. It allows developers to deploy full C-based microcontroller firmware and sensor logic through an AI agent directly from the web browser onto real silicon, without compiling native C code.

## How It Works

1. **The Playground**: Provide a plain English prompt summarizing your desired hardware setup (e.g., "turn on the heat lamp when temperature drops below 20C").
2. **The LLM Parser**: The Rust backend communicates with Mistral to generate a hardware-validated IR JSON document conforming strictly to Parakram's schemas.
3. **The Bytecode VM**: The backend compiles that logic into incredibly fast and secure C Bytecode, routing all dependencies to native HAL drivers configured on the backend.
4. **The Flasher**: Use WebSerial to push the payload directly onto an ESP32 via USB natively in your browser!

[Start Building ↗](https://github.com/parakram/parakram)
