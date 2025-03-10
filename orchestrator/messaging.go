// zeromq_message_service.go
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	zmq "github.com/pebbe/zmq4"
	"github.com/spf13/viper"
)

// MessageBroker manages message distribution between simulation components
type MessageBroker struct {
	// ZMQ context
	zmqContext *zmq.Context

	// ZMQ sockets
	pubSocket    *zmq.Socket // For publishing messages
	subSocket    *zmq.Socket // For subscribing to messages
	routerSocket *zmq.Socket // For request-reply patterns

	// Configuration
	pubEndpoint    string
	subEndpoint    string
	routerEndpoint string

	// WebSocket connections for UI/client updates
	wsClients      map[*websocket.Conn]bool
	wsClientsMutex sync.RWMutex
	wsUpgrader     websocket.Upgrader

	// Message channels
	marketDataChan    chan MarketDataMessage
	systemStatusChan  chan SystemStatusMessage
	executionDataChan chan ExecutionMessage
	commandChan       chan CommandMessage

	// Topic mappings
	topicMapping map[string]string

	// Running flag
	running bool
	mutex   sync.RWMutex
}

// ConfigService manages configuration and discovery for simulation components
type ConfigService struct {
	// Configuration store
	viper *viper.Viper

	// Node registry
	nodes      map[string]*NodeInfo
	nodesMutex sync.RWMutex

	// Active configuration
	activeConfig     *SimulationConfig
	activeConfigLock sync.RWMutex
}

// NodeInfo contains information about a simulation node
type NodeInfo struct {
	ID           string
	Address      string
	Status       string
	LastSeen     time.Time
	Capabilities []string
	Load         float64

	// Resources and capacity
	NumCPU       int
	MemoryGB     float64
	MaxActors    int
	MaxExchanges int
}

// Message types

// MarketDataMessage contains an update to market data
type MarketDataMessage struct {
	ExchangeID   string       `json:"exchange_id"`
	InstrumentID string       `json:"instrument_id"`
	Timestamp    time.Time    `json:"timestamp"`
	BestBid      float64      `json:"best_bid"`
	BestAsk      float64      `json:"best_ask"`
	LastPrice    float64      `json:"last_price"`
	LastQuantity float64      `json:"last_quantity"`
	BidLevels    [][2]float64 `json:"bid_levels"` // [price, quantity]
	AskLevels    [][2]float64 `json:"ask_levels"` // [price, quantity]
}

// SystemStatusMessage contains a system status update
type SystemStatusMessage struct {
	NodeID      string    `json:"node_id"`
	Timestamp   time.Time `json:"timestamp"`
	Status      string    `json:"status"`
	Message     string    `json:"message"`
	CPUUsage    float64   `json:"cpu_usage"`
	MemoryUsage float64   `json:"memory_usage"`
	SimTime     int64     `json:"sim_time"`
}

// ExecutionMessage contains information about an order execution
type ExecutionMessage struct {
	ExchangeID   string    `json:"exchange_id"`
	InstrumentID string    `json:"instrument_id"`
	Timestamp    time.Time `json:"timestamp"`
	OrderID      string    `json:"order_id"`
	TradeID      string    `json:"trade_id"`
	ActorID      string    `json:"actor_id"`
	Side         string    `json:"side"`
	Price        float64   `json:"price"`
	Quantity     float64   `json:"quantity"`
	OrderType    string    `json:"order_type"`
}

// CommandMessage contains a command to be executed
type CommandMessage struct {
	Command    string                 `json:"command"`
	TargetNode string                 `json:"target_node"`
	Params     map[string]interface{} `json:"params"`
}

// NewMessageBroker creates a new message broker
func NewMessageBroker(
	pubEndpoint string,
	subEndpoint string,
	routerEndpoint string,
) (*MessageBroker, error) {
	// Create ZMQ context
	context, err := zmq.NewContext()
	if err != nil {
		return nil, fmt.Errorf("failed to create ZMQ context: %v", err)
	}

	broker := &MessageBroker{
		zmqContext:     context,
		pubEndpoint:    pubEndpoint,
		subEndpoint:    subEndpoint,
		routerEndpoint: routerEndpoint,
		wsClients:      make(map[*websocket.Conn]bool),
		wsUpgrader: websocket.Upgrader{
			ReadBufferSize:  1024,
			WriteBufferSize: 1024,
			CheckOrigin: func(r *http.Request) bool {
				return true // Allow all origins in development
			},
		},
		marketDataChan:    make(chan MarketDataMessage, 10000),
		systemStatusChan:  make(chan SystemStatusMessage, 1000),
		executionDataChan: make(chan ExecutionMessage, 5000),
		commandChan:       make(chan CommandMessage, 1000),
		topicMapping:      make(map[string]string),
		running:           false,
	}

	// Set up default topic mappings
	broker.topicMapping["market_data"] = "MARKET"
	broker.topicMapping["system_status"] = "STATUS"
	broker.topicMapping["execution"] = "EXEC"
	broker.topicMapping["commands"] = "CMD"

	return broker, nil
}

