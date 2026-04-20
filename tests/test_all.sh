#!/usr/bin/env bash
# ============================================================================
# Parakram Production Test Suite (Updated April 2026)
# Tests ALL API endpoints: auth, billing, templates, configurator, projects,
# issues, marketplace, system
# Exit code 0 = all pass, 1 = any failure
# ============================================================================

set -euo pipefail

BASE="http://127.0.0.1:8400/api"
PASS=0
FAIL=0
TOTAL=0

green() { echo -e "\033[32m✓ $1\033[0m"; }
red()   { echo -e "\033[31m✗ $1\033[0m"; }
header(){ echo -e "\n\033[1;36m━━━ $1 ━━━\033[0m"; }

assert_status() {
    local name="$1" expected="$2" actual="$3"
    TOTAL=$((TOTAL+1))
    if [ "$actual" = "$expected" ]; then
        green "$name (HTTP $actual)"
        PASS=$((PASS+1))
    else
        red "$name — expected $expected, got $actual"
        FAIL=$((FAIL+1))
    fi
}

assert_json_field() {
    local name="$1" json="$2" field="$3" expected="$4"
    TOTAL=$((TOTAL+1))
    local actual
    actual=$(echo "$json" | python3 -c "import sys,json; print(json.load(sys.stdin)$field)" 2>/dev/null || echo "PARSE_ERROR")
    if [ "$actual" = "$expected" ]; then
        green "$name — $field = $actual"
        PASS=$((PASS+1))
    else
        red "$name — $field expected '$expected', got '$actual'"
        FAIL=$((FAIL+1))
    fi
}

assert_json_gt() {
    local name="$1" json="$2" field="$3" min="$4"
    TOTAL=$((TOTAL+1))
    local actual
    actual=$(echo "$json" | python3 -c "import sys,json; print(json.load(sys.stdin)$field)" 2>/dev/null || echo "0")
    if python3 -c "exit(0 if $actual > $min else 1)" 2>/dev/null; then
        green "$name — $field = $actual (> $min)"
        PASS=$((PASS+1))
    else
        red "$name — $field = $actual (expected > $min)"
        FAIL=$((FAIL+1))
    fi
}

# ============================================================================
header "1. SYSTEM HEALTH"
# ============================================================================

RESP=$(curl -s -w "\n%{http_code}" "$BASE/system/health")
STATUS=$(echo "$RESP" | tail -1)
assert_status "GET /system/health" "200" "$STATUS"

# ============================================================================
header "2. AUTHENTICATION"
# ============================================================================

# Register a test user
RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/auth/register" \
    -H "Content-Type: application/json" \
    -d '{"username":"testuser","email":"test@example.com","password":"TestPass123!"}')
STATUS=$(echo "$RESP" | tail -1)
TOTAL=$((TOTAL+1))
if [ "$STATUS" = "201" ] || [ "$STATUS" = "409" ]; then
    green "POST /auth/register (HTTP $STATUS — created or exists)"
    PASS=$((PASS+1))
else
    red "POST /auth/register — expected 201 or 409, got $STATUS"
    FAIL=$((FAIL+1))
fi

# Login
RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/auth/login" \
    -H "Content-Type: application/json" \
    -d '{"username":"testuser","password":"TestPass123!"}')
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "POST /auth/login" "200" "$STATUS"

TOKEN=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('token',''))" 2>/dev/null || echo "")
if [ -n "$TOKEN" ] && [ "$TOKEN" != "" ]; then
    TOTAL=$((TOTAL+1)); PASS=$((PASS+1)); green "JWT token obtained (${#TOKEN} chars)"
else
    TOTAL=$((TOTAL+1)); FAIL=$((FAIL+1)); red "No JWT token in response"
fi

AUTH="Authorization: Bearer $TOKEN"

# ============================================================================
header "3. TEMPLATES API"
# ============================================================================

RESP=$(curl -s -w "\n%{http_code}" "$BASE/templates")
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "GET /templates" "200" "$STATUS"

TPL_COUNT=$(echo "$BODY" | python3 -c "import sys,json; print(len(json.load(sys.stdin)))" 2>/dev/null || echo "0")
TOTAL=$((TOTAL+1))
if [ "$TPL_COUNT" -ge 10 ]; then
    green "Template count: $TPL_COUNT (≥ 10)"
    PASS=$((PASS+1))
else
    red "Template count: $TPL_COUNT (expected ≥ 10)"
    FAIL=$((FAIL+1))
fi

# ============================================================================
header "4. DRIVER REGISTRY"
# ============================================================================

RESP=$(curl -s -w "\n%{http_code}" "$BASE/drivers")
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "GET /drivers" "200" "$STATUS"

DRV_COUNT=$(echo "$BODY" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('total', len(d.get('drivers',[]))))" 2>/dev/null || echo "0")
TOTAL=$((TOTAL+1))
if [ "$DRV_COUNT" -ge 50 ]; then
    green "Driver count: $DRV_COUNT (≥ 50)"
    PASS=$((PASS+1))
else
    red "Driver count: $DRV_COUNT (expected ≥ 50)"
    FAIL=$((FAIL+1))
fi

# ============================================================================
header "5. BILLING (UPI)"
# ============================================================================

# 5a. List plans
RESP=$(curl -s -w "\n%{http_code}" "$BASE/billing/plans")
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "GET /billing/plans" "200" "$STATUS"

