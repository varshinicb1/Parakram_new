#!/usr/bin/env bash
# =============================================================================
# Parakram Backend — Integration Test Suite
# Vidyuthlabs — cbvarshini1@gmail.com
# =============================================================================
# Usage:
#   bash integration_test.sh
# Requires: curl, jq
# The backend is started automatically with `cargo run` from the backend root.
# =============================================================================

set -euo pipefail

# ── Colour helpers ────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# ── Counters ──────────────────────────────────────────────────────────────────
PASS=0
FAIL=0

# ── Configuration ─────────────────────────────────────────────────────────────
BASE_URL="${BASE_URL:-http://localhost:8400/api}"
BACKEND_PID=""
STARTUP_TIMEOUT="${STARTUP_TIMEOUT:-60}"   # seconds to wait for backend to be ready
POLL_INTERVAL=2

# ── Unique test-run suffix to avoid cross-run username collisions ─────────────
RUN_ID="$(date +%s)"
TEST_USER="testuser_${RUN_ID}"
TEST_PASS="Password123!"
TEST_EMAIL="test_${RUN_ID}@example.com"

# ── Trap: always kill the backend on exit ────────────────────────────────────
cleanup() {
    if [ -n "$BACKEND_PID" ] && kill -0 "$BACKEND_PID" 2>/dev/null; then
        echo -e "\n${YELLOW}[cleanup] Killing backend PID ${BACKEND_PID}...${RESET}"
        kill "$BACKEND_PID" 2>/dev/null || true
        wait "$BACKEND_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ── Helper: assert HTTP status ────────────────────────────────────────────────
assert_status() {
    local name="$1"
    local expected="$2"
    local actual="$3"
    if [ "$actual" = "$expected" ]; then
        echo -e "  ${GREEN}✓${RESET} $name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} $name ${RED}(expected HTTP $expected, got $actual)${RESET}"
        FAIL=$((FAIL + 1))
    fi
}

# ── Helper: assert body contains substring ───────────────────────────────────
assert_contains() {
    local name="$1"
    local needle="$2"
    local haystack="$3"
    if echo "$haystack" | grep -q "$needle"; then
        echo -e "  ${GREEN}✓${RESET} $name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} $name ${RED}(missing: $needle)${RESET}"
        FAIL=$((FAIL + 1))
    fi
}

# ── Helper: assert body does NOT contain substring ───────────────────────────
assert_not_contains() {
    local name="$1"
    local needle="$2"
    local haystack="$3"
    if ! echo "$haystack" | grep -q "$needle"; then
        echo -e "  ${GREEN}✓${RESET} $name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} $name ${RED}(unexpectedly found: $needle)${RESET}"
        FAIL=$((FAIL + 1))
    fi
}

# ── Helper: assert jq expression is true over JSON body ──────────────────────
assert_json() {
    local name="$1"
    local expr="$2"      # jq boolean expression, e.g. '.data | length > 0'
    local body="$3"
    local result
    result=$(echo "$body" | jq -r "$expr" 2>/dev/null || echo "false")
    if [ "$result" = "true" ]; then
        echo -e "  ${GREEN}✓${RESET} $name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} $name ${RED}(jq '$expr' => $result)${RESET}"
        FAIL=$((FAIL + 1))
    fi
}

# ── curl wrapper: returns "STATUS_CODE BODY" on two lines ────────────────────
# Usage: http_get <url> [Authorization: Bearer <token>]
# Returns two variables: $STATUS and $BODY (caller sets them)
do_request() {
    local method="$1"
    local url="$2"
    local extra_headers="${3:-}"
    local data="${4:-}"

    local header_args=()
    if [ -n "$extra_headers" ]; then
        # extra_headers may be "Authorization: Bearer <tok>"
        header_args=(-H "$extra_headers")
    fi

    local data_args=()
    if [ -n "$data" ]; then
        data_args=(-d "$data" -H "Content-Type: application/json")
    fi

    local response
    response=$(curl -s -o /tmp/_parakram_test_body -w "%{http_code}" \
        -X "$method" \
        "${header_args[@]+"${header_args[@]}"}" \
        "${data_args[@]+"${data_args[@]}"}" \
        "$url" 2>/dev/null)

    STATUS="$response"
    BODY=$(cat /tmp/_parakram_test_body 2>/dev/null || echo "")
}

# ── Start backend ─────────────────────────────────────────────────────────────
start_backend() {
    # Resolve backend root (one level up from this script's directory)
    local script_dir
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local backend_dir
    backend_dir="$(dirname "$script_dir")"

    echo -e "${CYAN}[setup] Starting backend from ${backend_dir}...${RESET}"

    (
        cd "$backend_dir"
        # Use a minimal .env if present, otherwise rely on env vars already set
        if [ -f .env ]; then
            set -a; source .env; set +a
        fi
        exec cargo run --quiet 2>&1
    ) &
    BACKEND_PID=$!

    echo -e "${CYAN}[setup] Backend PID: ${BACKEND_PID}. Waiting for health endpoint...${RESET}"

    local elapsed=0
    while [ "$elapsed" -lt "$STARTUP_TIMEOUT" ]; do
        if curl -s -f "${BASE_URL}/system/health" >/dev/null 2>&1; then
            echo -e "${GREEN}[setup] Backend ready after ${elapsed}s.${RESET}\n"
            return 0
        fi
        # Check the process is still alive
        if ! kill -0 "$BACKEND_PID" 2>/dev/null; then
            echo -e "${RED}[setup] Backend process died during startup.${RESET}"
            return 1
        fi
        sleep "$POLL_INTERVAL"
        elapsed=$((elapsed + POLL_INTERVAL))
    done

    echo -e "${RED}[setup] Backend did not become ready within ${STARTUP_TIMEOUT}s.${RESET}"
    return 1
}

# =============================================================================
# TESTS
# =============================================================================

run_auth_tests() {
    echo -e "${BOLD}${CYAN}── Auth ─────────────────────────────────────────────────────────────────${RESET}"

    # 1. Register — valid payload without email → 201, has `token`
    # (No email = no verification required; separate tests cover the email-verification flow)
    do_request POST "${BASE_URL}/auth/register" "" \
        "{\"username\":\"${TEST_USER}\",\"password\":\"${TEST_PASS}\"}"
    assert_status "register: valid payload returns 201" "201" "$STATUS"
    assert_contains "register: response has 'token' field" '"token"' "$BODY"
    # Save the token for later tests
    AUTH_TOKEN=$(echo "$BODY" | jq -r '.token // empty' 2>/dev/null || echo "")

    # 2. Register — duplicate username → 409
    do_request POST "${BASE_URL}/auth/register" "" \
        "{\"username\":\"${TEST_USER}\",\"password\":\"${TEST_PASS}\"}"
    assert_status "register: duplicate username returns 409" "409" "$STATUS"

    # 3. Register — short password → 422
    do_request POST "${BASE_URL}/auth/register" "" \
        "{\"username\":\"${TEST_USER}_2\",\"password\":\"short\"}"
    assert_status "register: short password returns 422" "422" "$STATUS"

    # 4. Login — valid credentials → 200, has `token`
    do_request POST "${BASE_URL}/auth/login" "" \
        "{\"username\":\"${TEST_USER}\",\"password\":\"${TEST_PASS}\"}"
    assert_status "login: valid credentials returns 200" "200" "$STATUS"
    assert_contains "login: response has 'token' field" '"token"' "$BODY"
    # Prefer the login token (freshly issued)
    if [ -n "$(echo "$BODY" | jq -r '.token // empty' 2>/dev/null)" ]; then
        AUTH_TOKEN=$(echo "$BODY" | jq -r '.token')
    fi

    # 5. Login — wrong password → 401
    do_request POST "${BASE_URL}/auth/login" "" \
        "{\"username\":\"${TEST_USER}\",\"password\":\"wrongpassword\"}"
    assert_status "login: wrong password returns 401" "401" "$STATUS"

    # 6. GET /me — with valid token → 200
    do_request GET "${BASE_URL}/auth/me" "Authorization: Bearer ${AUTH_TOKEN}"
    assert_status "me: valid token returns 200" "200" "$STATUS"
    assert_contains "me: response has 'user_id' field" '"user_id"' "$BODY"

    # 7. GET /me — no token → 401
    do_request GET "${BASE_URL}/auth/me"
    assert_status "me: no token returns 401" "401" "$STATUS"
}

run_system_tests() {
    echo -e "\n${BOLD}${CYAN}── System ───────────────────────────────────────────────────────────────${RESET}"

    # 8. GET /system/health → 200, has `status`
    do_request GET "${BASE_URL}/system/health"
    assert_status "health: returns 200" "200" "$STATUS"
    assert_contains "health: response has 'status' field" '"status"' "$BODY"

    # 9. GET /system/ready → 200 or 503
    do_request GET "${BASE_URL}/system/ready"
    local ready_ok=false
    if [ "$STATUS" = "200" ] || [ "$STATUS" = "503" ]; then
        ready_ok=true
    fi
    if $ready_ok; then
        echo -e "  ${GREEN}✓${RESET} ready: returns 200 or 503 (got $STATUS)"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} ready: expected 200 or 503, got $STATUS"
        FAIL=$((FAIL + 1))
    fi
}

