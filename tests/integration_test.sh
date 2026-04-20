#!/bin/bash
# ============================================================
# Parakram Production Integration Test Suite
# Tests: Auth, Devices, Projects, IR Validate, Compile, Deploy
# ============================================================
set -euo pipefail

BASE="${PARAKRAM_BASE_URL:-http://localhost:8400}"
PASS=0; FAIL=0; TOTAL=0

pass() { PASS=$((PASS+1)); TOTAL=$((TOTAL+1)); echo "  ✅ $1"; }
fail() { FAIL=$((FAIL+1)); TOTAL=$((TOTAL+1)); echo "  ❌ $1: $2"; }
assert_http() {
    local code="$1" expected="$2" label="$3"
    if [ "$code" = "$expected" ]; then pass "$label (HTTP $code)"
    else fail "$label" "expected HTTP $expected, got $code"; fi
}

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║     PARAKRAM v1.0.0 — Production Integration Test   ║"
echo "╠══════════════════════════════════════════════════════╣"
echo "║  Server: $BASE"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

# ──────────────────────────────────────────────────────────
# 1. SYSTEM HEALTH
# ──────────────────────────────────────────────────────────
echo "── System ─────────────────────────────────────────────"
HEALTH=$(curl -s "$BASE/api/system/health")
HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/api/system/health")
assert_http "$HTTP" "200" "Health Check"

DRIVERS=$(echo "$HEALTH" | python3 -c "import sys,json; print(json.load(sys.stdin).get('registered_drivers',0))" 2>/dev/null || echo "0")
if [ "$DRIVERS" -ge 40 ]; then pass "Driver registry ($DRIVERS drivers)"
else fail "Driver registry" "expected ≥40, got $DRIVERS"; fi

# ──────────────────────────────────────────────────────────
# 2. AUTH
# ──────────────────────────────────────────────────────────
echo ""
echo "── Auth ──────────────────────────────────────────────"
LOGIN_RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/api/auth/login" \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"parakram-admin"}')
HTTP=$(echo "$LOGIN_RESP" | tail -1)
BODY=$(echo "$LOGIN_RESP" | head -1)
assert_http "$HTTP" "200" "Admin Login"

TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['token'])" 2>/dev/null)
if [ -n "$TOKEN" ]; then pass "JWT Token received (${TOKEN:0:16}...)"
else fail "JWT Token" "empty token"; fi

AUTH="Authorization: Bearer $TOKEN"

# Bad login
HTTP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE/api/auth/login" \
  -H "Content-Type: application/json" -d '{"username":"admin","password":"wrong"}')
assert_http "$HTTP" "401" "Bad Password Rejected"

# ──────────────────────────────────────────────────────────
# 3. DRIVERS & BOARDS
# ──────────────────────────────────────────────────────────
echo ""
echo "── Drivers & Boards ────────────────────────────────"
HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/api/drivers")
assert_http "$HTTP" "200" "List Drivers"

HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/api/boards")
assert_http "$HTTP" "200" "List Boards"

# ──────────────────────────────────────────────────────────
# 4. DEVICE PAIRING
# ──────────────────────────────────────────────────────────
echo ""
echo "── Device Pairing ──────────────────────────────────"
PAIR_RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/api/devices/pair" \
  -H "$AUTH" -H "Content-Type: application/json" \
  -d '{"device_uuid":"dev-test-001","board_sku":"VDYT-S3-R1","device_pubkey":"0000000000000000000000000000000000000000000000000000000000000000","name":"Test Device Alpha"}')
HTTP=$(echo "$PAIR_RESP" | tail -1)
assert_http "$HTTP" "201" "Pair Device"

# Duplicate pairing
HTTP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE/api/devices/pair" \
  -H "$AUTH" -H "Content-Type: application/json" \
  -d '{"device_uuid":"dev-test-001","board_sku":"VDYT-S3-R1","device_pubkey":"0000","name":"Dup"}')
assert_http "$HTTP" "409" "Duplicate Pair Rejected"

# List devices
HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/api/devices" -H "$AUTH")
assert_http "$HTTP" "200" "List Devices"

# ──────────────────────────────────────────────────────────
# 5. PROJECTS
# ──────────────────────────────────────────────────────────
echo ""
echo "── Projects ──────────────────────────────────────────"
PROJ_RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/api/projects" \
  -H "$AUTH" -H "Content-Type: application/json" \
  -d '{"name":"Smart Thermostat","device_id":"dev-test-001","description":"Auto temperature control with fan"}')
HTTP=$(echo "$PROJ_RESP" | tail -1)
PROJ_BODY=$(echo "$PROJ_RESP" | head -1)
assert_http "$HTTP" "201" "Create Project"

PROJ_ID=$(echo "$PROJ_BODY" | python3 -c "import sys,json; print(json.load(sys.stdin)['project_id'])" 2>/dev/null)
if [ -n "$PROJ_ID" ]; then pass "Project ID: ${PROJ_ID:0:16}..."
else fail "Project ID" "missing"; fi

HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/api/projects" -H "$AUTH")
assert_http "$HTTP" "200" "List Projects"

# ──────────────────────────────────────────────────────────
# 6. IR VALIDATION
# ──────────────────────────────────────────────────────────
echo ""
echo "── IR Validation ─────────────────────────────────────"

