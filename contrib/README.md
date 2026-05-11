# Mock Servers for Integration Testing

This directory contains mock/fake servers that simulate external APIs for integration testing without requiring real API credentials or internet connectivity.

## Servers

### 1. fake_iqfeed - IQFeed Mock Server

Simulates the IQFeed market data feed with three TCP servers:

- **Lookup Server (Port 9100)**: Historical data requests
  - Responds to `HDX,SYMBOL,DATAPOINTS` (daily data)
  - Responds to `HIX,SYMBOL,INTERVAL,DATAPOINTS` (interval data)
  - Returns fake OHLCV candles

- **Level1 Server (Port 5009)**: Real-time quotes
  - Responds to `S,SET PROTOCOL,6.2` (protocol version)
  - Responds to `wSYMBOL` (watch symbol)
  - Responds to `rSYMBOL` (unwatch symbol)
  - Sends periodic quote updates for watched symbols

- **Level2 Server (Port 9200)**: Order book depth
  - Responds to `S,SET PROTOCOL,6.2` (protocol version)
  - Responds to `wSYMBOL` (watch symbol)
  - Responds to `rSYMBOL` (unwatch symbol)
  - Returns fake 5-level order book

#### Usage

```bash
# Build
make fake_iqfeed

# Run with default ports (9100, 5009, 9200)
./fake_iqfeed

# Run with custom ports
./fake_iqfeed --lookup-port 19100 --level1-port 15009 --level2-port 19200
```

#### Protocol Example

```bash
# Connect to lookup server
nc localhost 9100

# Request daily data for AAPL (last 10 days)
HDX,AAPL,10

# Request 5-minute interval data for TSLA (last 100 bars)
HIX,TSLA,300,100
```

### 2. fake_tradezero - TradeZero API Mock Server

Simulates the TradeZero broker API with HTTP REST endpoints and WebSocket streams:

- **HTTP REST API (Port 8080)**:
  - `GET /v1/api/accounts/{accountId}/positions` - Get positions
  - `GET /v1/api/accounts/{accountId}/orders` - Get orders
  - `POST /v1/api/accounts/{accountId}/order` - Place order
  - `DELETE /v1/api/accounts/{accountId}/orders/{orderId}` - Cancel order

- **WebSocket Streams (Port 8081)**:
  - `/stream/pnl` - P&L and account updates
  - `/stream/portfolio` - Order and position updates

#### Usage

```bash
# Build
make fake_tradezero

# Run with default ports (HTTP: 8080, WebSocket: 8081)
./fake_tradezero

# Run with custom ports
./fake_tradezero --http-port 18080 --ws-port 18081
```

#### REST API Examples

```bash
# Get positions
curl http://localhost:8080/v1/api/accounts/test123/positions

# Get orders
curl http://localhost:8080/v1/api/accounts/test123/orders

# Place order
curl -X POST http://localhost:8080/v1/api/accounts/test123/order \
  -H "Content-Type: application/json" \
  -d '{"symbol":"AAPL","side":"buy","orderQuantity":100,"limitPrice":150.00}'

# Cancel order
curl -X DELETE http://localhost:8080/v1/api/accounts/test123/orders/ORD1000
```

#### WebSocket Example

The WebSocket implementation is simplified (sends JSON over raw TCP):

```bash
# Connect to P&L stream
nc localhost 8081

# You'll receive:
# 1. {"@system":true,"status":"PENDING_AUTH"}
# 2. Send auth: {"key":"test","secret":"test"}
# 3. Receive: {"@system":true,"status":"CONNECTED"}
# 4. Send subscribe: {"account":"test123"}
# 5. Receive initial snapshot and periodic updates
```

## Integration Testing

### Example: Testing IQFeed Connection

```cpp
#include "iqfeed_tcp.h"

int main() {
    IQFeedLookup lookup;

    // Connect to fake server instead of real IQFeed
    if (!lookup.connect("127.0.0.1", 9100)) {
        printf("Failed to connect\n");
        return 1;
    }

    lookup.set_callback([](const LookupResult& result) {
        if (result.success) {
            printf("Received %zu candles for %s\n",
                   result.candles.size(), result.symbol);
        }
    });

    lookup.fetch_daily("AAPL", 10);

    // Wait for async result
    safe_sleep_s(1);

    return 0;
}
```

### Example: Testing TradeZero Connection

```cpp
#include "tradezero_client.h"

int main() {
    TradeZeroClient client;
    client.set_credentials("test_key", "test_secret", "test_account");

    // Override base URL to point to fake server
    // (You'd need to add set_base_url() method)

    TZResponse resp = client.get_positions();
    if (resp.success) {
        printf("Positions: %s\n", resp.body.c_str());
    }

    return 0;
}
```

## Benefits

1. **No Credentials Required**: Run tests without real API keys
2. **Offline Testing**: No internet connection needed
3. **Fast**: No network latency or rate limits
4. **Deterministic**: Consistent test data every time
5. **Edge Cases**: Easy to simulate errors and unusual conditions
6. **CI/CD Friendly**: Run tests in automated pipelines

## Implementation Notes

### fake_iqfeed

- Uses simple TCP sockets (no encryption)
- Generates deterministic fake data based on timestamps
- Supports multiple concurrent clients
- Each client runs in its own thread

### fake_tradezero

- HTTP server parses basic HTTP/1.1 requests
- WebSocket is simplified (JSON over TCP, no WebSocket framing)
- Maintains in-memory order and position state
- Thread-safe access to shared state with mutex

## Limitations

1. **Simplified Protocols**:
   - WebSocket doesn't implement full WebSocket handshake/framing
   - HTTP parser is basic (no chunked encoding, etc.)

2. **No Authentication**:
   - Fake servers accept any credentials
   - No validation or security

3. **Fake Data**:
   - All market data is generated, not realistic
   - No actual order matching or fills

4. **Single-threaded Clients**:
   - Each client connection gets its own thread, but client itself is simple

## Future Enhancements

- [ ] Add command-line options to inject specific test scenarios
- [ ] Add WebSocket proper framing for more realistic testing
- [ ] Add configurable latency simulation
- [ ] Add error injection (random disconnects, timeouts)
- [ ] Add state persistence (save orders/positions to file)
- [ ] Add HTTP authentication headers validation
- [ ] Add market data replay from CSV files

## Troubleshooting

**Port already in use**:
```bash
# Check what's using the port
lsof -i :9100

# Use different ports
./fake_iqfeed --lookup-port 19100
```

**Connection refused**:
```bash
# Make sure server is running
ps aux | grep fake_iqfeed

# Check server logs for errors
./fake_iqfeed 2>&1 | tee server.log
```

**No data received**:
```bash
# Enable debug logging in your client
# Check that you're sending correct protocol commands
# Use nc/telnet to manually test the protocol
```