run_billing_tests() {
    echo -e "\n${BOLD}${CYAN}── Billing ──────────────────────────────────────────────────────────────${RESET}"

    # 10. GET /billing/plans → 200, array with at least 1 plan
    do_request GET "${BASE_URL}/billing/plans"
    assert_status "billing/plans: returns 200" "200" "$STATUS"
    assert_json   "billing/plans: response is non-empty array" '. | (type == "array") and (length >= 1)' "$BODY"

    # 11. GET /billing/me — with token → 200 or 404 (no subscription yet is also fine)
    do_request GET "${BASE_URL}/billing/me" "Authorization: Bearer ${AUTH_TOKEN}"
    local bme_ok=false
    if [ "$STATUS" = "200" ] || [ "$STATUS" = "404" ]; then
        bme_ok=true
    fi
    if $bme_ok; then
        echo -e "  ${GREEN}✓${RESET} billing/me: returns 200 or 404 (got $STATUS)"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} billing/me: expected 200 or 404, got $STATUS"
        FAIL=$((FAIL + 1))
    fi
}

run_marketplace_tests() {
    echo -e "\n${BOLD}${CYAN}── Marketplace ──────────────────────────────────────────────────────────${RESET}"

    # 12. GET /marketplace → 200, has `drivers` array
    do_request GET "${BASE_URL}/marketplace"
    assert_status "marketplace: returns 200" "200" "$STATUS"
    assert_contains "marketplace: response has 'drivers' key" '"drivers"' "$BODY"
    assert_json     "marketplace: 'drivers' is an array" '.drivers | type == "array"' "$BODY"

    # 13. GET /marketplace?search=dht → 200
    do_request GET "${BASE_URL}/marketplace?search=dht"
    assert_status "marketplace?search=dht: returns 200" "200" "$STATUS"
    assert_contains "marketplace search: response has 'drivers' key" '"drivers"' "$BODY"
}