# Valid IR that matches exact IRDocument schema
VALID_IR='{
  "version": "1.0",
  "program_id": "thermostat-001",
  "board_id": "VDYT-S3-R1",
  "created_at": "2025-01-01T00:00:00Z",
  "signature": "",
  "devices": [
    {"id": "temp_sensor", "driver": "drv_bme280", "bus": "i2c_0", "address": "0x76", "capabilities": ["temperature", "humidity"]},
    {"id": "fan", "driver": "drv_relay", "bus": "gpio", "pin_slot": "5", "capabilities": ["on_off"]}
  ],
  "state": {
    "temp": {"type": "float", "initial": 0.0},
    "humidity": {"type": "float", "initial": 0.0},
    "fan_on": {"type": "bool", "initial": false}
  },
  "triggers": [
    {"id": "poll_5s", "type": "timer", "interval_ms": 5000}
  ],
  "pipelines": [
    {
      "id": "thermostat_pipeline",
      "trigger": "poll_5s",
      "enabled": true,
      "priority": 5,
      "max_execution_ms": 2000,
      "nodes": [
        {"id": "read_temp", "type": "sensor.read", "device": "temp_sensor", "field": "temperature", "store_to": "temp"},
        {"id": "read_hum", "type": "sensor.read", "device": "temp_sensor", "field": "humidity", "store_to": "humidity"},
        {"id": "check_hot", "type": "condition.compare", "left": "$temp", "op": "gt", "right": 30.0, "if_true": "fan_on_node", "if_false": "fan_off_node"},
        {"id": "fan_on_node", "type": "actuator.write", "device": "fan", "field": "on_off", "value": true},
        {"id": "fan_off_node", "type": "actuator.write", "device": "fan", "field": "on_off", "value": false}
      ]
    }
  ],
  "constraints": {"max_total_nodes": 256, "max_state_variables": 64, "max_pipelines": 16}
}'

VALIDATE_RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/api/ir/validate" \
  -H "$AUTH" -H "Content-Type: application/json" -d "$VALID_IR")
HTTP=$(echo "$VALIDATE_RESP" | tail -1)
VBODY=$(echo "$VALIDATE_RESP" | head -1)
assert_http "$HTTP" "200" "Validate IR"

IS_VALID=$(echo "$VBODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('valid',False))" 2>/dev/null)
STEPS=$(echo "$VBODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('steps_completed',0))" 2>/dev/null)
if [ "$IS_VALID" = "True" ]; then pass "IR Valid (8-step pipeline passed, steps=$STEPS)"
else fail "IR Validity" "expected True, got $IS_VALID (steps=$STEPS)"; fi

# ──────────────────────────────────────────────────────────
# 7. COMPILATION
# ──────────────────────────────────────────────────────────
echo ""
echo "── Compilation ────────────────────────────────────────"
COMPILE_REQ="{\"ir\": $VALID_IR, \"device_id\": \"dev-test-001\"}"
COMPILE_RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/api/ir/compile" \
  -H "$AUTH" -H "Content-Type: application/json" -d "$COMPILE_REQ")
HTTP=$(echo "$COMPILE_RESP" | tail -1)
CBODY=$(echo "$COMPILE_RESP" | head -1)
assert_http "$HTTP" "200" "Compile IR"

BYTECODE_SIZE=$(echo "$CBODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('bytecode_size',0))" 2>/dev/null)
NUM_INSTR=$(echo "$CBODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('num_instructions',0))" 2>/dev/null)
NUM_CONST=$(echo "$CBODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('num_constants',0))" 2>/dev/null)
BC_HASH=$(echo "$CBODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('bytecode_hash','')[:16])" 2>/dev/null)
if [ "$BYTECODE_SIZE" -gt 0 ] 2>/dev/null; then
  pass "Bytecode: ${BYTECODE_SIZE}B, ${NUM_INSTR} instr, ${NUM_CONST} const, hash=${BC_HASH}..."
else fail "Bytecode" "size=0"; fi

BYTECODE_B64=$(echo "$CBODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('bytecode_b64',''))" 2>/dev/null)

# ──────────────────────────────────────────────────────────
# 8. DEPLOYMENT
# ──────────────────────────────────────────────────────────
echo ""
echo "── Deployment ─────────────────────────────────────────"
DEPLOY_RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/api/ir/deploy/dev-test-001" \
  -H "$AUTH" -H "Content-Type: application/json" \
  -d "{\"bytecode_b64\":\"$BYTECODE_B64\",\"project_id\":\"$PROJ_ID\",\"transfer_method\":\"wifi\"}")
HTTP=$(echo "$DEPLOY_RESP" | tail -1)
DBODY=$(echo "$DEPLOY_RESP" | head -1)
assert_http "$HTTP" "200" "Deploy Bytecode"

DEPLOY_STATUS=$(echo "$DBODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('status',''))" 2>/dev/null)
if [ "$DEPLOY_STATUS" = "deployed" ]; then pass "Deploy status: $DEPLOY_STATUS"
else fail "Deploy status" "expected 'deployed', got '$DEPLOY_STATUS'"; fi

# ──────────────────────────────────────────────────────────
# 9. CLEANUP
# ──────────────────────────────────────────────────────────
echo ""
echo "── Cleanup ────────────────────────────────────────────"
HTTP=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$BASE/api/projects/$PROJ_ID" -H "$AUTH")
assert_http "$HTTP" "204" "Delete Project"

HTTP=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$BASE/api/devices/dev-test-001" -H "$AUTH")
assert_http "$HTTP" "204" "Unpair Device"

# ──────────────────────────────────────────────────────────
# SUMMARY
# ──────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════╗"
if [ "$FAIL" = "0" ]; then
echo "║  🎉 ALL $TOTAL TESTS PASSED — READY TO SHIP        ║"
else
echo "║  ⚠️  $PASS/$TOTAL PASSED, $FAIL FAILED              ║"
fi
echo "╚══════════════════════════════════════════════════════╝"
echo ""

exit $FAIL
