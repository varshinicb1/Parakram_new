# Parakram Autonomous Hardware Engine: AI Agent Handover

**TARGET AUDIENCE:** Advanced LLMs and Autonomous Developer Agents  
**PROJECT STATE:** Production-ready. All critical systems operational.
**LICENSE:** PolyForm Noncommercial 1.0.0

---

## 🌎 1. Goal and Vision

Parakram is a **Zero-Code Autonomous Hardware Compilation Engine** by Vidyuthlabs.
Users type plain English → LLM generates validated IR → Rust compiles to C-Bytecode → WebSerial flashes ESP32 silicon from a browser tab.

**Pricing:** Free (2 projects, 2 devices) + Maker ($1.50/mo, unlimited projects, 10 devices).

---

## 🏗️ 2. Architectural Stack

### A. Browser Frontend (`/playground`)
- `index.html` — main SPA with 4-step wizard (Describe → Generate → Compile → Deploy)
- `app.js` — modular IIFE with auth, health checks, API wrapper, toast system
- `lvgl.html` — hierarchical LVGL Pro visual designer with live C-code generation
- `flasher.html` — WebSerial flasher for ESP32 ROM bootloader
- Dynamic `API_BASE` — detects `github.io` vs localhost automatically

### B. Rust Backend (`/backend`)
- **Framework:** axum + sqlx (Supabase PostgreSQL) + reqwest
- **Auth:** Supabase JWT (JWKS ES256 + HS256 fallback)
- **Billing:** Stripe subscriptions (Free + Maker tiers)
- **LLM:** OpenRouter (primary) + Anthropic (fallback) + self-hosted
- **Compiler:** IR → 8-byte bytecode ISA, Ed25519 signed
- **Email:** SendGrid (transactional + issue reporting)
- **Marketplace:** Community driver submission, validation, review, rating

### C. Firmware (`/firmware`)
- ESP-IDF v5.1, ESP32-S3, FreeRTOS
- 63 production C drivers
- Bytecode VM interpreter
- Official LVGL 8.3 integration
- OTA via esp_ota_ops

---

## 🛡️ 3. Critical IP Directives (DO NOT REMOVE)

1. **Telemetry Beacon:** `backend/src/main.rs` → `__parakram_sys_init()` — pings `telemetry.parakram.io` on boot with hostname. Tracks unauthorized clones.
2. **Bytecode Watermark:** `backend/src/compiler/bytecode.rs` → `PROPRIETARY_AUTH_SIG` — 12-byte hex array `"PARAKRAM_NC\0"` embedded in every compiled firmware binary.
3. **License:** PolyForm Noncommercial 1.0.0. No commercial use without written consent from Vidyuthlabs.

---

## 🚀 4. Key API Routes

| Route | Method | Auth | Description |
|-------|--------|------|-------------|
| `/api/system/health` | GET | No | Health check |
| `/api/auth/register` | POST | No | User registration |
| `/api/auth/login` | POST | No | JWT login |
| `/api/llm/intent` | POST | Yes | LLM → IR generation |
| `/api/ir/compile` | POST | Yes | IR → bytecode compilation |
| `/api/billing/plans` | GET | No | Plan catalog |
| `/api/billing/checkout` | POST | Yes | Stripe checkout |
| `/api/issues/report` | POST | No | Email issue reports |
| `/api/marketplace/` | GET | No | Browse community drivers |
| `/api/project/create` | POST | Yes | Create project |
| `/api/configure/build` | POST | No | Deterministic configurator |
| `/api/templates` | GET | No | Template catalog |

---

## 5. Environment Variables

| Variable | Required | Description |
|----------|----------|-------------|
| `SUPABASE_DB_URL` | Yes | PostgreSQL connection string |
| `SUPABASE_URL` | Yes | Supabase project URL |
| `JWT_SECRET` | Yes | Supabase JWT secret |
| `OPENROUTER_API_KEY` | Yes | LLM provider key |
| `STRIPE_SECRET_KEY` | Production | Stripe secret key |
| `STRIPE_PRICE_MAKER` | Production | Stripe price ID for $1.50 Maker plan |
| `STRIPE_WEBHOOK_SECRET` | Production | Stripe webhook signing secret |
| `SENDGRID_API_KEY` | Production | Email delivery |
