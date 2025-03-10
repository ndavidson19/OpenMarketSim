// orchestrator.go
package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"math/rand"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	pb "github.com/yourusername/financial-sim/proto"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

// SimulationNode represents a connection to a C++ simulation node
type SimulationNode struct {
	ID         string
	Address    string
	Client     pb.SimulationServiceClient
	Connection *grpc.ClientConn
}

// Orchestrator manages multiple simulation nodes
type Orchestrator struct {
	// Nodes managed by this orchestrator
	Nodes []*SimulationNode

	// Configuration
	Config *SimulationConfig

	// Simulation state
	Running     bool
	mu          sync.Mutex
	marketData  map[string]map[string]*MarketState // exchange -> instrument -> state
	actorStates map[string]*ActorState
}

// SimulationConfig holds the configuration for the simulation
type SimulationConfig struct {
	Instruments      []InstrumentConfig
	Exchanges        []ExchangeConfig
	Actors           []ActorConfig
	SimulationLength time.Duration
	DataCollection   DataCollectionConfig
}

// InstrumentConfig defines an instrument to be simulated
type InstrumentConfig struct {
	ID           string
	InitialPrice float64
}

// ExchangeConfig defines an exchange to be simulated
type ExchangeConfig struct {
	ID          string
	BaseLatency time.Duration
	Jitter      time.Duration
	MakerRate   float64
	TakerRate   float64
	MinFee      float64
}

// ActorConfig defines a market participant
type ActorConfig struct {
	ID             string
	Type           string
	InitialCapital float64
	Instruments    []string
	Params         map[string]string
}

// DataCollectionConfig defines how data should be collected
type DataCollectionConfig struct {
	MarketDataSamplingRate time.Duration
	ActorStateSamplingRate time.Duration
	OutputPath             string
}

// MarketState holds the current state of a market
type MarketState struct {
	InstrumentID string
	Timestamp    time.Time
	BestBid      float64
	BestAsk      float64
	BidLevels    [][2]float64 // [price, volume]
	AskLevels    [][2]float64 // [price, volume]
	LastPrice    float64
	LastVolume   float64
}

// ActorState holds the current state of an actor
type ActorState struct {
	ActorID   string
	Capital   float64
	Positions map[string]Position
}

// Position represents a position in an instrument
type Position struct {
	InstrumentID  string
	Quantity      float64
	AvgPrice      float64
	RealizedPnL   float64
	UnrealizedPnL float64
}

// NewOrchestrator creates a new orchestrator
func NewOrchestrator(config *SimulationConfig) *Orchestrator {
	return &Orchestrator{
		Nodes:       make([]*SimulationNode, 0),
		Config:      config,
		Running:     false,
		marketData:  make(map[string]map[string]*MarketState),
		actorStates: make(map[string]*ActorState),
	}
}

// AddNode adds a simulation node to the orchestrator
func (o *Orchestrator) AddNode(id, address string) error {
	// Connect to the node
	conn, err := grpc.Dial(address, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		return fmt.Errorf("failed to connect to node: %v", err)
	}

	client := pb.NewSimulationServiceClient(conn)

	// Add the node
	o.Nodes = append(o.Nodes, &SimulationNode{
		ID:         id,
		Address:    address,
		Client:     client,
		Connection: conn,
	})

	log.Printf("Added simulation node: %s at %s", id, address)
	return nil
}

// Close closes all connections
func (o *Orchestrator) Close() {
	for _, node := range o.Nodes {
		node.Connection.Close()
	}
}

