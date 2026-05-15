#!/bin/bash
# PANIC SCRIPT - Close all positions and cancel all orders
# Use when the app is broken and you need to get flat immediately
set -e

BASE_URL="https://webapi.tradezero.com/v1/api"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/../config.ini"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${RED}=== PANIC MODE ===${NC}"
echo "This will CANCEL all orders and CLOSE all positions at MARKET price!"
echo ""

# Load credentials from config file if not set in environment
if [[ -z "$TZ_API_KEY_ID" || -z "$TZ_API_SECRET_KEY" || -z "$TZ_ACCOUNT_ID" ]]; then
    if [[ -f "$CONFIG_FILE" ]]; then
        echo "Loading credentials from $CONFIG_FILE..."
        TZ_API_KEY_ID=$(grep -E "^api_key_id\s*=" "$CONFIG_FILE" | sed 's/.*=\s*//' | tr -d '[:space:]')
        TZ_API_SECRET_KEY=$(grep -E "^api_secret_key\s*=" "$CONFIG_FILE" | sed 's/.*=\s*//' | tr -d '[:space:]')
        TZ_ACCOUNT_ID=$(grep -E "^account_id\s*=" "$CONFIG_FILE" | sed 's/.*=\s*//' | tr -d '[:space:]')
    else
        echo -e "${RED}ERROR: Config file not found and environment variables not set${NC}"
        echo "Set TZ_API_KEY_ID, TZ_API_SECRET_KEY, TZ_ACCOUNT_ID or create $CONFIG_FILE"
        exit 1
    fi
fi

# Validate credentials
if [[ -z "$TZ_API_KEY_ID" || -z "$TZ_API_SECRET_KEY" || -z "$TZ_ACCOUNT_ID" ]]; then
    echo -e "${RED}ERROR: Missing credentials${NC}"
    exit 1
fi

echo "Account: $TZ_ACCOUNT_ID"
echo ""

# Fetch current state before confirming
echo -e "${YELLOW}Fetching current state...${NC}"
echo ""

# Get pending orders
orders_response=$(curl -s -X GET "$BASE_URL/accounts/$TZ_ACCOUNT_ID/orders" \
    -H "Content-Type: application/json" \
    -H "TZ-API-KEY-ID: $TZ_API_KEY_ID" \
    -H "TZ-API-SECRET-KEY: $TZ_API_SECRET_KEY")

echo -e "${YELLOW}PENDING ORDERS:${NC}"
if command -v jq &> /dev/null; then
    # API returns {"orders": [...]} not raw array
    order_count=$(echo "$orders_response" | jq 'if type == "object" and .orders then .orders | length elif type == "array" then length else 0 end' 2>/dev/null || echo "0")
    if [[ "$order_count" == "0" || -z "$order_count" ]]; then
        echo "  (none)"
    else
        echo "$orders_response" | jq -r '(if type == "object" and .orders then .orders else . end) | .[] | "  \(.side) \(.orderQuantity) \(.symbol) @ \(.limitPrice // "MKT") [\(.orderStatus)]"' 2>/dev/null
    fi
else
    echo "  (install jq for formatted output)"
    echo "  $orders_response"
fi
echo ""

# Get open positions
positions_response=$(curl -s -X GET "$BASE_URL/accounts/$TZ_ACCOUNT_ID/positions" \
    -H "Content-Type: application/json" \
    -H "TZ-API-KEY-ID: $TZ_API_KEY_ID" \
    -H "TZ-API-SECRET-KEY: $TZ_API_SECRET_KEY")

echo -e "${YELLOW}OPEN POSITIONS:${NC}"
if command -v jq &> /dev/null; then
    # API returns {"positions": [...]} not raw array
    pos_count=$(echo "$positions_response" | jq 'if type == "object" and .positions then .positions | length elif type == "array" then length else 0 end' 2>/dev/null || echo "0")
    if [[ "$pos_count" == "0" || -z "$pos_count" ]]; then
        echo "  (none)"
    else
        echo "$positions_response" | jq -r '(if type == "object" and .positions then .positions else . end) | .[] | "  \(.symbol): \(.shares) shares @ $\(.priceAvg | tostring | .[0:6]) (current: $\(.priceClose | tostring | .[0:6]))"' 2>/dev/null
    fi
