// fake_tradezero.go - Mock TradeZero API server for integration testing
// Simulates TradeZero REST API and WebSocket streams
//
// Usage: go run fake_tradezero.go [--http-port 8080] [--ws-port 8081]
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/gorilla/websocket"
	"golang.org/x/text/cases"
	"golang.org/x/text/language"
)

// MockOrder represents an order in the mock system
type MockOrder struct {
	ClientOrderID string  `json:"clientOrderId"`
	Symbol        string  `json:"symbol"`
	Side          string  `json:"side"`
	OrderStatus   string  `json:"orderStatus"`
	OrderQuantity int     `json:"orderQuantity"`
	Executed      int     `json:"executed"`
	LimitPrice    float64 `json:"limitPrice"`
	PriceAvg      float64 `json:"priceAvg"`
}

// Global state
var (
	orders      []MockOrder
	ordersMu    sync.RWMutex
	nextOrderID = 1000

	// WebSocket clients for broadcasting events
	wsConns   []*wsConn
	wsConnsMu sync.RWMutex
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin:     func(r *http.Request) bool { return true },
}

func logInfo(format string, args ...interface{}) {
	ts := time.Now().Format("15:04:05")
	fmt.Printf("[%s][INFO] %s\n", ts, fmt.Sprintf(format, args...))
}

func logError(format string, args ...interface{}) {
	ts := time.Now().Format("15:04:05")
	fmt.Printf("[%s][ERROR] %s\n", ts, fmt.Sprintf(format, args...))
}

// REST API Handlers

func handlePositions(w http.ResponseWriter, r *http.Request) {
	logInfo("REST: GET %s", r.URL.Path)
	w.Header().Set("Content-Type", "application/json")
	// API returns array directly, not wrapped in object
	if _, err := w.Write([]byte(`[]`)); err != nil {
		logError("REST: Failed to write positions response: %v", err)
	}
}

func handleOrders(w http.ResponseWriter, r *http.Request) {
	logInfo("REST: GET %s", r.URL.Path)
	ordersMu.RLock()
	defer ordersMu.RUnlock()

	w.Header().Set("Content-Type", "application/json")
	// API returns array directly, not wrapped in object
	if err := json.NewEncoder(w).Encode(orders); err != nil {
		logError("REST: Failed to encode orders response: %v", err)
	}
}