// SetupSimulation sets up the simulation across all nodes
func (o *Orchestrator) SetupSimulation(ctx context.Context) error {
	if len(o.Nodes) == 0 {
		return fmt.Errorf("no simulation nodes available")
	}

	log.Println("Setting up simulation across all nodes")

	// Distribute exchanges across nodes
	exchangeNodeMap := make(map[string]*SimulationNode)
	for i, exchange := range o.Config.Exchanges {
		node := o.Nodes[i%len(o.Nodes)]
		exchangeNodeMap[exchange.ID] = node

		// Create exchange on node
		err := o.createExchange(ctx, node, exchange)
		if err != nil {
			return fmt.Errorf("failed to create exchange %s: %v", exchange.ID, err)
		}
	}

	// Add instruments to exchanges
	for _, instrument := range o.Config.Instruments {
		// Assign each instrument to all exchanges for now
		// In a more complex system, you could distribute them differently
		for _, exchange := range o.Config.Exchanges {
			node := exchangeNodeMap[exchange.ID]
			err := o.addInstrument(ctx, node, exchange.ID, instrument)
			if err != nil {
				return fmt.Errorf("failed to add instrument %s to exchange %s: %v",
					instrument.ID, exchange.ID, err)
			}
		}
	}

	// Initialize market data tracking structures
	for _, exchange := range o.Config.Exchanges {
		o.marketData[exchange.ID] = make(map[string]*MarketState)
		for _, instrument := range o.Config.Instruments {
			o.marketData[exchange.ID][instrument.ID] = &MarketState{
				InstrumentID: instrument.ID,
				Timestamp:    time.Now(),
				BestBid:      0,
				BestAsk:      0,
				BidLevels:    make([][2]float64, 0),
				AskLevels:    make([][2]float64, 0),
				LastPrice:    instrument.InitialPrice,
				LastVolume:   0,
			}
		}
	}

	// Distribute actors across nodes
	for _, actor := range o.Config.Actors {
		// Choose a random node for this actor
		node := o.Nodes[rand.Intn(len(o.Nodes))]

		err := o.registerActor(ctx, node, actor)
		if err != nil {
			return fmt.Errorf("failed to register actor %s: %v", actor.ID, err)
		}

		// Initialize actor state tracking
		o.actorStates[actor.ID] = &ActorState{
			ActorID:   actor.ID,
			Capital:   actor.InitialCapital,
			Positions: make(map[string]Position),
		}
	}

	log.Println("Simulation setup completed successfully")
	return nil
}

// createExchange creates an exchange on a node
func (o *Orchestrator) createExchange(ctx context.Context, node *SimulationNode, config ExchangeConfig) error {
	req := &pb.CreateExchangeRequest{
		ExchangeId: config.ID,
		LatencyModel: &pb.LatencyModel{
			BaseLatencyNs:    config.BaseLatency.Nanoseconds(),
			JitterNs:         config.Jitter.Nanoseconds(),
			CongestionFactor: 1.0,
		},
		FeeModel: &pb.FeeModel{
			MakerRate: config.MakerRate,
			TakerRate: config.TakerRate,
			MinFee:    config.MinFee,
		},
	}

	resp, err := node.Client.CreateExchange(ctx, req)
	if err != nil {
		return err
	}

	if !resp.Success {
		return fmt.Errorf("failed to create exchange: %s", resp.Message)
	}

	log.Printf("Created exchange %s on node %s", config.ID, node.ID)
	return nil
}

// addInstrument adds an instrument to an exchange
func (o *Orchestrator) addInstrument(ctx context.Context, node *SimulationNode, exchangeID string, config InstrumentConfig) error {
	req := &pb.AddInstrumentRequest{
		ExchangeId:   exchangeID,
		InstrumentId: config.ID,
		InitialPrice: config.InitialPrice,
	}

	resp, err := node.Client.AddInstrument(ctx, req)
	if err != nil {
		return err
	}

	if !resp.Success {
		return fmt.Errorf("failed to add instrument: %s", resp.Message)
	}

	log.Printf("Added instrument %s to exchange %s on node %s",
		config.ID, exchangeID, node.ID)
	return nil
}

// registerActor registers an actor on a node
func (o *Orchestrator) registerActor(ctx context.Context, node *SimulationNode, config ActorConfig) error {
	req := &pb.RegisterActorRequest{
		ActorId:        config.ID,
		ActorType:      config.Type,
		InitialCapital: config.InitialCapital,
		Instruments:    config.Instruments,
		Params:         config.Params,
	}

	resp, err := node.Client.RegisterActor(ctx, req)
	if err != nil {
		return err
	}

	if !resp.Success {
		return fmt.Errorf("failed to register actor: %s", resp.Message)
	}

	log.Printf("Registered actor %s of type %s on node %s",
		config.ID, config.Type, node.ID)
	return nil
}

