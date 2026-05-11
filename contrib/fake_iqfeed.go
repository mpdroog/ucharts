// fake_iqfeed.go - Mock IQFeed server for integration testing
// Simulates IQFeed Lookup (port 9100), Level1 (port 5009), and Level2 (port 9200) servers
//
// Usage: go run fake_iqfeed.go [--lookup-port 9100] [--level1-port 5009] [--level2-port 9200]
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"io"
	"math/rand"
	"net"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"
)

func logInfo(format string, args ...interface{}) {
	ts := time.Now().Format("15:04:05")
	fmt.Printf("[%s][INFO] %s\n", ts, fmt.Sprintf(format, args...))
}

func logError(format string, args ...interface{}) {
	ts := time.Now().Format("15:04:05")
	fmt.Printf("[%s][ERROR] %s\n", ts, fmt.Sprintf(format, args...))
}

// generateFakeCandles generates fake daily candle data
// Per IQFeed API spec (hist.txt), daily data format is:
// LH,Date,High,Low,Open,Close,PeriodVolume,OpenInterest
func generateFakeCandles(symbol string, count int) []string {
	var lines []string
	basePrice := 100.0 + rand.Float64()*50.0

	for i := count - 1; i >= 0; i-- {
		date := time.Now().AddDate(0, 0, -i)
		open := basePrice + (rand.Float64()-0.5)*5
		high := open + rand.Float64()*3
		low := open - rand.Float64()*3
		close := low + rand.Float64()*(high-low)
		volume := int64(10000 + rand.Intn(90000))

		// LH prefix required per API spec
		line := fmt.Sprintf("LH,%s,%.2f,%.2f,%.2f,%.2f,%d,0",
			date.Format("2006-01-02"),
			high, low, open, close, volume)
		lines = append(lines, line)

		basePrice = close
	}
	return lines
}

// generateFakeL1Quote generates a fake Level 1 quote
// msgType: 'P' for summary (initial), 'Q' for update (streaming)
// Format matches S,SELECT UPDATE FIELDS response order:
// Symbol,Last,Last Size,Last Time,Total Volume,Bid,Bid Size,Ask,Ask Size,High,Low,Open,Close
func generateFakeL1Quote(symbol string, msgType byte) string {
	bid := 99.90 + rand.Float64()*0.10
	ask := bid + 0.05 + rand.Float64()*0.10
	last := bid + rand.Float64()*(ask-bid)
	lastSize := 100 * (1 + rand.Intn(10))
	volume := int64(100000 + rand.Intn(900000))
	high := last + rand.Float64()*2
	low := last - rand.Float64()*2
	open := low + rand.Float64()*(high-low)
	closePrice := open + (rand.Float64()-0.5)*1
	timeStr := time.Now().Format("15:04:05.000000")

	return fmt.Sprintf("%c,%s,%.2f,%d,%s,%d,%.2f,%d,%.2f,%d,%.2f,%.2f,%.2f,%.2f",
		msgType, symbol, last, lastSize, timeStr, volume,
		bid, 100, ask, 200, high, low, open, closePrice)
}

// generateFakeL2Order generates a fake Level 2 order message (for equities)
// Per IQFeed API spec, order messages use:
// 6 = Order Summary, 3 = Order Add, 4 = Order Update, 5 = Order Delete
// Format: 6,Symbol,OrderID,MMID,Side,Price,Size,Priority,Precision,Time,Date
// For equities, OrderID is typically empty and MMID contains market maker ID
func generateFakeL2Order(symbol string, msgType byte) string {
	side := "B" // B=Bid, A=Ask
	if rand.Intn(2) == 1 {
		side = "A"
	}

	// Generate fake market maker IDs
	mmids := []string{"NSDQ", "ARCA", "BATS", "EDGX", "NYSE", "AMEX"}
	mmid := mmids[rand.Intn(len(mmids))]

	price := 99.90 + rand.Float64()*0.20
	size := 100 * (1 + rand.Intn(10))
	priority := rand.Intn(1000)
	precision := 4
	timeStr := time.Now().Format("15:04:05.000")
	dateStr := time.Now().Format("2006-01-02")

	// OrderID is empty for equity L2, MMID identifies the source
	return fmt.Sprintf("%c,%s,,%s,%s,%.4f,%d,%d,%d,%s,%s",
		msgType, symbol, mmid, side, price, size, priority, precision, timeStr, dateStr)
}