func handlePlaceOrder(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	defer r.Body.Close()

	var req struct {
		Symbol      string  `json:"symbol"`
		Quantity    int     `json:"quantity"`
		Side        string  `json:"side"`
		OrderType   string  `json:"orderType"`
		LimitPrice  float64 `json:"limitPrice"`
		TimeInForce string  `json:"timeInForce"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		logError("Failed to parse order: %v", err)
		http.Error(w, "Invalid request", http.StatusBadRequest)
		return
	}

	logInfo("REST: POST order %s %d %s @ %.2f", req.Side, req.Quantity, req.Symbol, req.LimitPrice)

	ordersMu.Lock()
	orderID := fmt.Sprintf("ORD%d", nextOrderID)
	nextOrderID++

	order := MockOrder{
		ClientOrderID: orderID,
		Symbol:        req.Symbol,
		Side:          cases.Title(language.English).String(strings.ToLower(req.Side)),
		OrderStatus:   "Accepted",
		OrderQuantity: req.Quantity,
		Executed:      0,
		LimitPrice:    req.LimitPrice,
		PriceAvg:      req.LimitPrice, // Fill at limit price
	}
	orders = append(orders, order)
	ordersMu.Unlock()

	// Queue WebSocket event for Accepted status
	broadcastEvent(order)

	// Simulate fill after a short delay
	// Mock market: bid=99.95, ask=100.05
	// Buy fills if limit >= ask, Sell fills if limit <= bid
	go simulateFill(orderID, req.Side, req.LimitPrice, req.Quantity)

	w.Header().Set("Content-Type", "application/json")
	// Return full order object per API spec (tradezero-order.txt lines 99-111)
	resp := map[string]interface{}{
		"clientOrderId":  orderID,
		"symbol":         req.Symbol,
		"side":           req.Side,
		"orderType":      req.OrderType,
		"quantity":       req.Quantity,
		"filledQuantity": 0,
		"averagePrice":   0.0,
		"orderStatus":    "new",
		"timestamp":      time.Now().Format(time.RFC3339),
	}
	if err := json.NewEncoder(w).Encode(resp); err != nil {
		logError("REST: Failed to encode place order response: %v", err)
	}
}

// simulateFill checks if an order should fill based on mock market prices
func simulateFill(orderID, side string, limitPrice float64, qty int) {
	// Small delay to simulate market processing
	time.Sleep(50 * time.Millisecond)

	// Mock market prices (matching test data in test_integration.cpp)
	const mockBid = 100.00
	const mockAsk = 100.05

	shouldFill := false
	side = strings.ToLower(side)
	if side == "buy" && limitPrice >= mockAsk {
		shouldFill = true
	} else if side == "sell" && limitPrice <= mockBid {
		shouldFill = true
	}

	if !shouldFill {
		logInfo("REST: Order %s not filling (side=%s limit=%.2f bid=%.2f ask=%.2f)",
			orderID, side, limitPrice, mockBid, mockAsk)
		return
	}

	ordersMu.Lock()
	for i := range orders {
		if orders[i].ClientOrderID == orderID && orders[i].OrderStatus == "Accepted" {
			orders[i].OrderStatus = "Filled"
			orders[i].Executed = qty
			orders[i].PriceAvg = limitPrice
			logInfo("REST: Order %s filled at %.2f", orderID, limitPrice)
			broadcastEvent(orders[i])
			break
		}
	}
	ordersMu.Unlock()
}

func handleCancelOrder(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodDelete {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Extract order ID from path
	parts := strings.Split(r.URL.Path, "/")
	orderID := parts[len(parts)-1]

	logInfo("REST: DELETE order %s (have %d orders)", orderID, len(orders))

	ordersMu.Lock()
	var cancelledOrder *MockOrder

	// First try exact match
	for i := range orders {
		logInfo("REST: Checking order %s vs %s", orders[i].ClientOrderID, orderID)
		if orders[i].ClientOrderID == orderID {
			orders[i].OrderStatus = "Canceled"
			cancelledOrder = &orders[i]
			logInfo("REST: Found and cancelled order %s", orderID)
			break
		}
	}

	// If not found, try matching by symbol prefix (client generates BUY_SYMBOL_timestamp)
	if cancelledOrder == nil && (strings.HasPrefix(orderID, "BUY_") || strings.HasPrefix(orderID, "SELL_")) {
		// Extract symbol from orderID (format: BUY_SYMBOL_timestamp or SELL_SYMBOL_timestamp)
		parts := strings.SplitN(orderID, "_", 3)
		if len(parts) >= 2 {
			symbol := parts[1]
			side := "Buy"
			if parts[0] == "SELL" {
				side = "Sell"
			}
			// Find first matching pending order
			for i := range orders {
				if orders[i].Symbol == symbol && orders[i].Side == side &&
					orders[i].OrderStatus != "Canceled" && orders[i].OrderStatus != "Filled" {
					orders[i].OrderStatus = "Canceled"
					orders[i].ClientOrderID = orderID // Update to client's ID
					cancelledOrder = &orders[i]
					logInfo("REST: Matched order by symbol/side: %s %s", symbol, side)
					break
				}
			}
		}
	}
	ordersMu.Unlock()

	if cancelledOrder != nil {
		broadcastEvent(*cancelledOrder)
		w.Header().Set("Content-Type", "application/json")
		if err := json.NewEncoder(w).Encode(map[string]bool{"success": true}); err != nil {
			logError("REST: Failed to encode cancel order response: %v", err)
		}
	} else {
		http.Error(w, "Order not found", http.StatusNotFound)
	}
}

func handleCancelAllOrders(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodDelete {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	logInfo("REST: DELETE all orders")

	ordersMu.Lock()
	for i := range orders {
		if orders[i].OrderStatus != "Canceled" && orders[i].OrderStatus != "Filled" {
			orders[i].OrderStatus = "Canceled"
			broadcastEvent(orders[i])
		}
	}
	ordersMu.Unlock()

	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(map[string]bool{"success": true}); err != nil {
		logError("REST: Failed to encode cancel all orders response: %v", err)
	}
}

// handleReset clears all orders (for testing)
func handleReset(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	ordersMu.Lock()
	oldOrderCount := len(orders)
	orders = nil
	// Don't reset nextOrderID - keep incrementing to avoid event confusion between tests
	// nextOrderID = 1000
	ordersMu.Unlock()

	logInfo("REST: POST reset - cleared %d orders", oldOrderCount)

	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(map[string]bool{"success": true}); err != nil {
		logError("REST: Failed to encode reset response: %v", err)
	}
}

// broadcastEvent sends an order event to all connected WebSocket clients
// Matches real TradeZero API format from docs/tradezero-websocket.txt
func broadcastEvent(order MockOrder) {
	// Order data is nested inside "order" object per API spec
	orderData := map[string]interface{}{
		"accountId":       "test_account",
		"clientOrderId":   order.ClientOrderID,
		"symbol":          order.Symbol,
		"tradedSymbol":    order.Symbol,
		"side":            order.Side,
		"orderStatus":     order.OrderStatus,
		"orderType":       "Limit",
		"orderQuantity":   order.OrderQuantity,
		"executed":        order.Executed,
		"leavesQuantity":  order.OrderQuantity - order.Executed,
		"canceledQuantity": 0,
		"limitPrice":      order.LimitPrice,
		"priceAvg":        order.PriceAvg,
		"priceStop":       0.0,
		"lastPrice":       order.PriceAvg,
		"lastQuantity":    order.Executed,
		"route":           "SIM",
		"securityType":    "Stock",
		"timeInForce":     "Day",
		"openClose":       "Open",
		"startTime":       time.Now().Format(time.RFC3339),
		"lastUpdated":     time.Now().Format(time.RFC3339),
	}

	event := map[string]interface{}{
		"ts":           time.Now().UnixMilli(),
		"accountId":    "test_account",
		"action":       "update",
		"subscription": "Order",
		"order":        orderData,
	}

	wsConnsMu.RLock()
	defer wsConnsMu.RUnlock()

	for _, wsc := range wsConns {
		if wsc.queueSend(event) {
			logInfo("WS: Broadcast order event for %s (%s) to client", order.ClientOrderID, order.OrderStatus)
		}
	}
}

// wsConn wraps a websocket connection with thread-safe state tracking
type wsConn struct {
	conn     *websocket.Conn
	mu       sync.Mutex
	closed   bool
	sendChan chan interface{} // Channel for sending messages
	doneChan chan struct{}    // Signal when connection is done
}

func newWsConn(conn *websocket.Conn) *wsConn {
	return &wsConn{
		conn:     conn,
		sendChan: make(chan interface{}, 100),
		doneChan: make(chan struct{}),
	}
}

func (w *wsConn) isClosed() bool {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.closed
}

func (w *wsConn) setClosed() {
	w.mu.Lock()
	alreadyClosed := w.closed
	w.closed = true
	w.mu.Unlock()

	// Signal done only once
	if !alreadyClosed {
		close(w.doneChan)
	}
}

func (w *wsConn) writeJSON(v interface{}) error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if w.closed {
		return fmt.Errorf("connection closed")
	}

	if err := w.conn.WriteJSON(v); err != nil {
		logError("WS: WriteJSON failed: %v", err)
		w.closed = true
		return err
	}
	return nil
}

// sendLoop runs in a separate goroutine to send messages
func (w *wsConn) sendLoop() {
	for {
		select {
		case <-w.doneChan:
			return
		case msg := <-w.sendChan:
			if err := w.writeJSON(msg); err != nil {
				logError("WS: sendLoop write failed: %v", err)
				return
			}
		}
	}
}

// queueSend queues a message to be sent
func (w *wsConn) queueSend(v interface{}) bool {
	select {
	case <-w.doneChan:
		return false
	case w.sendChan <- v:
		return true
	default:
		logError("WS: Send channel full, dropping message")
		return false
	}
}

// WebSocket Handler

func handleWebSocket(w http.ResponseWriter, r *http.Request) {
	path := r.URL.Path
	logInfo("WS: New connection to %s", path)

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		logError("WS upgrade failed: %v", err)
		return
	}

	wsc := newWsConn(conn)

	// Recover from any panics in gorilla/websocket
	defer func() {
		if r := recover(); r != nil {
			logError("WS: Recovered from panic: %v", r)
		}
		wsc.setClosed()
		conn.Close()
		unregisterWsConn(wsc)
		logInfo("WS: Client disconnected from %s", path)
	}()

	// Set close handler to detect when client closes connection
	conn.SetCloseHandler(func(code int, text string) error {
		logInfo("WS: Client sent close frame: code=%d text=%s", code, text)
		wsc.setClosed()
		return nil
	})

	// Start the send goroutine
	go wsc.sendLoop()

	// Send initial system message
	if err := wsc.writeJSON(map[string]interface{}{
		"@system": true,
		"status":  "PENDING_AUTH",
	}); err != nil {
		logError("WS: Failed to send PENDING_AUTH: %v", err)
		return
	}

	if err := runWebSocketLoop(wsc, path); err != nil {
		logError("WS: Connection ended: %v", err)
	}
}

func runWebSocketLoop(wsc *wsConn, path string) error {
	authenticated := false
	subscribed := false

	for {
		// Check if connection was closed
		if wsc.isClosed() {
			logInfo("WS: Connection marked closed, exiting loop")
			return nil
		}

		// Use a reasonable read timeout (30 seconds for keepalive)
		// Use longer timeout for mock server (5 minutes) - tests may have gaps between operations
		wsc.conn.SetReadDeadline(time.Now().Add(5 * time.Minute))
		_, msg, err := wsc.conn.ReadMessage()
		if err != nil {
			// Mark connection as closed first to stop other goroutines
			wsc.setClosed()

			// Check for normal close
			if websocket.IsCloseError(err, websocket.CloseNormalClosure, websocket.CloseGoingAway) {
				logInfo("WS: Client disconnected normally")
				return nil
			}

			// Timeout is also a normal close for our purposes
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				logInfo("WS: Read timeout, closing connection")
				return nil
			}

			// Log and return any other error
			logError("WS: Read error: %v", err)
			return fmt.Errorf("read error: %w", err)
		}

		logInfo("WS: Received: %s", string(msg))

		var data map[string]interface{}
		if err := json.Unmarshal(msg, &data); err != nil {
			logError("WS: Invalid JSON: %v", err)
			continue
		}

		// Handle authentication
		if !authenticated {
			if _, ok := data["key"]; ok {
				authenticated = true
				if err := wsc.writeJSON(map[string]interface{}{
					"@system": true,
					"status":  "CONNECTED",
				}); err != nil {
					return fmt.Errorf("failed to send CONNECTED: %w", err)
				}
				logInfo("WS: Authenticated")
			}
			continue
		}

		// Handle subscription
		if !subscribed {
			if _, ok := data["accountId"]; ok {
				subscribed = true
				logInfo("WS: Subscribed to %s", path)

				// Register client for broadcast events
				registerWsConn(wsc)
			}
		}
		// Events are broadcast directly via broadcastEvent(), no polling needed
	}
}

func registerWsConn(wsc *wsConn) {
	wsConnsMu.Lock()
	defer wsConnsMu.Unlock()
	wsConns = append(wsConns, wsc)
	logInfo("WS: Registered client (total: %d)", len(wsConns))
}

func unregisterWsConn(wsc *wsConn) {
	wsConnsMu.Lock()
	defer wsConnsMu.Unlock()
	for i, c := range wsConns {
		if c == wsc {
			wsConns = append(wsConns[:i], wsConns[i+1:]...)
			logInfo("WS: Unregistered client (remaining: %d)", len(wsConns))
			return
		}
	}
}

func main() {
	httpPort := flag.Int("http-port", 8080, "HTTP REST API port")
	wsPort := flag.Int("ws-port", 8081, "WebSocket server port")
	flag.Parse()

	logInfo("Starting fake TradeZero server")
	logInfo("HTTP REST API: http://localhost:%d", *httpPort)
	logInfo("WebSocket: ws://localhost:%d", *wsPort)

	// Setup signal handling
	ctx, cancel := context.WithCancel(context.Background())
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		<-sigChan
		logInfo("Shutdown signal received")
		cancel()
	}()

	// HTTP REST API server
	httpMux := http.NewServeMux()
	httpMux.HandleFunc("/v1/api/reset", handleReset)  // Test endpoint to clear state
	httpMux.HandleFunc("/v1/api/accounts/", func(w http.ResponseWriter, r *http.Request) {
		path := r.URL.Path
		if strings.HasSuffix(path, "/positions") {
			handlePositions(w, r)
		} else if strings.HasSuffix(path, "/orders") {
			if r.Method == http.MethodDelete {
				handleCancelAllOrders(w, r)
			} else {
				handleOrders(w, r)
			}
		} else if strings.Contains(path, "/order") {
			if r.Method == http.MethodDelete {
				handleCancelOrder(w, r)
			} else {
				handlePlaceOrder(w, r)
			}
		} else {
			http.NotFound(w, r)
		}
	})

	httpServer := &http.Server{
		Addr:         fmt.Sprintf(":%d", *httpPort),
		Handler:      httpMux,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 10 * time.Second,
	}

	// WebSocket server
	wsMux := http.NewServeMux()
	wsMux.HandleFunc("/stream/portfolio", handleWebSocket)
	wsMux.HandleFunc("/stream/pnl", handleWebSocket)

	wsServer := &http.Server{
		Addr:         fmt.Sprintf(":%d", *wsPort),
		Handler:      wsMux,
		ReadTimeout:  60 * time.Second,
		WriteTimeout: 60 * time.Second,
	}

	// Start servers
	go func() {
		logInfo("HTTP server listening on port %d", *httpPort)
		if err := httpServer.ListenAndServe(); err != http.ErrServerClosed {
			log.Fatalf("HTTP server error: %v", err)
		}
	}()

	go func() {
		logInfo("WebSocket server listening on port %d", *wsPort)
		if err := wsServer.ListenAndServe(); err != http.ErrServerClosed {
			log.Fatalf("WebSocket server error: %v", err)
		}
	}()

	// Wait for shutdown
	<-ctx.Done()

	// Graceful shutdown
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer shutdownCancel()

	if err := httpServer.Shutdown(shutdownCtx); err != nil {
		logError("HTTP server shutdown error: %v", err)
	}
	if err := wsServer.Shutdown(shutdownCtx); err != nil {
		logError("WebSocket server shutdown error: %v", err)
	}

	logInfo("Server stopped")
}