// RunSimulation runs the simulation across all nodes
func (o *Orchestrator) RunSimulation(ctx context.Context) error {
	o.mu.Lock()
	if o.Running {
		o.mu.Unlock()
		return fmt.Errorf("simulation is already running")
	}
	o.Running = true
	o.mu.Unlock()

	log.Println("Starting simulation across all nodes")

	// Start data collection
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		o.collectData(ctx)
	}()

	// Run the simulation on all nodes
	nodeErrors := make(chan error, len(o.Nodes))
	for _, node := range o.Nodes {
		wg.Add(1)
		go func(n *SimulationNode) {
			defer wg.Done()

			req := &pb.RunSimulationRequest{
				DurationNs: o.Config.SimulationLength.Nanoseconds(),
			}

			resp, err := n.Client.RunSimulation(ctx, req)
			if err != nil {
				nodeErrors <- fmt.Errorf("error on node %s: %v", n.ID, err)
				return
			}

			if !resp.Success {
				nodeErrors <- fmt.Errorf("simulation failed on node %s: %s", n.ID, resp.Message)
				return
			}

			log.Printf("Simulation started on node %s", n.ID)
		}(node)
	}

	// Wait for simulation to complete
	go func() {
		wg.Wait()
		close(nodeErrors)

		o.mu.Lock()
		o.Running = false
		o.mu.Unlock()

		log.Println("Simulation completed on all nodes")
	}()

	// Check for errors
	for err := range nodeErrors {
		if err != nil {
			return err
		}
	}

	return nil
}

// collectData periodically collects data from all nodes
func (o *Orchestrator) collectData(ctx context.Context) {
	// Create tickers for market data and actor state collection
	marketDataTicker := time.NewTicker(o.Config.DataCollection.MarketDataSamplingRate)
	defer marketDataTicker.Stop()

	actorStateTicker := time.NewTicker(o.Config.DataCollection.ActorStateSamplingRate)
	defer actorStateTicker.Stop()

	// Create output file
	dataFile, err := os.Create(o.Config.DataCollection.OutputPath)
	if err != nil {
		log.Printf("Failed to create data output file: %v", err)
		return
	}
	defer dataFile.Close()

	// Write header
	fmt.Fprintf(dataFile, "timestamp,event_type,exchange_id,instrument_id,actor_id,bid,ask,last_price,volume,capital,position,avg_price,realized_pnl,unrealized_pnl\n")

	for {
		select {
		case <-ctx.Done():
			return

		case <-marketDataTicker.C:
			// Collect market data from all nodes
			for _, node := range o.Nodes {
				// Prepare request for all instruments
				instruments := make([]string, 0, len(o.Config.Instruments))
				for _, inst := range o.Config.Instruments {
					instruments = append(instruments, inst.ID)
				}

				// Get state from node
				req := &pb.GetStateRequest{
					Instruments: instruments,
					Actors:      []string{}, // Don't need actor data here
				}

				resp, err := node.Client.GetSimulationState(ctx, req)
				if err != nil {
					log.Printf("Failed to get market data from node %s: %v", node.ID, err)
					continue
				}

				// Update local market data
				for _, exchange := range resp.Exchanges {
					for _, instrument := range exchange.Instruments {
						o.mu.Lock()

						if _, exists := o.marketData[exchange.ExchangeId]; !exists {
							o.marketData[exchange.ExchangeId] = make(map[string]*MarketState)
						}

						if _, exists := o.marketData[exchange.ExchangeId][instrument.InstrumentId]; !exists {
							o.marketData[exchange.ExchangeId][instrument.InstrumentId] = &MarketState{
								InstrumentID: instrument.InstrumentId,
							}
						}

						market := o.marketData[exchange.ExchangeId][instrument.InstrumentId]
						market.Timestamp = time.Now()
						market.BestBid = instrument.BestBid
						market.BestAsk = instrument.BestAsk

						// Update bid levels
						market.BidLevels = make([][2]float64, len(instrument.BidLevels))
						for i, level := range instrument.BidLevels {
							market.BidLevels[i] = [2]float64{level.Price, level.Volume}
						}

						// Update ask levels
						market.AskLevels = make([][2]float64, len(instrument.AskLevels))
						for i, level := range instrument.AskLevels {
							market.AskLevels[i] = [2]float64{level.Price, level.Volume}
						}

						// Write to output file
						fmt.Fprintf(dataFile, "%d,market_data,%s,%s,,%f,%f,,,,,,,\n",
							time.Now().UnixNano(),
							exchange.ExchangeId,
							instrument.InstrumentId,
							market.BestBid,
							market.BestAsk)

						o.mu.Unlock()
					}
				}
			}

		case <-actorStateTicker.C:
			// Collect actor states from all nodes
			for _, node := range o.Nodes {
				// Prepare request for all actors
				actors := make([]string, 0, len(o.Config.Actors))
				for _, actor := range o.Config.Actors {
					actors = append(actors, actor.ID)
				}

				// Get state from node
				req := &pb.GetStateRequest{
					Instruments: []string{}, // Don't need instrument data here
					Actors:      actors,
				}

				resp, err := node.Client.GetSimulationState(ctx, req)
				if err != nil {
					log.Printf("Failed to get actor states from node %s: %v", node.ID, err)
					continue
				}

				// Update local actor states
				for _, actorState := range resp.Actors {
					o.mu.Lock()

					if _, exists := o.actorStates[actorState.ActorId]; !exists {
						o.actorStates[actorState.ActorId] = &ActorState{
							ActorID:   actorState.ActorId,
							Positions: make(map[string]Position),
						}
					}

					actor := o.actorStates[actorState.ActorId]
					actor.Capital = actorState.Capital

					// Update positions
					for _, pos := range actorState.Positions {
						actor.Positions[pos.InstrumentId] = Position{
							InstrumentID:  pos.InstrumentId,
							Quantity:      pos.Quantity,
							AvgPrice:      pos.AvgPrice,
							RealizedPnL:   pos.RealizedPnl,
							UnrealizedPnL: pos.UnrealizedPnl,
						}

						// Write to output file
						fmt.Fprintf(dataFile, "%d,actor_state,,%s,%s,,,,%f,%f,%f,%f,%f\n",
							time.Now().UnixNano(),
							pos.InstrumentId,
							actorState.ActorId,
							actor.Capital,
							pos.Quantity,
							pos.AvgPrice,
							pos.RealizedPnl,
							pos.UnrealizedPnl)
					}

					o.mu.Unlock()
				}
			}
		}
	}
}