// handleLookupClient handles a connection to the Lookup port
func handleLookupClient(conn net.Conn, ctx context.Context) {
	defer conn.Close()
	logInfo("Lookup: Client connected from %s", conn.RemoteAddr())

	reader := bufio.NewReader(conn)

	for {
		select {
		case <-ctx.Done():
			return
		default:
		}

		conn.SetReadDeadline(time.Now().Add(1 * time.Second))
		line, err := reader.ReadString('\n')
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			if err != io.EOF {
				logError("Lookup: Read error: %v", err)
			}
			break
		}

		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		logInfo("Lookup: Received: %s", line)

		// Parse command
		parts := strings.Split(line, ",")
		if len(parts) < 1 {
			continue
		}

		cmd := parts[0]

		switch {
		case cmd == "HDX": // Historical daily data
			if len(parts) >= 3 {
				symbol := parts[1]
				logInfo("Lookup: HDX request for %s", symbol)

				// Send header
				if _, err := conn.Write([]byte("LH,TimeStamp,High,Low,Open,Close,PeriodVolume,OpenInterest\r\n")); err != nil {
					logError("Lookup: Failed to write HDX header: %v", err)
					return
				}

				// Send fake candles
				candles := generateFakeCandles(symbol, 30)
				for _, candle := range candles {
					if _, err := conn.Write([]byte(candle + "\r\n")); err != nil {
						logError("Lookup: Failed to write HDX candle: %v", err)
						return
					}
				}

				// End marker
				if _, err := conn.Write([]byte("!ENDMSG!,\r\n")); err != nil {
					logError("Lookup: Failed to write HDX end marker: %v", err)
					return
				}
			}

		case cmd == "HIX": // Historical interval data
			if len(parts) >= 3 {
				symbol := parts[1]
				logInfo("Lookup: HIX request for %s", symbol)

				// Send header
				if _, err := conn.Write([]byte("LH,TimeStamp,High,Low,Open,Close,TotalVolume,PeriodVolume\r\n")); err != nil {
					logError("Lookup: Failed to write HIX header: %v", err)
					return
				}

				// Send fake minute candles
				basePrice := 100.0
				for i := 0; i < 60; i++ {
					ts := time.Now().Add(time.Duration(-60+i) * time.Minute)
					open := basePrice + (rand.Float64()-0.5)*0.5
					high := open + rand.Float64()*0.3
					low := open - rand.Float64()*0.3
					close := low + rand.Float64()*(high-low)
					volume := int64(1000 + rand.Intn(5000))

					line := fmt.Sprintf("LH,%s,%.2f,%.2f,%.2f,%.2f,%d,%d",
						ts.Format("2006-01-02 15:04:05"),
						high, low, open, close, volume, volume)
					if _, err := conn.Write([]byte(line + "\r\n")); err != nil {
						logError("Lookup: Failed to write HIX candle: %v", err)
						return
					}

					basePrice = close
				}

				if _, err := conn.Write([]byte("!ENDMSG!,\r\n")); err != nil {
					logError("Lookup: Failed to write HIX end marker: %v", err)
					return
				}
			}

		case cmd == "S": // System command
			logInfo("Lookup: System command")
			if _, err := conn.Write([]byte("S,CURRENT PROTOCOL,6.2\r\n")); err != nil {
				logError("Lookup: Failed to write system response: %v", err)
				return
			}

		default:
			logInfo("Lookup: Unknown command: %s", cmd)
		}
	}

	logInfo("Lookup: Client disconnected")
}

