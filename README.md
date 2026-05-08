# ucharts - Professional Trading Platform

A professional trading platform built with Dear ImGui, GLFW, OpenGL 3, and SQLite.

## Features

- 4 ticker windows with Level 2 order book and Time & Sales
- Order entry with hotkeys
- Position management (open/closed positions)
- 3 synchronized charts (1min, 5min, daily)
- SQLite persistence for positions, orders, and settings
- File-based market data simulation

## Architecture

```
ucharts/
├── main.cpp              # Entry point, main loop, grid layout
├── types.h               # All data structures
├── database.h/.cpp       # SQLite wrapper
├── market_data.h/.cpp    # Level 2, Time & Sales, simulation
├── order_manager.h/.cpp  # Order execution, position tracking
├── chart_widget.h/.cpp   # Reusable chart component
├── ticker_widget.h/.cpp  # Level 2 + T&S + order entry
├── positions_widget.h/.cpp # Open/closed positions display
├── test_*.cpp            # Test files
├── Makefile
└── data/                 # Test data files
```

## UI Layout

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              14:32:57 (NYSE)                                │
├────────────┬───────────────────────────────────────────────────┬────────────┤
│            │  ┌───────────────────┐  ┌───────────────────┐     │            │
│  OPEN      │  │ [AAPL________]    │  │ [MSFT________]    │     │   1-MIN    │
│  POSITIONS │  │ Level2 | T&S      │  │ Level2 | T&S      │     │   CHART    │
│            │  │ [Qty][Price]      │  │ [Qty][Price]      │     │            │
│  Symbol    │  │ [BUY]    [SELL]   │  │ [BUY]    [SELL]   │     │  (selected │
│  Qty       │  └───────────────────┘  └───────────────────┘     │   ticker)  │
│  AvgPx     │  ┌───────────────────┐  ┌───────────────────┐     │            │
│  CurPx     │  │ [GOOGL_______]    │  │ [AMZN________]    │     ├────────────┤
│  P&L       │  │ Level2 | T&S      │  │ Level2 | T&S      │     │            │
│  P&L%      │  │ [Qty][Price]      │  │ [Qty][Price]      │     │   5-MIN    │
│            │  │ [BUY]    [SELL]   │  │ [BUY]    [SELL]   │     │   CHART    │
│ [Pending   │  └───────────────────┘  └───────────────────┘     │            │
│  w/ X btn] │                                                    │            │
├────────────┤  ┌────────────────────────────────────────────────┴────────────┤
│            │  │                                                              │
│  CLOSED    │  │                      DAILY CHART                             │
│  POSITIONS │  │                     (selected ticker)                        │
│            │  │                                                              │
└────────────┴──┴──────────────────────────────────────────────────────────────┘
```

**Proportions:** 15% positions | 55% tickers+daily | 30% 1m+5m charts

## Prerequisites

### macOS

```bash
brew install glfw sqlite
```

### Linux (Debian/Ubuntu)

```bash
sudo apt install libglfw3-dev libgl1-mesa-dev libsqlite3-dev
```

### Linux (Fedora)

```bash
sudo dnf install glfw-devel mesa-libGL-devel sqlite-devel
```

### Linux (Arch)

```bash
sudo pacman -S glfw-x11 mesa sqlite
```

## Build

```bash
# Build the application
make

# Run all tests
make test

