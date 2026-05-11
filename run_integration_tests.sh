#!/bin/bash
# Integration test runner with mock servers
# Uses trap to ensure servers are killed on exit

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Cleanup function
cleanup() {
    echo ""
    echo "Stopping mock servers..."
    if [ -n "$IQFEED_PID" ] && kill -0 $IQFEED_PID 2>/dev/null; then
        kill $IQFEED_PID 2>/dev/null || true
    fi
    if [ -n "$TRADEZERO_PID" ] && kill -0 $TRADEZERO_PID 2>/dev/null; then
        kill $TRADEZERO_PID 2>/dev/null || true
    fi
    rm -f .fake_iqfeed.pid .fake_tradezero.pid
    echo "Done."
}

# Set trap to cleanup on exit, error, or interrupt
trap cleanup EXIT INT TERM ERR

echo "========================================"
echo "Integration Test Runner"
echo "========================================"
echo ""

# Start mock servers
echo "Starting mock servers..."

contrib/fake_iqfeed &
IQFEED_PID=$!
echo "  IQFeed mock server PID: $IQFEED_PID (ports 9100, 5009, 9200)"

contrib/fake_tradezero &
TRADEZERO_PID=$!
echo "  TradeZero mock server PID: $TRADEZERO_PID (ports 8080, 8081)"

# Wait for servers to be ready
echo "  Waiting for servers to start..."
sleep 2

# Verify servers are running
if ! kill -0 $IQFEED_PID 2>/dev/null; then
    echo "ERROR: IQFeed mock server failed to start"
    exit 1
fi
if ! kill -0 $TRADEZERO_PID 2>/dev/null; then
    echo "ERROR: TradeZero mock server failed to start"
    exit 1
fi

# Quick health check
if curl -s --connect-timeout 2 http://localhost:8080/v1/api/accounts/test/positions > /dev/null; then
    echo "  TradeZero REST API responding OK"
else
    echo "ERROR: TradeZero REST API not responding"
    exit 1
fi

echo ""
echo "========================================"
echo "Running Tests"
echo "========================================"
echo ""

# Disable exit on error for test section so we can report all failures
set +e
FAILED=0

echo "[1/9] Logic tests..."
if ./test_logic; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "[2/9] Database tests..."
if ./test_database; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "[3/9] Market data tests..."
if ./test_market_data; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "[4/9] Order manager tests..."
if ./test_order_manager; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "[5/9] Integration tests..."
if ./test_integration; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "[6/9] Async I/O tests..."
if ./test_async_io; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "[7/9] TradeZero config tests..."
if ./test_tradezero_config; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "[8/9] TradeZero client tests..."
if ./test_tradezero_client; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "[9/9] TradeZero WebSocket tests..."
if ./test_tradezero_websocket; then
    echo ""
else
    echo "  FAILED"
    FAILED=1
fi

echo "========================================"
if [ $FAILED -eq 0 ]; then
    echo "All tests PASSED"
else
    echo "Some tests FAILED"
fi
echo "========================================"

exit $FAILED