// handleLevel1Client handles a connection to the Level1 port
func handleLevel1Client(conn net.Conn, ctx context.Context) {
	defer conn.Close()
	logInfo("Level1: Client connected from %s", conn.RemoteAddr())

	// Per API spec (level1.txt), send initialization messages on connect:
	// S,KEY, S,SERVER CONNECTED, S,IP, S,CUST
	initMsgs := []string{
		"S,KEY,12345\r\n",
		"S,SERVER CONNECTED\r\n",
		"S,IP,127.0.0.1 5009\r\n",
		"S,CUST,TESTUSER,0,0,0,0,0,0,0\r\n",
	}
	for _, msg := range initMsgs {
		if _, err := conn.Write([]byte(msg)); err != nil {
			logError("Level1: Failed to send init message: %v", err)
			return
		}
	}

	reader := bufio.NewReader(conn)

	// Per-connection watched symbols with mutex for thread safety
	var watchedSymbols = make(map[string]bool)
	var watchedMu sync.RWMutex

	// Start quote updater goroutine
	quoteChan := make(chan struct{})
	go func() {
		ticker := time.NewTicker(1 * time.Second)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-quoteChan:
				return
			case <-ticker.C:
				watchedMu.RLock()
				symbols := make([]string, 0, len(watchedSymbols))
				for symbol := range watchedSymbols {
					symbols = append(symbols, symbol)
				}
				watchedMu.RUnlock()
				for _, symbol := range symbols {
					quote := generateFakeL1Quote(symbol, 'Q') // Q for streaming updates
					if _, err := conn.Write([]byte(quote + "\r\n")); err != nil {
						logError("Level1: Failed to write quote for %s: %v", symbol, err)
						return
					}
				}
			}
		}
	}()

	for {
		select {
		case <-ctx.Done():
			close(quoteChan)
			return
		default:
		}

		conn.SetReadDeadline(time.Now().Add(1 * time.Second))
		line, err := reader.ReadString('\n')
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			if err != io.EOF {
				logError("Level1: Read error: %v", err)
			}
			break
		}

		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		logInfo("Level1: Received: %s", line)

		// Parse watch/unwatch commands
		if len(line) > 1 {
			cmd := line[0]
			symbol := strings.TrimSpace(line[1:])

			switch cmd {
			case 'w': // Watch
				watchedMu.Lock()
				watchedSymbols[strings.ToUpper(symbol)] = true
				watchedMu.Unlock()
				logInfo("Level1: Watching %s", symbol)

				// Send initial quote (P = summary message)
				quote := generateFakeL1Quote(strings.ToUpper(symbol), 'P')
				if _, err := conn.Write([]byte(quote + "\r\n")); err != nil {
					logError("Level1: Failed to write initial quote for %s: %v", symbol, err)
					break
				}

			case 'r': // Unwatch
				watchedMu.Lock()
				delete(watchedSymbols, symbol)
				watchedMu.Unlock()
				logInfo("Level1: Unwatching %s", symbol)

			case 'S': // System command
				if strings.HasPrefix(symbol, ",SET PROTOCOL") {
					if _, err := conn.Write([]byte("S,CURRENT PROTOCOL,6.2\r\n")); err != nil {
						logError("Level1: Failed to write protocol response: %v", err)
						break
					}
				} else if strings.HasPrefix(symbol, ",SELECT UPDATE FIELDS") {
					// Respond with field names matching the requested fields
					fieldNames := "S,CURRENT UPDATE FIELDNAMES,Symbol,Last,Last Size,Last Time,Total Volume,Bid,Bid Size,Ask,Ask Size,High,Low,Open,Close\r\n"
					if _, err := conn.Write([]byte(fieldNames)); err != nil {
						logError("Level1: Failed to write field names: %v", err)
						break
					}
					logInfo("Level1: Sent field names response")
				} else {
					// Generic system response
					if _, err := conn.Write([]byte("S,CURRENT PROTOCOL,6.2\r\n")); err != nil {
						logError("Level1: Failed to write system response: %v", err)
						break
					}
				}
			}
		}
	}

	close(quoteChan)
	logInfo("Level1: Client disconnected")
}