// Start initializes the message broker and starts processing messages
func (mb *MessageBroker) Start(ctx context.Context) error {
	mb.mutex.Lock()
	if mb.running {
		mb.mutex.Unlock()
		return fmt.Errorf("message broker already running")
	}
	mb.running = true
	mb.mutex.Unlock()

	// Create and bind publisher socket
	pubSocket, err := mb.zmqContext.NewSocket(zmq.PUB)
	if err != nil {
		return fmt.Errorf("failed to create publisher socket: %v", err)
	}
	if err := pubSocket.Bind(mb.pubEndpoint); err != nil {
		pubSocket.Close()
		return fmt.Errorf("failed to bind publisher socket: %v", err)
	}
	mb.pubSocket = pubSocket

	// Create and connect subscriber socket
	subSocket, err := mb.zmqContext.NewSocket(zmq.SUB)
	if err != nil {
		mb.pubSocket.Close()
		return fmt.Errorf("failed to create subscriber socket: %v", err)
	}
	if err := subSocket.Connect(mb.subEndpoint); err != nil {
		mb.pubSocket.Close()
		subSocket.Close()
		return fmt.Errorf("failed to connect subscriber socket: %v", err)
	}

	// Subscribe to all relevant topics
	for _, topic := range mb.topicMapping {
		if err := subSocket.SetSubscribe(topic); err != nil {
			mb.pubSocket.Close()
			subSocket.Close()
			return fmt.Errorf("failed to subscribe to topic %s: %v", topic, err)
		}
	}
	mb.subSocket = subSocket

	// Create and bind router socket for request-reply
	routerSocket, err := mb.zmqContext.NewSocket(zmq.ROUTER)
	if err != nil {
		mb.pubSocket.Close()
		mb.subSocket.Close()
		return fmt.Errorf("failed to create router socket: %v", err)
	}
	if err := routerSocket.Bind(mb.routerEndpoint); err != nil {
		mb.pubSocket.Close()
		mb.subSocket.Close()
		routerSocket.Close()
		return fmt.Errorf("failed to bind router socket: %v", err)
	}
	mb.routerSocket = routerSocket

	// Start processing goroutines
	go mb.receiveMessages(ctx)
	go mb.processMarketData(ctx)
	go mb.processSystemStatus(ctx)
	go mb.processExecutionData(ctx)
	go mb.processCommands(ctx)

	log.Println("Message broker started successfully")
	log.Printf("Publisher bound to %s", mb.pubEndpoint)
	log.Printf("Subscriber connected to %s", mb.subEndpoint)
	log.Printf("Router bound to %s", mb.routerEndpoint)

	return nil
}

// receiveMessages handles incoming ZeroMQ messages
func (mb *MessageBroker) receiveMessages(ctx context.Context) {
	// Set up poller to monitor subscriber socket
	poller := zmq.NewPoller()
	poller.Add(mb.subSocket, zmq.POLLIN)

	for {
		select {
		case <-ctx.Done():
			return

		default:
			// Poll with timeout
			sockets, err := poller.Poll(100 * time.Millisecond)
			if err != nil {
				log.Printf("Error polling ZMQ sockets: %v", err)
				continue
			}

			for _, socket := range sockets {
				switch s := socket.Socket; s {
				case mb.subSocket:
					// Receive message
					msg, err := s.RecvMessage(0)
					if err != nil {
						log.Printf("Error receiving message: %v", err)
						continue
					}

					if len(msg) < 2 {
						log.Printf("Malformed message, expected at least topic and payload")
						continue
					}

					topic := msg[0]
					payload := msg[1]

					// Process message based on topic
					mb.handleMessage(topic, payload)
				}
			}
		}
	}
}

