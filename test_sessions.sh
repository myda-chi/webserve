#!/bin/bash
#
# Session / Cookie test suite for a custom HTTP/1.1 server
# ----------------------------------------------------------
# Usage:
#   chmod +x test_sessions.sh
#   ./test_sessions.sh http://localhost:8080 /some/route
#
# Adjust ROUTE below to a real path your server handles (e.g. "/" or "/dashboard").
# If your server has a route that reads/writes session data (e.g. a counter
# stored in session), set TEST_ROUTE to it for test 3.

set -e

HOST="${1:-http://localhost:8080}"
TEST_ROUTE="${2:-/}"
JAR_A=$(mktemp)
JAR_B=$(mktemp)

PASS=0
FAIL=0

pass() { echo "  [PASS] $1"; PASS=$((PASS+1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }

echo "=================================================="
echo " Session/Cookie Test Suite"
echo " Target: $HOST$TEST_ROUTE"
echo "=================================================="

# ---------------------------------------------------------
echo
echo "[1] First request (no cookie) -> server should issue Set-Cookie"
RESP1=$(curl -s -D - -o /dev/null -c "$JAR_A" "$HOST$TEST_ROUTE")
echo "$RESP1" | grep -i "^Set-Cookie" || true

if echo "$RESP1" | grep -qi "^Set-Cookie"; then
    pass "Set-Cookie header present on first request"
else
    fail "No Set-Cookie header found — server did not issue a session cookie"
fi

COOKIE_NAME=$(grep -i "Set-Cookie" <<< "$RESP1" | sed -E 's/^[Ss]et-[Cc]ookie:\s*([^=]+)=.*/\1/' | tr -d '\r')
echo "  Detected cookie name: ${COOKIE_NAME:-<none found>}"

# ---------------------------------------------------------
echo
echo "[2] Reuse cookie jar -> server should NOT issue a new session ID"
RESP2=$(curl -s -D - -o /dev/null -b "$JAR_A" -c "$JAR_A" "$HOST$TEST_ROUTE")
SID_BEFORE=$(grep -i "$COOKIE_NAME" "$JAR_A" | awk '{print $7}')

if echo "$RESP2" | grep -qi "^Set-Cookie"; then
    SID_NEW=$(echo "$RESP2" | grep -i "Set-Cookie" | sed -E "s/.*${COOKIE_NAME}=([^;]+).*/\1/" | tr -d '\r')
    if [ "$SID_NEW" == "$SID_BEFORE" ]; then
        pass "Same session ID returned (server re-confirmed but did not rotate it)"
    else
        fail "Session ID changed between requests ($SID_BEFORE -> $SID_NEW) — session not persisting"
    fi
else
    pass "No new Set-Cookie sent — server recognized existing session silently (also valid)"
fi

# ---------------------------------------------------------
echo
echo "[3] Two independent clients -> must get DIFFERENT session IDs"
curl -s -o /dev/null -c "$JAR_B" "$HOST$TEST_ROUTE"
SID_A=$(grep -i "$COOKIE_NAME" "$JAR_A" | awk '{print $7}')
SID_B=$(grep -i "$COOKIE_NAME" "$JAR_B" | awk '{print $7}')

echo "  Client A session: $SID_A"
echo "  Client B session: $SID_B"

if [ "$SID_A" != "$SID_B" ] && [ -n "$SID_A" ] && [ -n "$SID_B" ]; then
    pass "Sessions are isolated between clients"
else
    fail "Session IDs collided or missing — isolation broken"
fi

# ---------------------------------------------------------
echo
echo "[4] Cookie attributes sanity check (HttpOnly / Path / Max-Age)"
echo "$RESP1" | grep -i "^Set-Cookie"
if echo "$RESP1" | grep -qi "HttpOnly"; then
    pass "HttpOnly flag set (mitigates JS/XSS cookie theft)"
else
    echo "  [WARN] No HttpOnly flag — fine for testing, but add it before calling this production-ready"
fi

# ---------------------------------------------------------
echo
echo "[5] Malformed / tampered cookie -> server should not crash, should issue fresh session"
RESP3=$(curl -s -D - -o /dev/null -H "Cookie: ${COOKIE_NAME}=not-a-real-session-id-12345" "$HOST$TEST_ROUTE")
HTTP_CODE=$(echo "$RESP3" | head -1 | awk '{print $2}')
if [ "$HTTP_CODE" == "200" ]; then
    pass "Server handled bogus session ID gracefully (HTTP 200, did not crash)"
else
    fail "Server returned unexpected status ($HTTP_CODE) for bogus session ID"
fi

# ---------------------------------------------------------
echo
echo "=================================================="
echo " Results: $PASS passed, $FAIL failed"
echo "=================================================="

rm -f "$JAR_A" "$JAR_B"
[ "$FAIL" -eq 0 ]