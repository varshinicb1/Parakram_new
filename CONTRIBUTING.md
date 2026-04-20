# Contributing to Parakram

Thank you for your interest in contributing to the future of Zero-Code Autonomous Hardware Compilation! 

Parakram thrives on community collaboration. Whether you're adding a new ESP-IDF driver, expanding the Rust Virtual Machine ISA, or fixing the web-tier LVGL Visual Designer, we welcome your pull requests.

## Architecture Stack

Parakram is split into three main tiers:
1. **Frontend Playground** (`/playground`): HTML/JS/CSS single-page application.
2. **Compiler Backend** (`/backend`): High-performance Rust server running `axum`, handling SQLite persistence, API boundaries, and generating raw Bytecode Arrays from LLM configurations.
3. **C-Firmware Client** (`/firmware`): The ESP-IDF project that runs on the ESP32 hardware, translating bytecode commands into hardware interrupts.

## Development Environment Setup

### 1. Boot up the Rust Backend
```bash
cd backend
# Make sure your API keys are in the .env file
cargo run
```

### 2. Run the Firmware Compilation (ESP-IDF)
```bash
cd firmware
export IDF_PATH=/path/to/esp-idf
. $IDF_PATH/export.sh
idf.py build
# If flashing to physical hardware:
idf.py -p /dev/ttyUSB0 flash monitor
```

### 3. Open the Frontend
Since it relies entirely on the `.env` API boundaries over `http://127.0.0.1:8400`, just launch:
```bash
cd playground
python3 -m http.server 3000
```

## Making a Pull Request
- Create a new branch `feat/your-cool-driver`.
- Ensure all CI tests pass natively on GitHub Actions.
- Ensure `cargo check` and `cargo fmt` are silent.
- Submit for review!
