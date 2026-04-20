# Parakram API — Developer Quickstart

Parakram is Vidyuthlabs' zero-code embedded platform. Describe what you want
your IoT device to do in plain English; the backend converts it to a validated
IR document, compiles it to signed bytecode, and deploys it to your board.

**Base URL:** `http://localhost:8400/api` (local dev)  
**Production:** `https://api.parakram.vidyuthlabs.co.in/api`  
**API Reference:** [openapi.yaml](./openapi.yaml)

---

## Table of contents

1. [Authentication](#authentication)
2. [Quick start](#quick-start)
   - [Register](#1-register)
   - [Login](#2-login)
   - [Process an intent](#3-process-an-intent)
   - [Compile the IR](#4-compile-the-ir)
   - [Deploy the bytecode](#5-deploy-the-bytecode)
3. [WebSocket telemetry](#websocket-telemetry)
4. [Billing and quotas](#billing-and-quotas)
5. [Driver registry and boards](#driver-registry-and-boards)
6. [Marketplace](#marketplace)
7. [Fleet management](#fleet-management)
8. [OTA updates](#ota-updates)
9. [Error format](#error-format)
10. [Rate limits](#rate-limits)

---

## Authentication

All endpoints under `/api/` except the system and public listing routes require
a JWT Bearer token.

### How to get a token

Register once, then log in on subsequent sessions. Each token is valid for
**24 hours**.

```bash
# Register (one-time)
curl -s -X POST http://localhost:8400/api/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"ada_lovelace","email":"ada@example.com","password":"s3cur3P@ssw0rd"}' \
  | jq .

# Login (returns a fresh token)
curl -s -X POST http://localhost:8400/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"ada_lovelace","password":"s3cur3P@ssw0rd"}' \
  | jq .token
```

Both endpoints return:

```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "expires_at": "2026-04-21T12:00:00Z",
  "user_id": "550e8400-e29b-41d4-a716-446655440000",
  "username": "ada_lovelace"
}
```

### How to use the token

Pass it in every request as an HTTP header:

```
Authorization: Bearer <token>
```

Example:

```bash
TOKEN="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."

curl -s http://localhost:8400/api/auth/me \
  -H "Authorization: Bearer $TOKEN" \
  | jq .
```

---

## Quick start

The full flow is: **register → login → pair device → process intent → compile → deploy**.

The examples below use a `$TOKEN` shell variable. Set it once:

```bash
TOKEN=$(curl -s -X POST http://localhost:8400/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"ada_lovelace","password":"s3cur3P@ssw0rd"}' \
  | jq -r .token)
```

### 1. Register

```bash
curl -s -X POST http://localhost:8400/api/auth/register \
  -H 'Content-Type: application/json' \
  -d '{
    "username": "ada_lovelace",
    "email":    "ada@example.com",
    "password": "s3cur3P@ssw0rd"
  }' | jq .
```

**Response `201`:**

```json
{
  "token":      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "expires_at": "2026-04-21T12:00:00Z",
  "user_id":    "550e8400-e29b-41d4-a716-446655440000",
  "username":   "ada_lovelace"
}
```

> Username rules: 3–32 characters, letters / digits / underscore only.  
> Password minimum: 8 characters.

---

### 2. Login

```bash
curl -s -X POST http://localhost:8400/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"ada_lovelace","password":"s3cur3P@ssw0rd"}' \
  | jq .
```

---

### 3. Process an intent

Send a plain-English description. Get back a validated IR document.

```bash
curl -s -X POST http://localhost:8400/api/llm/intent \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{
    "description": "Read temperature every 30 seconds. If above 28 degrees Celsius, turn on the fan relay.",
    "board_id":    "esp32s3-devkit-c1"
  }' | jq .
```

**Response `200` (feasible):**

```json
{
  "feasible": true,
  "llm_model": "claude-sonnet-4-6",
  "generation_time_ms": 487,
  "ir": {
    "version": "1.0",
    "program_id": "prog_abc123",
    "board_id": "esp32s3-devkit-c1",
    "devices": [
      { "id": "temp_sensor", "driver": "drv_dht22", "bus": "gpio",
        "pin_slot": "4", "capabilities": ["temperature","humidity"] }
    ],
    "triggers": [
      { "id": "trig_timer", "trigger_type": "timer", "interval_ms": 30000 }
    ],
    "pipelines": [ "..." ]
  },
  "ir_preview": {
    "summary": "1 pipeline(s) with 1 trigger(s), 1 sensor(s), 1 actuator(s)",
    "sensors_used": ["temp_sensor (drv_dht22)"],
    "actuators_used": ["relay_fan (drv_relay)"]
  },
  "validation": { "valid": true, "errors": [], "warnings": [], "steps_completed": 8 }
}
```

If the description is infeasible, `feasible` is `false` and the response
contains `reason`, optional `clarifications`, and optional `suggestions`
instead of `ir`.

> **Quota:** each successful intent costs 1 `llm_intents_per_month` unit.  
> Free plan: 20 per month. Upgrade at `/api/billing/checkout`.

---

### 4. Compile the IR

Take the `ir` object from the intent response and compile it to signed bytecode.
You also need your paired device UUID (see `POST /api/devices/pair`).

```bash
DEVICE_ID="7c9e6679-7425-40de-944b-e07fc1f90ae7"

# IR is the object returned by /llm/intent — store it in a file for readability
# echo "$INTENT_RESPONSE" | jq .ir > ir.json

curl -s -X POST http://localhost:8400/api/ir/compile \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{
    \"device_id\": \"$DEVICE_ID\",
    \"ir\": $(cat ir.json)
  }" | jq .
```

**Response `200`:**

```json
{
  "bytecode_b64":      "AAABAAI...",
  "bytecode_size":     248,
  "bytecode_hash":     "3b4c2d...",
  "num_instructions":  12,
  "num_constants":     4,
  "num_pipelines":     1,
  "ir_summary": {
    "devices": 2,
    "state_variables": 1,
    "triggers": 1,
    "total_nodes": 4
  }
}
```

> **Quota:** each compile costs 1 `compiles_per_month` unit.

If the IR fails validation, you receive HTTP `400` with structured
`ValidationError` objects showing which of the 8 validation steps failed.

You can also call `POST /api/ir/validate` (no quota cost) to check an IR
document without compiling it.

---

### 5. Deploy the bytecode

Record the deployment and bind the bytecode to the device. You need the
`bytecode_b64` from step 4 and a `project_id` from `POST /api/projects`.

```bash
PROJECT_ID="9e107d9d-372b-4d4f-83eb-6e9e10843b22"
BYTECODE_B64="AAABAAI..."

curl -s -X POST "http://localhost:8400/api/ir/deploy/$DEVICE_ID" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{
    \"bytecode_b64\":    \"$BYTECODE_B64\",
    \"project_id\":      \"$PROJECT_ID\",
    \"transfer_method\": \"wifi\"
  }" | jq .
```

**Response `200`:**

```json
{
  "status":          "deployed",
  "device_id":       "7c9e6679-7425-40de-944b-e07fc1f90ae7",
  "transfer_method": "wifi",
  "message":         "Bytecode payload ready for device transfer"
}
```

The device now pulls the bytecode via the OTA endpoints (`GET /api/ota/check`,
`GET /api/ota/manifest`, `GET /api/ota/chunk`).

---

## WebSocket telemetry

Subscribe to a live stream of sensor readings and device vitals for any of your
paired devices.

**URL format:**

```
ws://localhost:8400/api/telemetry/ws/<device_id>?token=<jwt>
```

Because the WebSocket upgrade request cannot carry an `Authorization` header,
the token is passed as a query parameter.

### wscat example

```bash
npm install -g wscat   # one-time install

DEVICE_ID="7c9e6679-7425-40de-944b-e07fc1f90ae7"

wscat -c "ws://localhost:8400/api/telemetry/ws/$DEVICE_ID?token=$TOKEN"
```

### Frames you will receive

**On connect:**

```json
{ "type": "connected", "deviceId": "7c9e6679-7425-40de-944b-e07fc1f90ae7" }
```

**Every 2 seconds:**

```json
{
  "type": "telemetry",
  "ts":   1745150400,
  "data": {
    "temperature": 24.7,
    "humidity":    58.3,
    "uptime_s":    42,
    "free_heap":   189440,
    "rssi":        -67
  }
}
```

### Keep-alive

Send `{"type":"ping"}` to receive `{"type":"pong"}`. The server also handles
WebSocket-level ping frames automatically.

---

## Billing and quotas

Parakram uses usage-based quotas per billing period. Check your current
consumption at any time:

```bash
# Your plan
curl -s http://localhost:8400/api/billing/me \
  -H "Authorization: Bearer $TOKEN" | jq .

# This period's usage
curl -s http://localhost:8400/api/billing/usage \
  -H "Authorization: Bearer $TOKEN" | jq .

# All plans (public, no auth)
curl -s http://localhost:8400/api/billing/plans | jq .
```

| Plan | Price | LLM intents | Compiles | Deploys | Devices |
|------|-------|-------------|----------|---------|---------|
| Free | $0 | 20 | 50 | 10 | 1 |
| Hobby | $9/mo | 500 | 2 000 | 500 | 5 |
| Pro | $29/mo | 10 000 | 50 000 | 10 000 | 50 |
| Enterprise | Custom | Unlimited | Unlimited | Unlimited | Unlimited |

To upgrade, create a Stripe Checkout session:

```bash
curl -s -X POST http://localhost:8400/api/billing/checkout \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{
    "tier":        "hobby",
    "success_url": "https://yourapp.com/billing/success",
    "cancel_url":  "https://yourapp.com/billing/cancel"
  }' | jq .url
```

Redirect your user to the returned URL to complete payment.

---

## Driver registry and boards

All 59 built-in drivers and supported boards are queryable without authentication.

```bash
# All drivers
curl -s http://localhost:8400/api/drivers | jq '.total, .drivers[0]'

# Single driver
curl -s http://localhost:8400/api/drivers/drv_dht22 | jq .

# All boards
curl -s http://localhost:8400/api/boards | jq .

# Board pin map
curl -s http://localhost:8400/api/boards/esp32s3-devkit-c1 | jq .
```

Use the `driver` field values from the registry as the `driver` property in
your IR documents. The `board_id` comes from the board `sku` field.

---

## Marketplace

Browse, install, and contribute community-built drivers.

```bash
# Browse (public)
curl -s "http://localhost:8400/api/marketplace?sort=stars&limit=10" | jq .

# Search by capability
curl -s "http://localhost:8400/api/marketplace?search=pressure&bus=i2c" | jq .

# Get driver detail (increments download counter)
curl -s http://localhost:8400/api/marketplace/<driver-uuid> | jq .

# Get source code (requires Hobby+ plan)
curl -s http://localhost:8400/api/marketplace/<driver-uuid>/source \
  -H "Authorization: Bearer $TOKEN" | jq .source_code

# Install (records install for your account)
curl -s -X POST http://localhost:8400/api/marketplace/<driver-uuid>/install \
  -H "Authorization: Bearer $TOKEN"

# Rate a driver
curl -s -X POST http://localhost:8400/api/marketplace/<driver-uuid>/rate \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"stars": 5, "review": "Works great with my BMP280!"}'

# Submit your own driver (requires Hobby+ plan)
curl -s -X POST http://localhost:8400/api/marketplace/submit \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{
    "name":         "drv_my_sensor",
    "display_name": "My Custom Sensor",
    "description":  "Reads pressure via I2C.",
    "version":      "1.0.0",
    "driver_type":  "sensor",
    "source_code":  "#include \"driver_abi.h\"\n..."
  }' | jq .
```

Submissions start as `pending` and become `approved` after admin review.

---

## Fleet management

```bash
# Aggregated stats
curl -s http://localhost:8400/api/fleet/overview \
  -H "Authorization: Bearer $TOKEN" | jq .

# Detailed device list with active project names
curl -s http://localhost:8400/api/fleet/devices \
  -H "Authorization: Bearer $TOKEN" | jq .

# Heartbeat / ping from device or mobile app
curl -s -X POST "http://localhost:8400/api/fleet/devices/$DEVICE_ID/ping" \
  -H "Authorization: Bearer $TOKEN" | jq .
```

---

## OTA updates

Devices poll the OTA endpoints to discover and download firmware updates.

```bash
# Check for an update (device or backend)
curl -s "http://localhost:8400/api/ota/check/$DEVICE_ID" \
  -H "Authorization: Bearer $TOKEN" | jq .

# Fetch the full manifest
curl -s "http://localhost:8400/api/ota/manifest/$DEVICE_ID" \
  -H "Authorization: Bearer $TOKEN" | jq .

# Stream the bytecode (device-facing, uses X-Device-Key instead of JWT)
curl -s "http://localhost:8400/api/ota/chunk/$DEVICE_ID" \
  -H "X-Device-Key: <device-provisioning-key>" \
  --output firmware.bin
```

The `url` field in the manifest points to `/api/ota/chunk/<device_id>`.

---

## Error format

Every error response uses the same envelope:

```json
{
  "error": {
    "code":    "INVALID_CREDENTIALS",
    "message": "Invalid credentials"
  }
}
```

Common error codes:

| Code | HTTP | Meaning |
|------|------|---------|
| `MISSING_TOKEN` | 401 | No `Authorization` header / no `?token` param |
| `INVALID_TOKEN` | 401 | Token malformed or signature invalid |
| `INVALID_CREDENTIALS` | 401 | Wrong username or password |
| `USERNAME_TAKEN` | 409 | Registration username conflict |
| `NOT_FOUND` | 404 | Resource does not exist or is not yours |
| `VALIDATION_ERROR` | 422 | Request body failed semantic rules |
| `VALIDATION_FAILED` | 400/422 | IR validation failed (check `errors` array) |
| `QUOTA_EXCEEDED` | 429 | Monthly quota used up — upgrade plan |
| `LLM_NOT_CONFIGURED` | 503 | `ANTHROPIC_API_KEY` env var not set |
| `DB_ERROR` | 500 | Database query failed |

---

## Rate limits

| Limit | Value | Scope |
|-------|-------|-------|
| Concurrent LLM requests | 10 | Server-wide |
| LLM intents per month | Plan-dependent (20–unlimited) | Per user |
| Compiles per month | Plan-dependent (50–unlimited) | Per user |
| Deploys per month | Plan-dependent (10–unlimited) | Per user |

When the server-side concurrency gate is hit, you receive HTTP `429` with no
JSON body (the axum rate limiter returns a bare status code). Retry after a
short backoff.

When a monthly quota is exceeded, you receive HTTP `429` with a JSON error
body and `code: "QUOTA_EXCEEDED"`.

---

## Further reading

- Full OpenAPI 3.1 spec: [openapi.yaml](./openapi.yaml)
- Architecture overview: [../ARCHITECTURE.md](../ARCHITECTURE.md)
- IR schema and validator: `backend/src/ir/`
- Bytecode compiler: `backend/src/compiler/`
- All 59 driver specs: `backend/src/drivers/registry.rs`
