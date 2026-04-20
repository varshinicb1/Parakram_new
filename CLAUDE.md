# Parakram — Vidyuthlabs
## AI Agent Reference (Honest, Verified April 2026 — updated)

Parakram is the core product of **Vidyuthlabs** (vidyuthlabs.co.in).
It is a zero-code embedded platform: users type plain English, and within ~500ms
the system generates, validates, and deploys executable bytecode to IoT hardware.

---

## What Actually Works Today

| Component | Status | Notes |
|---|---|---|
| Rust backend (axum) | **Compiles + runs** | Port 8400 |
| IR validation (8-step) | **Working** | `src/ir/validator.rs` |
| Bytecode compiler | **Working** | `src/compiler/` |
| Driver registry | **Working** | 59 driver specs in memory |
| LLM pipeline | **Working** | OpenRouter (primary) + Anthropic + self-hosted fallback |
| JWT auth | **Working** | PBKDF2 passwords, 24h tokens, role claim |
| Email verification | **Working** | 6-digit code, 15-min expiry, blocks login |
| Admin role gate | **Working** | `GET /api/users` requires `role == "admin"` |
| SQLite DB | **Working** | 9 tables, WAL mode |
| Playground frontend | **Working** | Vanilla JS, calls backend |
| Android Kotlin app | **Working** | BLE + Retrofit + Jetpack Compose |
| iOS SwiftUI app | **Working** | CoreBluetooth + URLSession + TabView |
| Firmware core (ESP-IDF 5.1) | **Builds** (needs idf.py) | ESP32-S3 only today |
| All 59 firmware drivers | **Real implementations** | `firmware/main/drivers/` |
| Multi-MCU PAL | **Complete** | ESP32-S3, RP2040, STM32F4, Arduino |
| Ed25519 signing (backend) | **Working** | `ring` crate |
| Ed25519 verification (firmware) | **Real** | PSA Crypto API |
| ROS 2 node graph engine | **Working** | Local, zero-LLM, sub-ms latency |
| parakram_msgs ROS 2 package | **Complete** | 7 msg + 3 srv types |
| Stripe billing | **Working** | 4 plans, checkout, portal, webhook, quota enforcement |
| Cloud deployment | **Ready** | Docker Compose, Kubernetes, Azure Container Apps, AWS ECS |
| Observability | **Working** | `/health`, `/ready`, Prometheus `/metrics`, structured logs |
| CI/CD | **Ready** | GitHub Actions — build, test, multi-arch image, Azure deploy |
| Driver marketplace | **Working** | Submit, validate, approve, rate, install, discover |
| OTA firmware updates | **Working** | ESP-IDF esp_ota_ops, SHA-256 verify, auto-reboot |
| WebSocket telemetry | **Working** | Live device stream, 2s push, iOS + Android clients |
| Fleet dashboard | **Working** | Overview + device list API + mobile screens |
| Web playground | **Working** | Dark SPA, 4-step wizard, frosted glass, no build step |
| Rate limiting | **Working** | Auth 10/min, LLM 5/min, API 120/min, per-IP |
| Push notifications | **Working** | Real FCM (legacy) + APNs (ES256 JWT) |
| Password reset | **Working** | 6-digit code, 15-min expiry, iOS + Android screens |
| Admin panel | **Working** | Dark SPA — driver moderation, Prometheus metrics, health |
| OpenAPI spec | **Working** | `docs/openapi.yaml` — 39 paths, 58 schemas, 11 tags |

## What's In Progress / Missing

- Nothing critical. All planned features are implemented.

---

## Build & Run

### Backend
```bash
cd backend
cp .env.example .env        # fill in API keys
cargo run                   # dev mode
cargo build --release       # production binary
```

### Frontend
Open `playground/index.html` in Chrome (or `http://localhost:8400` when backend runs).

### Tests
```bash
cd tests && bash integration_test.sh
```

### Docker (production)
```bash
docker-compose up --build
```

---

## Architecture

```
User (plain English)
       ↓
Playground / Mobile App
       ↓  POST /api/llm/intent
Rust Backend (axum, port 8400)
       ↓  Claude API (prompt-cached system prompt)
IR Document (JSON, validated 8 steps)
       ↓  POST /api/ir/compile
Bytecode (8-byte instructions, Ed25519 signed)
       ↓  WiFi TCP / BLE GATT / WebSerial
ESP32-S3 Firmware (FreeRTOS + VM)
       ↓
Physical hardware (sensors, actuators, displays)
```

---

## Key File Locations

