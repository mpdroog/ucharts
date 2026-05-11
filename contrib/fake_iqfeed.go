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
func generateFakeL1Quote(symbol string) string {
	bid := 99.90 + rand.Float64()*0.10
	ask := bid + 0.05 + rand.Float64()*0.10
	last := bid + rand.Float64()*(ask-bid)
	volume := int64(1000 + rand.Intn(9000))

	return fmt.Sprintf("Q,%s,%.2f,100,%.2f,200,%.2f,%d,09:30:00",
		symbol, bid, ask, last, volume)
}

// generateFakeL2Update generates a fake Level 2 price level update
// Per IQFeed API spec (l2.txt), price level messages use:
// 7 = Price Level Summary, 8 = Price Level Update, 9 = Price Level Delete
// Format: 8,Symbol,Side,Price,Size,OrderCount,Time,Date
func generateFakeL2Update(symbol string) string {
	side := "B" // B=Bid, A=Ask per spec
	if rand.Intn(2) == 1 {
		side = "A"
	}

	price := 99.90 + rand.Float64()*0.20
	size := 100 * (1 + rand.Intn(10))
	orderCount := 1 + rand.Intn(5)
	timeStr := time.Now().Format("15:04:05")
	dateStr := time.Now().Format("2006-01-02")

	// Use message type 8 for updates
	return fmt.Sprintf("8,%s,%s,%.4f,%d,%d,%s,%s",
		symbol, side, price, size, orderCount, timeStr, dateStr)
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
					quote := generateFakeL1Quote(symbol)
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
				watchedSymbols[symbol] = true
				watchedMu.Unlock()
				logInfo("Level1: Watching %s", symbol)

				// Send initial quote
				quote := generateFakeL1Quote(symbol)
				if _, err := conn.Write([]byte(quote + "\r\n")); err != nil {
					logError("Level1: Failed to write initial quote for %s: %v", symbol, err)
					break
				}

			case 'r': // Unwatch
				watchedMu.Lock()
				delete(watchedSymbols, symbol)
				watchedMu.Unlock()
				logInfo("Level1: Unwatching %s", symbol)

			case 'S': // System
				if _, err := conn.Write([]byte("S,CURRENT PROTOCOL,6.2\r\n")); err != nil {
					logError("Level1: Failed to write system response: %v", err)
					break
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
					update := generateFakeL2Update(symbol)
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

		// Parse L2 commands - support both WPL/RPL format and legacy single-char
		parts := strings.Split(line, ",")
		var cmd string
		var symbol string

		if strings.HasPrefix(line, "WPL,") && len(parts) >= 2 {
			// WPL,SYMBOL[,MaxLevels] format per API spec
			cmd = "WPL"
			symbol = parts[1]
		} else if strings.HasPrefix(line, "RPL,") && len(parts) >= 2 {
			// RPL,SYMBOL format per API spec
			cmd = "RPL"
			symbol = parts[1]
		} else if len(line) > 1 {
			// Legacy single-char format
			cmd = string(line[0])
			symbol = strings.TrimSpace(line[1:])
		}

		switch cmd {
		case "WPL", "w": // Watch price levels
			watchedMu.Lock()
			watched[symbol] = true
			watchedMu.Unlock()
			logInfo("Level2: Watching %s", symbol)

			// Send initial book snapshot using message type 7 (Price Level Summary)
			// Format per API spec: 7,Symbol,Side,Price,Size,OrderCount,Time,Date
			timeStr := time.Now().Format("15:04:05")
			dateStr := time.Now().Format("2006-01-02")
			writeErr := false
			for i := 0; i < 5 && !writeErr; i++ {
				bidPrice := 99.90 - float64(i)*0.01
				askPrice := 100.00 + float64(i)*0.01
				size := 100 * (1 + rand.Intn(10))
				orderCount := 1 + rand.Intn(5)

				if _, err := conn.Write([]byte(fmt.Sprintf("7,%s,B,%.4f,%d,%d,%s,%s\r\n", symbol, bidPrice, size, orderCount, timeStr, dateStr))); err != nil {
					logError("Level2: Failed to write BID snapshot for %s: %v", symbol, err)
					writeErr = true
					break
				}
				if _, err := conn.Write([]byte(fmt.Sprintf("7,%s,A,%.4f,%d,%d,%s,%s\r\n", symbol, askPrice, size, orderCount, timeStr, dateStr))); err != nil {
					logError("Level2: Failed to write ASK snapshot for %s: %v", symbol, err)
					writeErr = true
					break
				}
			}
			if writeErr {
				break
			}

		case "RPL", "r": // Unwatch price levels
			watchedMu.Lock()
			delete(watched, symbol)
			watchedMu.Unlock()
			logInfo("Level2: Unwatching %s", symbol)

		case "S": // System
			if _, err := conn.Write([]byte("S,CURRENT PROTOCOL,6.2\r\n")); err != nil {
				logError("Level2: Failed to write system response: %v", err)
				break
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
	defer listener.Close()

	logInfo("%s server listening on port %d", name, port)

	go func() {
		<-ctx.Done()
		listener.Close()
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