else
    echo "  (install jq for formatted output)"
    echo "  $positions_response"
fi
echo ""

# Check if there's anything to do
if command -v jq &> /dev/null; then
    if [[ "$order_count" == "0" && "$pos_count" == "0" ]]; then
        echo -e "${GREEN}Nothing to cancel or close. You're flat!${NC}"
        exit 0
    fi
fi

# Confirm before proceeding
echo -e "${RED}This will CANCEL all orders above and CLOSE all positions at MARKET price!${NC}"
read -p "Type 'PANIC' to confirm: " confirm
if [[ "$confirm" != "PANIC" ]]; then
    echo "Aborted."
    exit 0
fi

echo ""

# Step 1: Cancel all pending orders
echo -e "${YELLOW}[1/2] Canceling all pending orders...${NC}"
cancel_response=$(curl -s -X DELETE "$BASE_URL/accounts/orders" \
    -H "Content-Type: application/json" \
    -H "TZ-API-KEY-ID: $TZ_API_KEY_ID" \
    -H "TZ-API-SECRET-KEY: $TZ_API_SECRET_KEY")

echo "Response: $cancel_response"
echo -e "${GREEN}Orders canceled.${NC}"
echo ""

# Step 2: Close positions (we already have positions_response)
echo -e "${YELLOW}[2/2] Closing positions at market...${NC}"

# Parse positions using jq if available, otherwise use grep/sed
if command -v jq &> /dev/null; then
    # API returns {"positions": [...]} not raw array
    symbols=$(echo "$positions_response" | jq -r '(if type == "object" and .positions then .positions else . end) | .[] | "\(.symbol):\(.shares)"' 2>/dev/null || echo "")
else
    echo "Warning: jq not installed, using basic parsing"
    symbols=$(echo "$positions_response" | grep -oE '"symbol":"[^"]+"|"shares":[0-9.-]+' | paste - - | sed 's/"symbol":"//g; s/"|"shares":/:/g')
fi

if [[ -z "$symbols" ]]; then
    echo "No open positions to close."
    echo ""
    echo -e "${GREEN}=== DONE ===${NC}"
    exit 0
fi

# Generate unique order ID prefix
order_prefix="PANIC-$(date +%s)"
order_num=0

echo "$symbols" | while IFS=: read -r symbol shares; do
    # Skip if shares is 0 or negative (short positions need buy to close)
    if [[ -z "$shares" || "$shares" == "0" ]]; then
        continue
    fi

    # Determine side: positive shares = sell, negative = buy to cover
    if [[ "$shares" -gt 0 ]]; then
        side="Sell"
        qty="$shares"
    else
        side="Buy"
        qty="${shares#-}"  # Remove minus sign
    fi

    order_num=$((order_num + 1))
    client_order_id="${order_prefix}-${order_num}"

    echo "  Closing $symbol: $side $qty shares (order: $client_order_id)"

    order_body=$(cat <<EOF
{
    "clientOrderId": "$client_order_id",
    "symbol": "$symbol",
    "orderQuantity": $qty,
    "side": "$side",
    "orderType": "Market",
    "securityType": "Stock",
    "timeInForce": "Day"
}
EOF
)

    order_response=$(curl -s -X POST "$BASE_URL/accounts/$TZ_ACCOUNT_ID/order" \
        -H "Content-Type: application/json" \
        -H "TZ-API-KEY-ID: $TZ_API_KEY_ID" \
        -H "TZ-API-SECRET-KEY: $TZ_API_SECRET_KEY" \
        -d "$order_body")

    echo "    Response: $order_response"
done

echo ""
echo -e "${GREEN}=== DONE ===${NC}"
echo "All orders canceled and positions closed at market."
echo "Check your broker for confirmation."