| What | Where |
|---|---|
| LLM → IR pipeline | `backend/src/llm/` |
| IR schema & validator | `backend/src/ir/` |
| Bytecode compiler | `backend/src/compiler/` |
| Driver registry (59 specs) | `backend/src/drivers/registry.rs` |
| All API endpoints | `backend/src/api/` |
| Database schema | `backend/src/db/mod.rs` |
| Firmware boot sequence | `firmware/main/app_main.c` |
| Bytecode VM | `firmware/main/vm.c` (+ `include/vm.h`) |
| Firmware drivers (59) | `firmware/main/drivers/` |
| Multi-MCU PAL header | `firmware/hal/include/parakram_pal.h` |
| ESP32-S3 PAL impl | `firmware/hal/esp32s3/pal_impl.c` |
| RP2040 PAL impl | `firmware/hal/rp2040/pal_impl.c` |
| STM32F4 PAL impl | `firmware/hal/stm32/pal_impl.c` |
| Arduino PAL impl | `firmware/hal/arduino/pal_impl.cpp` |
| One-wire (platform-agnostic) | `firmware/hal/common/pal_onewire.c` |
| RP2040 port build | `firmware/ports/rp2040/` |
| STM32F4 port build | `firmware/ports/stm32f4/` |
| Arduino port (PlatformIO) | `firmware/ports/arduino/parakram_arduino/` |
| ROS 2 messages/services | `ros2_ws/src/parakram_msgs/` |
| ROS graph engine | `backend/src/ros_graph/` |
| Stripe billing | `backend/src/billing/`, `backend/src/api/billing.rs` |
| Driver marketplace | `backend/src/marketplace/`, `backend/src/api/marketplace.rs` |
| Marketplace DB migration | `backend/src/db/migrations/0004_marketplace.sql` |
| Billing DB migration | `backend/src/db/migrations/0003_billing.sql` |
| Metrics (Prometheus) | `backend/src/metrics.rs`, `/api/system/metrics` |
| Production Dockerfile | `backend/Dockerfile` (cargo-chef, distroless-style) |
| Production compose | `docker-compose.prod.yml` + `nginx/nginx.prod.conf` |
| Kubernetes manifests | `deploy/kubernetes/` (deployment, HPA, ingress, PodMonitor) |
| Azure Container Apps | `deploy/azure/container_app.bicep` |
| AWS ECS Fargate | `deploy/aws/ecs-task.json` |
| CI/CD | `.github/workflows/ci.yml`, `.github/workflows/deploy.yml` |
| Android app | `android/app/src/main/` |
| iOS app | `ios/Parakram/` |
| OTA backend endpoints | `backend/src/api/ota.rs` |
| WebSocket telemetry endpoint | `backend/src/api/telemetry_ws.rs` |
| Fleet API | `backend/src/api/fleet.rs` |
| Notification stubs | `backend/src/notifications.rs` |
| Firmware OTA manager | `firmware/main/ota_manager.c` + `include/ota_manager.h` |
| Web playground | `playground/index.html` + `playground/app.js` |
| Admin panel | `admin/index.html` + `admin/app.js` |
| OpenAPI spec | `docs/openapi.yaml` |
| App icon SVG source | `assets/icons/icon.svg` |
| Android adaptive icon | `android/app/src/main/res/drawable/ic_launcher_*.xml` |
| iOS icon xcassets | `ios/Parakram/Assets.xcassets/AppIcon.appiconset/Contents.json` |
| Web playground | `playground/` |

---

## Environment Variables

| Variable | Required | Description |
|---|---|---|
| `OPENROUTER_API_KEY` | Yes (preferred) | Primary LLM — one key, 100+ models |
| `OPENROUTER_MODEL` | No | Default: `openai/gpt-4o-mini` |
| `ANTHROPIC_API_KEY` | Fallback | Used if no OPENROUTER_API_KEY |
| `ANTHROPIC_MODEL` | No | Default: `claude-sonnet-4-6` |
| `LLM_BASE_URL` | Self-hosted only | Any OpenAI-compatible endpoint |
| `DATABASE_URL` | No | Default: `sqlite:parakram.db?mode=rwc` |
| `JWT_SECRET` | Yes (prod) | Base64 random string, min 32 bytes |
| `BIND_ADDR` | No | Default: `0.0.0.0:8400` |

---

## Coding Standards

- Rust: use `thiserror` for errors, `tracing` for logs (not `println!`)
- Firmware C: no dynamic allocation in hot paths, no `malloc` in VM
- All new firmware drivers: implement `driver_vtable_t` from `driver_abi.h`
- All new backend drivers: add to both `registry.rs` AND `llm/prompt.rs`
- No `unwrap()` in production paths — use `?` or explicit error handling
- JWT auth required on all `/api/` endpoints except `/api/system/health`, `/api/drivers`, `/api/boards`

---

## Company

**Vidyuthlabs** — vidyuthlabs.co.in  
Product: **Parakram**  
License: PolyForm Noncommercial 1.0.0
Target: 2M customers building IoT products in natural language
Pricing: Free (2 projects) + Maker ($1.50/month, unlimited projects)
