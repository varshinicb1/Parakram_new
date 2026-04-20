# AI Pipeline

Parakram's AI pipeline converts plain English descriptions into validated, compiled bytecode for microcontrollers.

## Flow

```
User Prompt (English)
    ↓ POST /api/llm/intent
LLM (OpenRouter/Mistral/Claude)
    ↓ Structured JSON output
IR Document (validated against schema)
    ↓ POST /api/ir/compile
Bytecode (8-byte instructions, Ed25519 signed)
    ↓ WebSerial / OTA / BLE
ESP32-S3 Firmware VM
```

## LLM Integration

The backend supports three LLM providers with automatic fallback:

1. **OpenRouter** (preferred) — access to 100+ models via a single API key
2. **Anthropic Claude** — direct integration as fallback
3. **Self-hosted** — any OpenAI-compatible endpoint via `LLM_BASE_URL`

## IR Validation

Every LLM response is validated through an 8-step pipeline (`src/ir/validator.rs`):

1. JSON schema conformance
2. Driver existence verification against the 63-driver registry
3. Pin conflict detection
4. Bus compatibility checks
5. Trigger/action graph validation
6. Boundary value range checks
7. Resource limit enforcement
8. Security policy compliance

## Quota Enforcement

All metered operations (LLM intents, compiles, deploys) are tracked per-user per-month via the billing quota system. Usage resets on the 1st of each month.