// handleLevel2Client handles a connection to the Level2 port
func handleLevel2Client(conn net.Conn, ctx context.Context) {
	defer conn.Close()
	logInfo("Level2: Client connected from %s", conn.RemoteAddr())

	reader := bufio.NewReader(conn)

	// Per-connection watched symbols with mutex for thread safety
	var watched = make(map[string]bool)
	var watchedMu sync.RWMutex

	// Start L2 updater goroutine
	updateChan := make(chan struct{})
	go func() {
		ticker := time.NewTicker(500 * time.Millisecond)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-updateChan:
				return
			case <-ticker.C:
				watchedMu.RLock()
				symbols := make([]string, 0, len(watched))
				for symbol := range watched {
					symbols = append(symbols, symbol)
				}
				watchedMu.RUnlock()
				for _, symbol := range symbols {
					// Use order-based updates (message type 3 = Order Add)
					update := generateFakeL2Order(symbol, '3')
					if _, err := conn.Write([]byte(update + "\r\n")); err != nil {
						logError("Level2: Failed to write update for %s: %v", symbol, err)
						return
					}
				}
			}
		}
	}()

	for {
		select {
		case <-ctx.Done():
			close(updateChan)
			return
		default:
		}

		conn.SetReadDeadline(time.Now().Add(1 * time.Second))
		line, err := reader.ReadString('\n')
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			if err != io.EOF {
				logError("Level2: Read error: %v", err)
			}
			break
		}

		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		logInfo("Level2: Received: %s", line)

		// Parse L2 commands - support WOR/ROR (orders) and WPL/RPL (price levels)
		parts := strings.Split(line, ",")
		var cmd string
		var symbol string

		if strings.HasPrefix(line, "WOR,") && len(parts) >= 2 {
			// WOR,SYMBOL - Watch Orders (for equities/Nasdaq L2)
			cmd = "WOR"
			symbol = strings.ToUpper(parts[1])
		} else if strings.HasPrefix(line, "ROR,") && len(parts) >= 2 {
			// ROR,SYMBOL - Remove Order watch
			cmd = "ROR"
			symbol = strings.ToUpper(parts[1])
		} else if strings.HasPrefix(line, "WPL,") && len(parts) >= 2 {
			// WPL,SYMBOL[,MaxLevels] - Watch Price Levels (for futures)
			cmd = "WPL"
			symbol = strings.ToUpper(parts[1])
		} else if strings.HasPrefix(line, "RPL,") && len(parts) >= 2 {
			// RPL,SYMBOL - Remove Price Level watch
			cmd = "RPL"
			symbol = strings.ToUpper(parts[1])
		} else if len(line) > 1 {
			// Legacy single-char format
			cmd = string(line[0])
			symbol = strings.ToUpper(strings.TrimSpace(line[1:]))
		}

		switch cmd {
		case "WOR", "WPL", "w": // Watch orders or price levels
			watchedMu.Lock()
			watched[symbol] = true
			watchedMu.Unlock()
			logInfo("Level2: Watching %s (cmd=%s)", symbol, cmd)

			// Send initial book snapshot using order format (message type 6 = Order Summary)
			// Format: 6,Symbol,OrderID,MMID,Side,Price,Size,Priority,Precision,Time,Date
			// For equities, OrderID is empty and MMID contains market maker ID
			writeErr := false
			mmids := []string{"NSDQ", "ARCA", "BATS", "EDGX", "NYSE"}
			for i := 0; i < 5 && !writeErr; i++ {
				bidPrice := 99.90 - float64(i)*0.01
				askPrice := 100.00 + float64(i)*0.01
				size := 100 * (1 + rand.Intn(10))
				mmid := mmids[i%len(mmids)]
				priority := i
				precision := 4
				timeStr := time.Now().Format("15:04:05.000")
				dateStr := time.Now().Format("2006-01-02")

				// Bid order summary
				if _, err := conn.Write([]byte(fmt.Sprintf("6,%s,,%s,B,%.4f,%d,%d,%d,%s,%s\r\n",
					symbol, mmid, bidPrice, size, priority, precision, timeStr, dateStr))); err != nil {
					logError("Level2: Failed to write BID snapshot for %s: %v", symbol, err)
					writeErr = true
					break
				}
				// Ask order summary
				if _, err := conn.Write([]byte(fmt.Sprintf("6,%s,,%s,A,%.4f,%d,%d,%d,%s,%s\r\n",
					symbol, mmid, askPrice, size, priority, precision, timeStr, dateStr))); err != nil {
					logError("Level2: Failed to write ASK snapshot for %s: %v", symbol, err)
					writeErr = true
					break
				}
			}
			if writeErr {
				break
			}

		case "ROR", "RPL", "r": // Unwatch orders or price levels
			watchedMu.Lock()
			delete(watched, symbol)
			watchedMu.Unlock()
			logInfo("Level2: Unwatching %s", symbol)

		case "S": // System command
			if strings.HasPrefix(line, "S,SET PROTOCOL") {
				// Send protocol confirmation followed by server connected
				if _, err := conn.Write([]byte("S,CURRENT PROTOCOL,6.2\r\n")); err != nil {
					logError("Level2: Failed to write protocol response: %v", err)
					break
				}
				if _, err := conn.Write([]byte("S,SERVER CONNECTED\r\n")); err != nil {
					logError("Level2: Failed to write server connected: %v", err)
					break
				}
			} else {
				if _, err := conn.Write([]byte("S,CURRENT PROTOCOL,6.2\r\n")); err != nil {
					logError("Level2: Failed to write system response: %v", err)
					break
				}
			}
		}
	}

	close(updateChan)
	logInfo("Level2: Client disconnected")
}