# Clean build artifacts
make clean
```

The build will automatically download Dear ImGui if not present.

## Usage

```bash
./ucharts
```

## Hotkeys

| Key | Action |
|-----|--------|
| Shift+1 | Buy 100 shares at ask + $0.05 |
| Shift+2 | Buy 200 shares at ask + $0.05 |
| Shift+3 | Buy 500 shares at ask + $0.05 |
| Shift+4 | Buy 1000 shares at ask + $0.05 |
| Ctrl+1 | Sell 25% of position at bid - $0.05 |
| Ctrl+2 | Sell 50% of position at bid - $0.05 |
| Ctrl+3 | Sell 75% of position at bid - $0.05 |
| Ctrl+4 | Sell 100% of position at bid - $0.05 |
| Ctrl+C | Sell entire position at bid - $0.05 |
| Ctrl+Z | Cancel all pending orders |
| Escape | Exit fullscreen chart |
| Double-click chart | Toggle fullscreen |
| Left/Right arrows | Pan chart |
| Up/Down or +/- | Zoom chart |
| Home | Reset chart view |
| Delete | Remove selected drawing |

## Test Data Format

### Level 2 Data (data/level2_SYMBOL.csv)
```csv
timestamp,symbol,side,exchange,price,size
09:30:00.000,AAPL,BID,NYSE,150.00,1500
09:30:00.000,AAPL,ASK,ARCA,150.05,800
```

### Time & Sales Data (data/timesales_SYMBOL.csv)
```csv
timestamp,symbol,price,size,direction
09:30:00.456,AAPL,150.02,200,UP
```

### Candle Data (data/candles_SYMBOL_TIMEFRAME.csv)
```csv
timestamp,open,high,low,close,volume
2024-01-15 09:30,150.00,150.50,149.75,150.25,12500
```

## Data Structures

```cpp
// Level 2 order book entry
struct Level2Entry {
    char exchange[8];     // NYSE, ARCA, BATS, etc.
    float price;
    int size;             // In shares (display as size/1000)
    ImU32 color;          // For distinguishing price levels
};

// Time & Sales entry
struct TimeSalesEntry {
    char timestamp[16];   // HH:MM:SS.mmm
    float price;
    int size;
    int direction;        // 1=uptick, -1=downtick, 0=same
};

// Order
struct Order {
    int64_t id;
    char symbol[8];
    char side;            // 'B' or 'S'
    int quantity;
    int filled;
    float price;
    char status;          // 'P'=pending, 'F'=filled, 'X'=cancelled
    int64_t created_at;
};

// Position
struct Position {
    char symbol[8];
    int quantity;
    float avg_price;
    float current_price;
    float unrealized_pnl;
    float pnl_percent;
};
```

## Verification Checklist

### Infrastructure
- [ ] SQLite database creates on first run
- [ ] Last session tickers restored on startup
- [ ] Database survives app restart

### Market Data
- [ ] Level 2 shows 10 bid/10 ask levels
- [ ] Exchange names display correctly
- [ ] Size shows as thousands (1500 → 1.5)
- [ ] Time & Sales shows 15 rows
- [ ] T&S colors: green uptick, red downtick, yellow same
- [ ] T&S auto-scrolls to newest

### Order Entry
- [ ] Shift+1 buys 100 at ask+5c on selected ticker
- [ ] Shift+2/3/4 buy 200/500/1000
- [ ] Ctrl+1 sells 25% of position at bid-5c
- [ ] Ctrl+2/3/4 sell 50%/75%/100%
- [ ] Ctrl+C sells entire position
- [ ] Ctrl+Z cancels all pending orders
- [ ] Order appears in pending with X button
- [ ] Clicking X cancels the order

### Positions
- [ ] Open position shows after buy fills
- [ ] P&L calculates correctly
- [ ] P&L color: green positive, red negative
- [ ] Closed position appears after sell fills
- [ ] Position persists across restart

### Charts
- [ ] Click ticker → all 3 charts update
- [ ] Selected ticker has border highlight
- [ ] Double-click chart → fullscreen
- [ ] Escape or double-click → exit fullscreen
- [ ] Drawings persist per symbol
- [ ] Indicator settings persist per symbol
- [ ] 200 datapoint limit enforced

### Ticker Windows
- [ ] Symbol input shows current ticker or "Enter Symbol"
- [ ] Invalid symbol shows error
- [ ] First ticker selected by default
- [ ] Border highlights selected ticker

### Clock
- [ ] Shows NYSE time (America/New_York)
- [ ] 24-hour format with seconds
- [ ] Updates every second

### Layout
- [ ] Proportions: 15% / 55% / 30%
- [ ] No resize handles
- [ ] All panels visible