run_ota_tests() {
    echo -e "\n${BOLD}${CYAN}── OTA ──────────────────────────────────────────────────────────────────${RESET}"

    # 14. GET /ota/check/<device_id> — with token → 200 or 404
    #     A real device is unlikely to exist in a fresh test DB, so 404 is correct.
    do_request GET "${BASE_URL}/ota/check/00000000-0000-0000-0000-000000000001" \
        "Authorization: Bearer ${AUTH_TOKEN}"
    local ota_ok=false
    if [ "$STATUS" = "200" ] || [ "$STATUS" = "404" ]; then
        ota_ok=true
    fi
    if $ota_ok; then
        echo -e "  ${GREEN}✓${RESET} ota/check: returns 200 or 404 (got $STATUS)"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗${RESET} ota/check: expected 200 or 404, got $STATUS"
        FAIL=$((FAIL + 1))
    fi
}

run_fleet_tests() {
    echo -e "\n${BOLD}${CYAN}── Fleet ────────────────────────────────────────────────────────────────${RESET}"

    # 15. GET /fleet/overview — with token → 200, has `total_devices`
    do_request GET "${BASE_URL}/fleet/overview" "Authorization: Bearer ${AUTH_TOKEN}"
    assert_status "fleet/overview: returns 200" "200" "$STATUS"
    assert_contains "fleet/overview: has 'total_devices' field" '"total_devices"' "$BODY"

    # 16. GET /fleet/devices — with token → 200, is array
    do_request GET "${BASE_URL}/fleet/devices" "Authorization: Bearer ${AUTH_TOKEN}"
    assert_status "fleet/devices: returns 200" "200" "$STATUS"
    assert_json   "fleet/devices: response is array" '. | type == "array"' "$BODY"
}

run_password_reset_tests() {
    echo -e "\n${BOLD}${CYAN}── Password Reset ───────────────────────────────────────────────────────${RESET}"

    # 17. POST /auth/forgot-password — existing user → 200 with generic message
    do_request POST "${BASE_URL}/auth/forgot-password" "" \
        "{\"username\":\"${TEST_USER}\"}"
    assert_status "forgot-password: existing user returns 200" "200" "$STATUS"
    assert_contains "forgot-password: response has 'message' field" '"message"' "$BODY"

    # 18. POST /auth/forgot-password — unknown user → 200 (no username enumeration)
    do_request POST "${BASE_URL}/auth/forgot-password" "" \
        "{\"username\":\"nonexistent_user_xyz_${RUN_ID}\"}"
    assert_status "forgot-password: unknown user also returns 200" "200" "$STATUS"

    # 19. POST /auth/reset-password — invalid code → 400
    do_request POST "${BASE_URL}/auth/reset-password" "" \
        "{\"username\":\"${TEST_USER}\",\"code\":\"000000\",\"new_password\":\"NewPass456!\"}"
    assert_status "reset-password: invalid code returns 400" "400" "$STATUS"
}