func runTCPServer(ctx context.Context, wg *sync.WaitGroup, port int, name string, handler func(net.Conn, context.Context)) {
	listener, err := net.Listen("tcp", fmt.Sprintf(":%d", port))
	if err != nil {
		logError("Failed to start %s server on port %d: %v", name, port, err)
		return
	}

	// Use sync.Once to ensure listener is closed exactly once
	var closeOnce sync.Once
	closeListener := func() {
		closeOnce.Do(func() {
			listener.Close()
		})
	}
	defer closeListener()

	logInfo("%s server listening on port %d", name, port)

	go func() {
		<-ctx.Done()
		closeListener() // Unblock Accept() on context cancellation
	}()

	for {
		conn, err := listener.Accept()
		if err != nil {
			select {
			case <-ctx.Done():
				return
			default:
				logError("%s: Accept error: %v", name, err)
				continue
			}
		}

		wg.Add(1)
		go func(c net.Conn) {
			defer wg.Done()
			handler(c, ctx)
		}(conn)
	}
}

func main() {
	lookupPort := flag.Int("lookup-port", 9100, "Lookup server port")
	level1Port := flag.Int("level1-port", 5009, "Level1 server port")
	level2Port := flag.Int("level2-port", 9200, "Level2 server port")
	flag.Parse()

	// Note: rand is auto-seeded in Go 1.20+

	logInfo("Starting fake IQFeed server")
	logInfo("Lookup port: %d", *lookupPort)
	logInfo("Level1 port: %d", *level1Port)
	logInfo("Level2 port: %d", *level2Port)

	// Setup signal handling
	ctx, cancel := context.WithCancel(context.Background())
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		<-sigChan
		logInfo("Shutdown signal received")
		cancel()
	}()

	// WaitGroup to track handler goroutines
	var wg sync.WaitGroup

	// Start servers
	go runTCPServer(ctx, &wg, *lookupPort, "Lookup", handleLookupClient)
	go runTCPServer(ctx, &wg, *level1Port, "Level1", handleLevel1Client)
	go runTCPServer(ctx, &wg, *level2Port, "Level2", handleLevel2Client)

	// Wait for shutdown signal
	<-ctx.Done()

	// Wait for all handlers to finish (with timeout)
	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()

	select {
	case <-done:
		logInfo("All handlers finished")
	case <-time.After(2 * time.Second):
		logInfo("Timeout waiting for handlers, forcing shutdown")
	}

	logInfo("Server stopped")
}