func createSampleConfig() *SimulationConfig {
	return &SimulationConfig{
		Instruments: []InstrumentConfig{
			{ID: "AAPL", InitialPrice: 150.0},
			{ID: "MSFT", InitialPrice: 250.0},
			{ID: "GOOGL", InitialPrice: 2500.0},
		},
		Exchanges: []ExchangeConfig{
			{
				ID:          "nyse",
				BaseLatency: 1 * time.Millisecond,
				Jitter:      500 * time.Microsecond,
				MakerRate:   -0.0002, // Rebate
				TakerRate:   0.0007,
				MinFee:      0.01,
			},
		},
		Actors: []ActorConfig{
			{
				ID:             "mm_1",
				Type:           "market_maker",
				InitialCapital: 1000000.0,
				Instruments:    []string{"AAPL", "MSFT", "GOOGL"},
				Params: map[string]string{
					"target_spread":  "0.05",
					"max_position":   "1000.0",
					"order_size_pct": "0.05",
				},
			},
			{
				ID:             "tf_1",
				Type:           "trend_follower",
				InitialCapital: 1000000.0,
				Instruments:    []string{"AAPL", "MSFT", "GOOGL"},
				Params: map[string]string{
					"short_window":      "10",
					"long_window":       "30",
					"position_size_pct": "0.1",
				},
			},
		},
		SimulationLength: 60 * time.Second,
		DataCollection: DataCollectionConfig{
			MarketDataSamplingRate: 1 * time.Second,
			ActorStateSamplingRate: 5 * time.Second,
			OutputPath:             "simulation_data.csv",
		},
	}
}

func main() {
	// Parse command line flags
	nodeAddrs := flag.String("nodes", "localhost:50051", "Comma-separated list of simulation node addresses")
	flag.Parse()

	// Create context with cancellation
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Set up signal handling for graceful shutdown
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-signals
		log.Printf("Received signal: %v, initiating shutdown", sig)
		cancel()
	}()

	// Create orchestrator with sample config
	config := createSampleConfig()
	orchestrator := NewOrchestrator(config)
	defer orchestrator.Close()

	// Add simulation nodes
	// In a real implementation, you would parse the nodeAddrs flag
	// and add each node address
	err := orchestrator.AddNode("node1", "localhost:50051")
	if err != nil {
		log.Fatalf("Failed to add node: %v", err)
	}

	// Set up the simulation
	err = orchestrator.SetupSimulation(ctx)
	if err != nil {
		log.Fatalf("Failed to set up simulation: %v", err)
	}

	// Run the simulation
	err = orchestrator.RunSimulation(ctx)
	if err != nil {
		log.Fatalf("Failed to run simulation: %v", err)
	}

	// Wait for context cancellation (from signal handler)
	<-ctx.Done()
	log.Println("Shutting down orchestrator")
}