// handleMessage processes an incoming message based on its topic
func (mb *MessageBroker) handleMessage(topic, payload string) {
	switch topic {
	case mb.topicMapping["market_data"]:
		var marketData MarketDataMessage
		if err := json.Unmarshal([]byte(payload), &marketData); err != nil {
			log.Printf("Error unmarshaling market data: %v", err)
			return
		}

		// Send to processing channel (non-blocking)
		select {
		case mb.marketDataChan <- marketData:
			// Message sent successfully
		default:
			// Channel is full, log and discard
			log.Printf("Market data channel full, discarding message for %s:%s",
				marketData.ExchangeID, marketData.InstrumentID)
		}

	case mb.topicMapping["system_status"]:
		var statusMsg SystemStatusMessage
		if err := json.Unmarshal([]byte(payload), &statusMsg); err != nil {
			log.Printf("Error unmarshaling system status: %v", err)
			return
		}

		// Send to processing channel (non-blocking)
		select {
		case mb.systemStatusChan <- statusMsg:
			// Message sent successfully
		default:
			// Channel is full, log and discard
			log.Printf("System status channel full, discarding message for %s", statusMsg.NodeID)
		}

	case mb.topicMapping["execution"]:
		var execMsg ExecutionMessage
		if err := json.Unmarshal([]byte(payload), &execMsg); err != nil {
			log.Printf("Error unmarshaling execution data: %v", err)
			return
		}

		// Send to processing channel (non-blocking)
		select {
		case mb.executionDataChan <- execMsg:
			// Message sent successfully
		default:
			// Channel is full, log and discard
			log.Printf("Execution data channel full, discarding message for %s", execMsg.OrderID)
		}

	case mb.topicMapping["commands"]:
		var cmdMsg CommandMessage
		if err := json.Unmarshal([]byte(payload), &cmdMsg); err != nil {
			log.Printf("Error unmarshaling command message: %v", err)
			return
		}

		// Send to processing channel (non-blocking)
		select {
		case mb.commandChan <- cmdMsg:
			// Message sent successfully
		default:
			// Channel is full, log and discard
			log.Printf("Command channel full, discarding command %s", cmdMsg.Command)
		}

	default:
		log.Printf("Received message with unknown topic: %s", topic)
	}
}

// Processing goroutines

func (mb *MessageBroker) processMarketData(ctx context.Context) {
	// Use a ticker to batch market data updates
	ticker := time.NewTicker(50 * time.Millisecond)
	defer ticker.Stop()

	// Batching buffer
	buffer := make(map[string]map[string]MarketDataMessage)

	for {
		select {
		case <-ctx.Done():
			return

		case data := <-mb.marketDataChan:
			// Add to buffer, keyed by exchange and instrument
			if _, exists := buffer[data.ExchangeID]; !exists {
				buffer[data.ExchangeID] = make(map[string]MarketDataMessage)
			}
			buffer[data.ExchangeID][data.InstrumentID] = data

		case <-ticker.C:
			// Process buffered data
			if len(buffer) > 0 {
				// Send to WebSocket clients
				mb.broadcastMarketData(buffer)

				// Clear buffer for next batch
				buffer = make(map[string]map[string]MarketDataMessage)
			}
		}
	}
}

func (mb *MessageBroker) processSystemStatus(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return

		case status := <-mb.systemStatusChan:
			// Process system status updates
			mb.broadcastSystemStatus(status)
		}
	}
}

func (mb *MessageBroker) processExecutionData(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return

		case exec := <-mb.executionDataChan:
			// Process execution data
			mb.broadcastExecution(exec)
		}
	}
}

func (mb *MessageBroker) processCommands(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return

		case cmd := <-mb.commandChan:
			// Process and route commands
			// This would typically involve sending commands to specific nodes
			log.Printf("Processing command: %s for node %s", cmd.Command, cmd.TargetNode)
		}
	}
}

// WebSocket methods

// HandleWebSocketConnection handles a new WebSocket connection
func (mb *MessageBroker) HandleWebSocketConnection(w http.ResponseWriter, r *http.Request) {
	conn, err := mb.wsUpgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("Failed to upgrade connection to WebSocket: %v", err)
		return
	}

	// Register new client
	mb.wsClientsMutex.Lock()
	mb.wsClients[conn] = true
	mb.wsClientsMutex.Unlock()

	// Set up cleanup when connection closes
	go func() {
		defer func() {
			conn.Close()
			mb.wsClientsMutex.Lock()
			delete(mb.wsClients, conn)
			mb.wsClientsMutex.Unlock()
		}()

		// Listen for client messages (like subscriptions)
		for {
			messageType, message, err := conn.ReadMessage()
			if err != nil {
				if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
					log.Printf("WebSocket error: %v", err)
				}
				break
			}

			// Handle client message
			mb.handleClientMessage(conn, messageType, message)
		}
	}()
}