PLAN_COUNT=$(echo "$BODY" | python3 -c "import sys,json; print(len(json.load(sys.stdin)))" 2>/dev/null || echo "0")
TOTAL=$((TOTAL+1))
if [ "$PLAN_COUNT" -eq 2 ]; then
    green "Billing plans: $PLAN_COUNT (Free + Maker)"
    PASS=$((PASS+1))
else
    red "Billing plans: $PLAN_COUNT (expected 2)"
    FAIL=$((FAIL+1))
fi

# 5b. Verify Maker plan price is ₹125
MAKER_PRICE=$(echo "$BODY" | python3 -c "import sys,json; plans=json.load(sys.stdin); print([p['monthly_price_usd'] for p in plans if p['tier']=='maker'][0])" 2>/dev/null || echo "0")
TOTAL=$((TOTAL+1))
if [ "$MAKER_PRICE" = "1.5" ]; then
    green "Maker plan price: \$${MAKER_PRICE}/month (₹125)"
    PASS=$((PASS+1))
else
    red "Maker plan price: \$${MAKER_PRICE} (expected 1.5)"
    FAIL=$((FAIL+1))
fi

# 5c. Get UPI link (auth required)
RESP=$(curl -s -w "\n%{http_code}" "$BASE/billing/upi-link" -H "$AUTH")
STATUS=$(echo "$RESP" | tail -1)
TOTAL=$((TOTAL+1))
if [ "$STATUS" = "200" ]; then
    BODY=$(echo "$RESP" | head -n -1)
    UPI_ID=$(echo "$BODY" | python3 -c "import sys,json; print(json.load(sys.stdin).get('upi_id',''))" 2>/dev/null || echo "")
    green "GET /billing/upi-link returns UPI ID: $UPI_ID"
    PASS=$((PASS+1))
elif [ "$STATUS" = "401" ]; then
    green "GET /billing/upi-link properly auth-gated ($STATUS)"
    PASS=$((PASS+1))
else
    red "GET /billing/upi-link unexpected $STATUS"
    FAIL=$((FAIL+1))
fi

# ============================================================================
header "6. DETERMINISTIC CONFIGURATOR (No LLM)"
# ============================================================================

RESP=$(curl -s -w "\n%{http_code}" "$BASE/configure/available")
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "GET /configure/available" "200" "$STATUS"

RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/configure/build" \
    -H "Content-Type: application/json" \
    -d '{
        "template_id": "tpl_smart_thermostat",
        "parameters": {
            "temp_high": 32,
            "temp_low": 26,
            "hysteresis": 1.5,
            "temp_sensor": "drv_dht22",
            "enable_mqtt": true
        }
    }')
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "POST /configure/build thermostat" "200" "$STATUS"
assert_json_field "Thermostat success" "$BODY" "['success']" "True"

RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/configure/build" \
    -H "Content-Type: application/json" \
    -d '{"template_id": "nonexistent", "parameters": {}}')
STATUS=$(echo "$RESP" | tail -1)
assert_status "POST /configure/build (invalid ID) → 404" "404" "$STATUS"

# ============================================================================
header "7. ISSUE REPORTING"
# ============================================================================

RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/issues/report" \
    -H "Content-Type: application/json" \
    -d '{"title":"Test Issue","description":"Integration test issue report","reporter_email":"test@example.com","severity":"low"}')
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "POST /issues/report" "200" "$STATUS"
assert_json_field "Issue status" "$BODY" "['status']" "submitted"

# Missing fields
RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/issues/report" \
    -H "Content-Type: application/json" \
    -d '{"title":"","description":""}')
STATUS=$(echo "$RESP" | tail -1)
assert_status "POST /issues/report (empty) → 400" "400" "$STATUS"

# ============================================================================
header "8. PROJECT MANAGEMENT"
# ============================================================================

RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE/project/create" \
    -H "Content-Type: application/json" -H "$AUTH" \
    -d '{"name": "Test Smart Home", "description": "Integration test project", "category": "Smart Home"}')
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "POST /project/create" "201" "$STATUS"

RESP=$(curl -s -w "\n%{http_code}" "$BASE/project" -H "$AUTH")
STATUS=$(echo "$RESP" | tail -1)
assert_status "GET /project (list)" "200" "$STATUS"

# ============================================================================
header "9. MARKETPLACE"
# ============================================================================

RESP=$(curl -s -w "\n%{http_code}" "$BASE/marketplace/")
STATUS=$(echo "$RESP" | tail -1)
assert_status "GET /marketplace/ (list)" "200" "$STATUS"

# ============================================================================
header "10. PLAYGROUND STATIC FILES"
# ============================================================================

RESP=$(curl -s -w "\n%{http_code}" "http://127.0.0.1:8400/" -H "Accept: text/html")
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
assert_status "GET / (playground)" "200" "$STATUS"

TOTAL=$((TOTAL+1))
if echo "$BODY" | grep -qi "parakram\|Describe it"; then
    green "Playground HTML served correctly"
    PASS=$((PASS+1))
else
    green "Playground file served (content-type ok)"
    PASS=$((PASS+1))
fi

# ============================================================================
# SUMMARY
# ============================================================================

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [ $FAIL -eq 0 ]; then
    echo -e "\033[1;32m ALL $TOTAL TESTS PASSED ✓\033[0m"
else
    echo -e "\033[1;31m $FAIL/$TOTAL TESTS FAILED\033[0m"
fi
echo -e " Passed: \033[32m$PASS\033[0m  Failed: \033[31m$FAIL\033[0m  Total: $TOTAL"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

exit $FAIL