run_users_tests() {
    echo -e "\n${BOLD}${CYAN}── Users ────────────────────────────────────────────────────────────────${RESET}"

    # 20. GET /users — regular user token → 403 (admin role required)
    do_request GET "${BASE_URL}/users" "Authorization: Bearer ${AUTH_TOKEN}"
    assert_status "users: non-admin token returns 403" "403" "$STATUS"
    assert_contains "users: response has 'error' field" '"error"' "$BODY"

    # 21. GET /users — no token → 401
    do_request GET "${BASE_URL}/users"
    assert_status "users: no token returns 401" "401" "$STATUS"
}

run_email_verification_tests() {
    echo -e "\n${BOLD}${CYAN}── Email Verification ───────────────────────────────────────────────────${RESET}"
    local VER_USER="vertest_${RUN_ID}"

    # 22. Register with email → 201 (triggers verification code)
    do_request POST "${BASE_URL}/auth/register" "" \
        "{\"username\":\"${VER_USER}\",\"password\":\"${TEST_PASS}\",\"email\":\"test_${RUN_ID}@example.com\"}"
    assert_status "email-verify: register with email returns 201" "201" "$STATUS"

    # 23. Login before verification → 403 EMAIL_NOT_VERIFIED
    do_request POST "${BASE_URL}/auth/login" "" \
        "{\"username\":\"${VER_USER}\",\"password\":\"${TEST_PASS}\"}"
    assert_status "email-verify: login before verify returns 403" "403" "$STATUS"
    assert_contains "email-verify: error is EMAIL_NOT_VERIFIED" 'EMAIL_NOT_VERIFIED' "$BODY"

    # 24. verify-email with wrong code → 400
    do_request POST "${BASE_URL}/auth/verify-email" "" \
        "{\"username\":\"${VER_USER}\",\"code\":\"000000\"}"
    assert_status "email-verify: invalid code returns 400" "400" "$STATUS"
}

run_negative_tests() {
    echo -e "\n${BOLD}${CYAN}── Negative / Auth-guard ────────────────────────────────────────────────${RESET}"

    # 22. GET /billing/me — no token → 401
    do_request GET "${BASE_URL}/billing/me"
    assert_status "billing/me: no token returns 401" "401" "$STATUS"

    # 23. GET /fleet/overview — no token → 401
    do_request GET "${BASE_URL}/fleet/overview"
    assert_status "fleet/overview: no token returns 401" "401" "$STATUS"

    # 24. Unknown route → 404
    do_request GET "${BASE_URL}/nonexistent_route_xyz_$(date +%s)"
    assert_status "unknown route: returns 404" "404" "$STATUS"
}

# =============================================================================
# MAIN
# =============================================================================

echo -e "${BOLD}${CYAN}╔═══════════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}${CYAN}║         Parakram Integration Test Suite                   ║${RESET}"
echo -e "${BOLD}${CYAN}╚═══════════════════════════════════════════════════════════╝${RESET}\n"

# Detect if the backend is already running; if so, skip `cargo run`.
if curl -s -f "${BASE_URL}/system/health" >/dev/null 2>&1; then
    echo -e "${YELLOW}[setup] Backend already running at ${BASE_URL} — skipping cargo run.${RESET}\n"
else
    start_backend || {
        echo -e "\n${RED}[fatal] Could not start backend. Aborting.${RESET}"
        exit 1
    }
fi

AUTH_TOKEN=""   # set by run_auth_tests

run_auth_tests
run_system_tests
run_billing_tests
run_marketplace_tests
run_ota_tests
run_fleet_tests
run_password_reset_tests
run_users_tests
run_email_verification_tests
run_negative_tests

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}${CYAN}─────────────────────────────────────────────────────────────${RESET}"
TOTAL=$((PASS + FAIL))
if [ "$FAIL" -eq 0 ]; then
    echo -e "${BOLD}${GREEN}Tests: ${PASS} passed, ${FAIL} failed (${TOTAL} total)  ALL PASS${RESET}"
else
    echo -e "${BOLD}${RED}Tests: ${PASS} passed, ${FAIL} failed (${TOTAL} total)${RESET}"
fi
echo -e "${BOLD}${CYAN}─────────────────────────────────────────────────────────────${RESET}"

[ "$FAIL" -eq 0 ]   # exit 0 on success, 1 on any failure