// handleClientMessage processes messages from WebSocket clients
func (mb *MessageBroker) handleClientMessage(conn *websocket.Conn, messageType int, message []byte) {
	// Parse client message and handle accordingly
	var msg map[string]interface{}
	if err := json.Unmarshal(message, &msg); err != nil {
		log.Printf("Error parsing client message: %v", err)
		return
	}

	// Example: handle subscription requests
	if msgType, ok := msg["type"].(string); ok && msgType == "subscribe" {
		log.Printf("Received subscription request from client")
		// Handle subscription logic
	}
}

// broadcastMarketData sends market data updates to WebSocket clients
func (mb *MessageBroker) broadcastMarketData(data map[string]map[string]MarketDataMessage) {
	// Convert to JSON
	jsonData, err := json.Marshal(map[string]interface{}{
		"type": "market_data",
		"data": data,
	})
	if err != nil {
		log.Printf("Error marshaling market data: %v", err)
		return
	}

	// Send to all clients
	mb.wsClientsMutex.RLock()
	for client := range mb.wsClients {
		if err := client.WriteMessage(websocket.TextMessage, jsonData); err != nil {
			log.Printf("Error sending to client: %v", err)
		}
	}
	mb.wsClientsMutex.RUnlock()
}

// broadcastSystemStatus sends system status updates to WebSocket clients
func (mb *MessageBroker) broadcastSystemStatus(status SystemStatusMessage) {
	// Convert to JSON
	jsonData, err := json.Marshal(map[string]interface{}{
		"type": "system_status",
		"data": status,
	})
	if err != nil {
		log.Printf("Error marshaling system status: %v", err)
		return
	}

	// Send to all clients
	mb.wsClientsMutex.RLock()
	for client := range mb.wsClients {
		if err := client.WriteMessage(websocket.TextMessage, jsonData); err != nil {
			log.Printf("Error sending to client: %v", err)
		}
	}
	mb.wsClientsMutex.RUnlock()
}

// broadcastExecution sends execution updates to WebSocket clients
func (mb *MessageBroker) broadcastExecution(exec ExecutionMessage) {
	// Convert to JSON
	jsonData, err := json.Marshal(map[string]interface{}{
		"type": "execution",
		"data": exec,
	})
	if err != nil {
		log.Printf("Error marshaling execution data: %v", err)
		return
	}

	// Send to all clients
	mb.wsClientsMutex.RLock()
	for client := range mb.wsClients {
		if err := client.WriteMessage(websocket.TextMessage, jsonData); err != nil {
			log.Printf("Error sending to client: %v", err)
		}
	}
	mb.wsClientsMutex.RUnlock()
}

// PublishMarketData publishes market data to ZMQ
func (mb *MessageBroker) PublishMarketData(data MarketDataMessage) error {
	jsonData, err := json.Marshal(data)
	if err != nil {
		return fmt.Errorf("error marshaling market data: %v", err)
	}

	_, err = mb.pubSocket.SendMessage(mb.topicMapping["market_data"], string(jsonData))
	return err
}

// PublishSystemStatus publishes system status to ZMQ
func (mb *MessageBroker) PublishSystemStatus(status SystemStatusMessage) error {
	jsonData, err := json.Marshal(status)
	if err != nil {
		return fmt.Errorf("error marshaling system status: %v", err)
	}

	_, err = mb.pubSocket.SendMessage(mb.topicMapping["system_status"], string(jsonData))
	return err
}

// PublishExecution publishes execution data to ZMQ
func (mb *MessageBroker) PublishExecution(exec ExecutionMessage) error {
	jsonData, err := json.Marshal(exec)
	if err != nil {
		return fmt.Errorf("error marshaling execution data: %v", err)
	}

	_, err = mb.pubSocket.SendMessage(mb.topicMapping["execution"], string(jsonData))
	return err
}

// PublishCommand publishes a command to ZMQ
func (mb *MessageBroker) PublishCommand(cmd CommandMessage) error {
	jsonData, err := json.Marshal(cmd)
	if err != nil {
		return fmt.Errorf("error marshaling command: %v", err)
	}

	_, err = mb.pubSocket.SendMessage(mb.topicMapping["commands"], string(jsonData))
	return err
}

