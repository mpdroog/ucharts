#!/bin/bash
# Integration test runner with mock servers
# Uses trap to ensure servers are killed on exit
#
# Usage: ./run_integration_tests.sh [-v]
#   -v  Verbose mode: show all test output

set -e

VERBOSE=0
if [ "$1" = "-v" ]; then
    VERBOSE=1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Cleanup function
cleanup() {
    if [ -n "$IQFEED_PID" ] && kill -0 $IQFEED_PID 2>/dev/null; then
        kill $IQFEED_PID 2>/dev/null || true
    fi
    if [ -n "$TRADEZERO_PID" ] && kill -0 $TRADEZERO_PID 2>/dev/null; then
        kill $TRADEZERO_PID 2>/dev/null || true
    fi
    rm -f .fake_iqfeed.pid .fake_tradezero.pid
}

# Set trap to cleanup on exit, error, or interrupt
trap cleanup EXIT INT TERM ERR

# Start mock servers
echo -n "Starting mock servers..."
contrib/fake_iqfeed >/dev/null 2>&1 &
IQFEED_PID=$!
contrib/fake_tradezero >/dev/null 2>&1 &
TRADEZERO_PID=$!

# Wait for servers to be ready
sleep 1
echo -n "."

# Verify servers are running
if ! kill -0 $IQFEED_PID 2>/dev/null; then
    echo " ERROR: IQFeed mock server failed to start"
    exit 1
fi
if ! kill -0 $TRADEZERO_PID 2>/dev/null; then
    echo " ERROR: TradeZero mock server failed to start"
    exit 1
fi

# Quick health check
if ! curl -s --connect-timeout 2 http://localhost:8080/v1/api/accounts/test/positions > /dev/null; then
    echo " ERROR: TradeZero REST API not responding"
    exit 1
fi
echo " ready"

# Run tests
FAILED=0
FAILED_TESTS=""
TOTAL=9
PASSED=0

run_test() {
    local num=$1
    local name=$2
    local cmd=$3

    echo -n "[$num/$TOTAL] $name..."

    if [ $VERBOSE -eq 1 ]; then
        echo ""
        if $cmd -v; then
            echo "  PASSED"
            PASSED=$((PASSED + 1))
        else
            echo "  FAILED"
            FAILED=1
            FAILED_TESTS="$FAILED_TESTS $name"
        fi
    else
        # Quiet mode: capture output, only show on failure
        local output
        if output=$($cmd 2>&1); then
            echo " ok"
            PASSED=$((PASSED + 1))
        else
            echo " FAIL"
            echo "$output" | sed 's/^/  /'
            echo ""
            FAILED=1
            FAILED_TESTS="$FAILED_TESTS $name"
        fi
    fi
}

run_test 1 "Logic tests" ./test_logic
run_test 2 "Database tests" ./test_database
run_test 3 "Market data tests" ./test_market_data
run_test 4 "Order manager tests" ./test_order_manager
run_test 5 "Integration tests" ./test_integration
run_test 6 "Async I/O tests" ./test_async_io
run_test 7 "TradeZero config tests" ./test_tradezero_config
run_test 8 "TradeZero client tests" ./test_tradezero_client
run_test 9 "TradeZero WebSocket tests" ./test_tradezero_websocket

# Summary
if [ $FAILED -eq 0 ]; then
    echo "OK: $PASSED/$TOTAL tests passed"
else
    echo ""
    echo "FAILED: $PASSED/$TOTAL tests passed"
    echo "Failed tests:$FAILED_TESTS"
fi

exit $FAILED
