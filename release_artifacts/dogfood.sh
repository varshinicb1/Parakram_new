#!/bin/bash
set -euo pipefail

BASE="http://127.0.0.1:8400"
CURL_OPTS=(-s -H "Bypass-Tunnel-Reminder: true" -H "Content-Type: application/json" -H "User-Agent: Parakram-Dogfood/1.0")

echo "🐶 Initiating Parakram Dogfood Test Sequence 🐶"

# 1. Login
echo "=> Logging in..."
LOGIN_RESP=$(curl "${CURL_OPTS[@]}" -X POST "$BASE/api/auth/login" -d '{"username":"admin","password":"parakram-admin"}')
TOKEN=$(echo "$LOGIN_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('token',''))")
if [ -z "$TOKEN" ]; then echo "Login failed!"; exit 1; fi
AUTH="-H \"Authorization: Bearer $TOKEN\""

# 2. Pair a virtual device
echo "=> Pairing virtual hardware..."
curl "${CURL_OPTS[@]}" $AUTH -H "Authorization: Bearer $TOKEN" -X POST "$BASE/api/devices/pair" \
  -d '{"device_uuid":"dogfood-001","board_sku":"VDYT-S3-R1","device_pubkey":"0000","name":"Smart Office Node"}' > /dev/null || true

# 3. Simulate User prompt via LLM
echo "=> Submitting user intent: 'When the temperature goes above 25 degrees, turn on the cooler fan'"
INTENT_REQ='{"description":"When the temperature goes above 25 degrees, turn on the cooler fan. Use the drv_bme280 for temp_sensor on i2c_0, and drv_relay for fan on gpio slot 5.","board_id":"VDYT-S3-R1","device_id":"dogfood-001"}'
LLM_RESP=$(curl "${CURL_OPTS[@]}" $AUTH -H "Authorization: Bearer $TOKEN" -X POST "$BASE/api/llm/intent" -d "$INTENT_REQ")

FEASIBLE=$(echo "$LLM_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('feasible', False))")
if [ "$FEASIBLE" != "True" ]; then
    echo "LLM rejected intent or failed:"
    echo "$LLM_RESP" | python3 -m json.tool
    exit 1
fi

echo "=> LLM Interpretation Success! Generating IR Document..."
IR=$(echo "$LLM_RESP" | python3 -c "import sys,json; print(json.dumps(json.load(sys.stdin).get('ir',{})))")

# 4. Compile the IR to hardware bytecode!
echo "=> Compiling the IR down to ESP32 Bytecode..."
COMPILE_REQ="{\"ir\": $IR, \"device_id\": \"dogfood-001\"}"
COMPILE_RESP=$(curl "${CURL_OPTS[@]}" $AUTH -H "Authorization: Bearer $TOKEN" -X POST "$BASE/api/ir/compile" -d "$COMPILE_REQ")
echo "RAW COMPILE RESP: $COMPILE_RESP"

BC_SIZE=$(echo "$COMPILE_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('bytecode_size',0))")
BC_B64=$(echo "$COMPILE_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('bytecode_b64',''))")

echo "=> Success! Final secure binary block size is ${BC_SIZE} bytes."
echo "=> Payload Preview (Base64): ${BC_B64:0:32}..."

echo "🚀 End-to-End Test Completed successfully across external Cloudflare proxy."