// SendRequest sends a request to a specific node and waits for a response
func (mb *MessageBroker) SendRequest(nodeID string, request interface{}, timeout time.Duration) (interface{}, error) {
	// Marshal request to JSON
	requestData, err := json.Marshal(request)
	if err != nil {
		return nil, fmt.Errorf("error marshaling request: %v", err)
	}

	// Set identity for the request (target node ID)
	if err := mb.routerSocket.SendMessage(nodeID, requestData); err != nil {
		return nil, fmt.Errorf("error sending request: %v", err)
	}

	// Set up poller with timeout
	poller := zmq.NewPoller()
	poller.Add(mb.routerSocket, zmq.POLLIN)

	// Wait for response with timeout
	sockets, err := poller.Poll(timeout)
	if err != nil {
		return nil, fmt.Errorf("error polling for response: %v", err)
	}

	if len(sockets) == 0 {
		return nil, fmt.Errorf("timeout waiting for response from node %s", nodeID)
	}

	// Receive response
	msg, err := mb.routerSocket.RecvMessage(0)
	if err != nil {
		return nil, fmt.Errorf("error receiving response: %v", err)
	}

	if len(msg) < 2 {
		return nil, fmt.Errorf("malformed response from node %s", nodeID)
	}

	// First frame is sender identity, second is payload
	respNodeID := msg[0]
	payload := msg[1]

	if respNodeID != nodeID {
		return nil, fmt.Errorf("received response from unexpected node: %s", respNodeID)
	}

	// Parse response
	var response interface{}
	if err := json.Unmarshal([]byte(payload), &response); err != nil {
		return nil, fmt.Errorf("error unmarshaling response: %v", err)
	}

	return response, nil
}

// Close closes the message broker and all connections
func (mb *MessageBroker) Close() {
	mb.mutex.Lock()
	defer mb.mutex.Unlock()

	if !mb.running {
		return
	}

	// Close ZMQ sockets
	if mb.pubSocket != nil {
		mb.pubSocket.Close()
	}

	if mb.subSocket != nil {
		mb.subSocket.Close()
	}

	if mb.routerSocket != nil {
		mb.routerSocket.Close()
	}

	// Close WebSocket connections
	mb.wsClientsMutex.Lock()
	for client := range mb.wsClients {
		client.Close()
	}
	mb.wsClients = make(map[*websocket.Conn]bool)
	mb.wsClientsMutex.Unlock()

	mb.running = false
	log.Println("Message broker closed")
}

// ConfigService remains largely the same as in the previous implementation
// We're focusing on updating the messaging component from NATS to ZeroMQ

// NewConfigService creates a new configuration service
func NewConfigService() *ConfigService {
	v := viper.New()
	v.SetConfigName("simulation-config")
	v.SetConfigType("yaml")
	v.AddConfigPath(".")
	v.AddConfigPath("./config")

	cs := &ConfigService{
		viper: v,
		nodes: make(map[string]*NodeInfo),
	}

	return cs
}

// Main service integration function
func setupMessagingAndConfig(ctx context.Context, orchestrator *Orchestrator) error {
	// Create config service
	configService := NewConfigService()
	if err := configService.LoadConfig(); err != nil {
		return fmt.Errorf("failed to load configuration: %v", err)
	}

	// Start node cleanup
	go configService.StartNodeCleanup(ctx, 30*time.Second, 2*time.Minute)

	// Start config service API
	if err := configService.StartConfigService(":8080"); err != nil {
		return fmt.Errorf("failed to start config service: %v", err)
	}

	// Create message broker with ZeroMQ endpoints
	messageBroker, err := NewMessageBroker(
		"tcp://*:5555",         // Publisher endpoint
		"tcp://localhost:5555", // Subscriber endpoint
		"tcp://*:5556",         // Router endpoint
	)
	if err != nil {
		return fmt.Errorf("failed to create message broker: %v", err)
	}

	// Start message broker
	if err := messageBroker.Start(ctx); err != nil {
		return fmt.Errorf("failed to start message broker: %v", err)
	}

	// Set up WebSocket handler
	http.HandleFunc("/ws", messageBroker.HandleWebSocketConnection)
	go func() {
		log.Println("Starting WebSocket server on :8081")
		if err := http.ListenAndServe(":8081", nil); err != nil {
			log.Fatalf("WebSocket server error: %v", err)
		}
	}()

	// Store for later use
	orchestrator.configService = configService
	orchestrator.messageBroker = messageBroker

	// Update configuration from service
	orchestrator.Config = configService.GetConfig()

	return nil
}
