// config.go
package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"time"

	"github.com/spf13/viper"
	"gopkg.in/yaml.v2"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/runtime/schema"
)

// SimulationConfig is the root configuration structure
type SimulationConfig struct {
	// Meta information
	Name        string            `json:"name" yaml:"name"`
	Description string            `json:"description" yaml:"description"`
	Version     string            `json:"version" yaml:"version"`
	Tags        map[string]string `json:"tags" yaml:"tags"`

	// Cluster configuration
	Cluster ClusterConfig `json:"cluster" yaml:"cluster"`

	// Simulation parameters
	SimulationParams SimulationParams `json:"simulationParams" yaml:"simulationParams"`

	// Market structure
	Exchanges   []ExchangeConfig   `json:"exchanges" yaml:"exchanges"`
	Instruments []InstrumentConfig `json:"instruments" yaml:"instruments"`

	// Market participants
	Actors []ActorConfig `json:"actors" yaml:"actors"`

	// External data sources
	DataSources []DataSourceConfig `json:"dataSources" yaml:"dataSources"`

	// Market events and news
	MarketEvents []MarketEventConfig `json:"marketEvents" yaml:"marketEvents"`

	// Output and analysis
	OutputConfig OutputConfig `json:"outputConfig" yaml:"outputConfig"`
}

// ClusterConfig defines how the simulation is deployed on Kubernetes
type ClusterConfig struct {
	Enabled            bool   `json:"enabled" yaml:"enabled"`
	Namespace          string `json:"namespace" yaml:"namespace"`
	ServiceAccountName string `json:"serviceAccountName" yaml:"serviceAccountName"`

	// Node configurations
	MinNodes  int    `json:"minNodes" yaml:"minNodes"`
	MaxNodes  int    `json:"maxNodes" yaml:"maxNodes"`
	NodeImage string `json:"nodeImage" yaml:"nodeImage"`

	// Resource requests/limits
	Resources ResourceConfig `json:"resources" yaml:"resources"`

	// Network configuration
	NetworkConfig NetworkConfig `json:"networkConfig" yaml:"networkConfig"`

	// Storage configuration
	PersistentStorage bool   `json:"persistentStorage" yaml:"persistentStorage"`
	StorageClass      string `json:"storageClass" yaml:"storageClass"`
	StorageSize       string `json:"storageSize" yaml:"storageSize"`
}

// ResourceConfig defines CPU/memory requests and limits
type ResourceConfig struct {
	RequestsCPU    string `json:"requestsCpu" yaml:"requestsCpu"`
	RequestsMemory string `json:"requestsMemory" yaml:"requestsMemory"`
	LimitsCPU      string `json:"limitsCpu" yaml:"limitsCpu"`
	LimitsMemory   string `json:"limitsMemory" yaml:"limitsMemory"`
}

// NetworkConfig defines network settings for the simulation
type NetworkConfig struct {
	EnableVirtualNetwork bool              `json:"enableVirtualNetwork" yaml:"enableVirtualNetwork"`
	TopologyFile         string            `json:"topologyFile" yaml:"topologyFile"`
	Latencies            map[string]string `json:"latencies" yaml:"latencies"` // e.g. "us-east:eu-west": "80ms"
	FailureRate          float64           `json:"failureRate" yaml:"failureRate"`
	JitterFactor         float64           `json:"jitterFactor" yaml:"jitterFactor"`
}

// SimulationParams defines the core simulation parameters
type SimulationParams struct {
	StartTime          time.Time        `json:"startTime" yaml:"startTime"`
	Duration           string           `json:"duration" yaml:"duration"`
	TimeScale          float64          `json:"timeScale" yaml:"timeScale"`
	RandomSeed         int64            `json:"randomSeed" yaml:"randomSeed"`
	ClockSyncInterval  string           `json:"clockSyncInterval" yaml:"clockSyncInterval"`
	MarketHours        MarketHours      `json:"marketHours" yaml:"marketHours"`
	CheckpointInterval string           `json:"checkpointInterval" yaml:"checkpointInterval"`
	CheckpointPath     string           `json:"checkpointPath" yaml:"checkpointPath"`
	MaxEvents          int64            `json:"maxEvents" yaml:"maxEvents"`
	MarketConditions   MarketConditions `json:"marketConditions" yaml:"marketConditions"`
}

// MarketHours defines trading session hours
type MarketHours struct {
	Enabled    bool   `json:"enabled" yaml:"enabled"`
	OpenTime   string `json:"openTime" yaml:"openTime"`   // "09:30:00"
	CloseTime  string `json:"closeTime" yaml:"closeTime"` // "16:00:00"
	TimeZone   string `json:"timeZone" yaml:"timeZone"`   // "America/New_York"
	PreMarket  bool   `json:"preMarket" yaml:"preMarket"`
	AfterHours bool   `json:"afterHours" yaml:"afterHours"`
}

// MarketConditions defines the overall market environment
type MarketConditions struct {
	VolatilityLevel   string  `json:"volatilityLevel" yaml:"volatilityLevel"` // low, medium, high, extreme
	LiquidityLevel    string  `json:"liquidityLevel" yaml:"liquidityLevel"`   // low, medium, high
	SentimentBias     float64 `json:"sentimentBias" yaml:"sentimentBias"`     // -1.0 to 1.0
	CorrelationFactor float64 `json:"correlationFactor" yaml:"correlationFactor"`
	CircuitBreakers   bool    `json:"circuitBreakers" yaml:"circuitBreakers"`
	MarketStress      float64 `json:"marketStress" yaml:"marketStress"` // 0.0 to 1.0
}

// ExchangeConfig defines a trading venue
type ExchangeConfig struct {
	ID                 string            `json:"id" yaml:"id"`
	Name               string            `json:"name" yaml:"name"`
	Type               string            `json:"type" yaml:"type"` // "equity", "futures", "options", "crypto"
	Location           string            `json:"location" yaml:"location"`
	Instruments        []string          `json:"instruments" yaml:"instruments"`
	TickSize           float64           `json:"tickSize" yaml:"tickSize"`
	TradingFees        TradingFees       `json:"tradingFees" yaml:"tradingFees"`
	MarketMakerRebates bool              `json:"marketMakerRebates" yaml:"marketMakerRebates"`
	LatencyModel       LatencyModel      `json:"latencyModel" yaml:"latencyModel"`
	MatchingEngine     MatchingEngine    `json:"matchingEngine" yaml:"matchingEngine"`
	OrderTypes         []string          `json:"orderTypes" yaml:"orderTypes"`
	Capabilities       map[string]bool   `json:"capabilities" yaml:"capabilities"`
	Parameters         map[string]string `json:"parameters" yaml:"parameters"`
}

// TradingFees defines the fee structure
type TradingFees struct {
	MakerFee       float64   `json:"makerFee" yaml:"makerFee"`
	TakerFee       float64   `json:"takerFee" yaml:"takerFee"`
	MinFee         float64   `json:"minFee" yaml:"minFee"`
	MaxFee         float64   `json:"maxFee" yaml:"maxFee"`
	DiscountLevels []float64 `json:"discountLevels" yaml:"discountLevels"`
}

// LatencyModel defines network and processing delays
type LatencyModel struct {
	BaseLatencyMS     int            `json:"baseLatencyMs" yaml:"baseLatencyMs"`
	JitterMS          int            `json:"jitterMs" yaml:"jitterMs"`
	LocationLatencies map[string]int `json:"locationLatencies" yaml:"locationLatencies"`
	CongestionFactor  float64        `json:"congestionFactor" yaml:"congestionFactor"`
	CongestionModel   string         `json:"congestionModel" yaml:"congestionModel"` // "linear", "exponential", etc.
}

// MatchingEngine defines matching engine characteristics
type MatchingEngine struct {
	Type              string `json:"type" yaml:"type"` // "price-time", "pro-rata", etc.
	ProcessingTimeNS  int    `json:"processingTimeNs" yaml:"processingTimeNs"`
	BatchProcessing   bool   `json:"batchProcessing" yaml:"batchProcessing"`
	BatchIntervalMS   int    `json:"batchIntervalMs" yaml:"batchIntervalMs"`
	MarketOrderBuffer int    `json:"marketOrderBuffer" yaml:"marketOrderBuffer"`
}

// InstrumentConfig defines a tradable instrument
type InstrumentConfig struct {
	ID           string   `json:"id" yaml:"id"`
	Symbol       string   `json:"symbol" yaml:"symbol"`
	Name         string   `json:"name" yaml:"name"`
	Type         string   `json:"type" yaml:"type"` // "stock", "future", "option", "crypto"
	InitialPrice float64  `json:"initialPrice" yaml:"initialPrice"`
	Currency     string   `json:"currency" yaml:"currency"`
	Exchanges    []string `json:"exchanges" yaml:"exchanges"`
	MinTickSize  float64  `json:"minTickSize" yaml:"minTickSize"`
	LotSize      float64  `json:"lotSize" yaml:"lotSize"`
	MarketCap    float64  `json:"marketCap" yaml:"marketCap"`
	Sector       string   `json:"sector" yaml:"sector"`
	Industry     string   `json:"industry" yaml:"industry"`

	// For options
	OptionConfig *OptionConfig `json:"optionConfig,omitempty" yaml:"optionConfig,omitempty"`

	// Price dynamics
	PriceDynamics PriceDynamics `json:"priceDynamics" yaml:"priceDynamics"`

	// Correlations with other instruments
	Correlations map[string]float64 `json:"correlations" yaml:"correlations"`
}

// OptionConfig contains option-specific configuration
type OptionConfig struct {
	UnderlyingID string  `json:"underlyingId" yaml:"underlyingId"`
	OptionType   string  `json:"optionType" yaml:"optionType"` // "call", "put"
	Strike       float64 `json:"strike" yaml:"strike"`
	ExpiryDate   string  `json:"expiryDate" yaml:"expiryDate"`
	Exercise     string  `json:"exercise" yaml:"exercise"` // "european", "american"
	ImpliedVol   float64 `json:"impliedVol" yaml:"impliedVol"`
}

// PriceDynamics defines how prices evolve
type PriceDynamics struct {
	Model           string    `json:"model" yaml:"model"` // "gbn", "heston", "jump-diffusion"
	Volatility      float64   `json:"volatility" yaml:"volatility"`
	DriftFactor     float64   `json:"driftFactor" yaml:"driftFactor"`
	MeanReversion   float64   `json:"meanReversion" yaml:"meanReversion"`
	JumpIntensity   float64   `json:"jumpIntensity" yaml:"jumpIntensity"`
	JumpSizeMean    float64   `json:"jumpSizeMean" yaml:"jumpSizeMean"`
	JumpSizeStdDev  float64   `json:"jumpSizeStdDev" yaml:"jumpSizeStdDev"`
	SeasonalFactors []float64 `json:"seasonalFactors" yaml:"seasonalFactors"`
}

// ActorConfig defines a market participant
type ActorConfig struct {
	ID             string   `json:"id" yaml:"id"`
	Type           string   `json:"type" yaml:"type"`   // "market_maker", "trend_follower", "hft", "retail"
	Count          int      `json:"count" yaml:"count"` // Number of this actor type to create
	InitialCapital float64  `json:"initialCapital" yaml:"initialCapital"`
	Location       string   `json:"location" yaml:"location"`
	Instruments    []string `json:"instruments" yaml:"instruments"`

	// Type-specific parameters
	MarketMakerParams   *MarketMakerParams   `json:"marketMakerParams,omitempty" yaml:"marketMakerParams,omitempty"`
	TrendFollowerParams *TrendFollowerParams `json:"trendFollowerParams,omitempty" yaml:"trendFollowerParams,omitempty"`
	HFTParams           *HFTParams           `json:"hftParams,omitempty" yaml:"hftParams,omitempty"`
	RetailParams        *RetailParams        `json:"retailParams,omitempty" yaml:"retailParams,omitempty"`

	// Behavioral parameters
	BehavioralParams BehavioralParams `json:"behavioralParams" yaml:"behavioralParams"`

	// Custom parameters
	Parameters map[string]string `json:"parameters" yaml:"parameters"`
}

// MarketMakerParams contains market maker specific parameters
type MarketMakerParams struct {
	TargetSpreadBps  float64 `json:"targetSpreadBps" yaml:"targetSpreadBps"`
	MaxPosition      float64 `json:"maxPosition" yaml:"maxPosition"`
	OrderSizePercent float64 `json:"orderSizePercent" yaml:"orderSizePercent"`
	QuoteRefreshMS   int     `json:"quoteRefreshMs" yaml:"quoteRefreshMs"`
	InventorySkew    bool    `json:"inventorySkew" yaml:"inventorySkew"`
	SkewFactor       float64 `json:"skewFactor" yaml:"skewFactor"`
	LayeringStrategy string  `json:"layeringStrategy" yaml:"layeringStrategy"`
	MaxLayers        int     `json:"maxLayers" yaml:"maxLayers"`
}

// TrendFollowerParams contains trend follower specific parameters
type TrendFollowerParams struct {
	ShortWindow         int     `json:"shortWindow" yaml:"shortWindow"`
	LongWindow          int     `json:"longWindow" yaml:"longWindow"`
	PositionSizePercent float64 `json:"positionSizePercent" yaml:"positionSizePercent"`
	EntryThreshold      float64 `json:"entryThreshold" yaml:"entryThreshold"`
	ExitThreshold       float64 `json:"exitThreshold" yaml:"exitThreshold"`
	StopLossPct         float64 `json:"stopLossPct" yaml:"stopLossPct"`
	TakeProfitPct       float64 `json:"takeProfitPct" yaml:"takeProfitPct"`
}

// HFTParams contains high frequency trader specific parameters
type HFTParams struct {
	MinEdge         float64            `json:"minEdge" yaml:"minEdge"`
	ReactionTimeNS  int64              `json:"reactionTimeNs" yaml:"reactionTimeNs"`
	Colocation      bool               `json:"colocation" yaml:"colocation"`
	MaxOrdersPerSec int                `json:"maxOrdersPerSec" yaml:"maxOrdersPerSec"`
	CancelRate      float64            `json:"cancelRate" yaml:"cancelRate"`
	Strategies      []string           `json:"strategies" yaml:"strategies"`
	SignalWeights   map[string]float64 `json:"signalWeights" yaml:"signalWeights"`
}

// RetailParams contains retail trader specific parameters
type RetailParams struct {
	RiskAversion          float64 `json:"riskAversion" yaml:"riskAversion"`
	FOMOSusceptibility    float64 `json:"fomoSusceptibility" yaml:"fomoSusceptibility"`
	LossAversion          float64 `json:"lossAversion" yaml:"lossAversion"`
	NewsResponseFactor    float64 `json:"newsResponseFactor" yaml:"newsResponseFactor"`
	TradingFrequency      string  `json:"tradingFrequency" yaml:"tradingFrequency"`           // "low", "medium", "high"
	OrderSizeDistribution string  `json:"orderSizeDistribution" yaml:"orderSizeDistribution"` // Distribution parameters
}

// BehavioralParams defines common behavioral parameters
type BehavioralParams struct {
	RandomSeed       int64   `json:"randomSeed" yaml:"randomSeed"`
	StrategicBias    float64 `json:"strategicBias" yaml:"strategicBias"` // -1.0 to 1.0
	NoiseLevel       float64 `json:"noiseLevel" yaml:"noiseLevel"`       // 0.0 to 1.0
	AdaptationRate   float64 `json:"adaptationRate" yaml:"adaptationRate"`
	InformationLevel float64 `json:"informationLevel" yaml:"informationLevel"` // 0.0 to 1.0
}

// DataSourceConfig defines an external data source
type DataSourceConfig struct {
	ID               string            `json:"id" yaml:"id"`
	Type             string            `json:"type" yaml:"type"` // "historical", "realtime", "file"
	Provider         string            `json:"provider" yaml:"provider"`
	ConnectionString string            `json:"connectionString" yaml:"connectionString"`
	Instruments      []string          `json:"instruments" yaml:"instruments"`
	StartDate        string            `json:"startDate" yaml:"startDate"`
	EndDate          string            `json:"endDate" yaml:"endDate"`
	BufferSize       int               `json:"bufferSize" yaml:"bufferSize"`
	Parameters       map[string]string `json:"parameters" yaml:"parameters"`
}

// MarketEventConfig defines scheduled or random market events
type MarketEventConfig struct {
	ID               string            `json:"id" yaml:"id"`
	Type             string            `json:"type" yaml:"type"`                 // "earnings", "economic", "geopolitical"
	ScheduleType     string            `json:"scheduleType" yaml:"scheduleType"` // "fixed", "random", "conditional"
	ScheduleTime     string            `json:"scheduleTime" yaml:"scheduleTime"`
	Instruments      []string          `json:"instruments" yaml:"instruments"`
	AffectedSectors  []string          `json:"affectedSectors" yaml:"affectedSectors"`
	ImpactMagnitude  float64           `json:"impactMagnitude" yaml:"impactMagnitude"`   // -1.0 to 1.0
	ImpactPermanence float64           `json:"impactPermanence" yaml:"impactPermanence"` // 0.0 to 1.0
	VolatilityImpact float64           `json:"volatilityImpact" yaml:"volatilityImpact"`
	Description      string            `json:"description" yaml:"description"`
	Parameters       map[string]string `json:"parameters" yaml:"parameters"`
}

// OutputConfig defines data collection and output settings
type OutputConfig struct {
	LogLevel            string   `json:"logLevel" yaml:"logLevel"`
	DataCollectionRate  string   `json:"dataCollectionRate" yaml:"dataCollectionRate"`
	OutputFormat        string   `json:"outputFormat" yaml:"outputFormat"` // "csv", "parquet", "arrow"
	OutputPath          string   `json:"outputPath" yaml:"outputPath"`
	Metrics             []string `json:"metrics" yaml:"metrics"`
	SnapshotInterval    string   `json:"snapshotInterval" yaml:"snapshotInterval"`
	CompressOutput      bool     `json:"compressOutput" yaml:"compressOutput"`
	RetentionPolicy     string   `json:"retentionPolicy" yaml:"retentionPolicy"`
	EnableTraceLogging  bool     `json:"enableTraceLogging" yaml:"enableTraceLogging"`
	EnableMetricsServer bool     `json:"enableMetricsServer" yaml:"enableMetricsServer"`
	MetricsPort         int      `json:"metricsPort" yaml:"metricsPort"`
}

// ConfigService manages the simulation configuration
type ConfigService struct {
	configPath string
	configFile string
	config     *SimulationConfig
	viper      *viper.Viper
}

// NewConfigService creates a new config service
func NewConfigService(configPath, configFile string) *ConfigService {
	v := viper.New()
	v.SetConfigName(configFile)
	v.SetConfigType("yaml")
	v.AddConfigPath(configPath)
	v.AddConfigPath(".")

	return &ConfigService{
		configPath: configPath,
		configFile: configFile,
		viper:      v,
	}
}

// LoadConfig loads the configuration from file
func (cs *ConfigService) LoadConfig() error {
	if err := cs.viper.ReadInConfig(); err != nil {
		return fmt.Errorf("error reading config file: %v", err)
	}

	config := &SimulationConfig{}
	if err := cs.viper.Unmarshal(config); err != nil {
		return fmt.Errorf("error unmarshaling config: %v", err)
	}

	cs.config = config
	return nil
}

// GetConfig returns the current configuration
func (cs *ConfigService) GetConfig() *SimulationConfig {
	return cs.config
}

// SaveConfig saves the configuration to file
func (cs *ConfigService) SaveConfig() error {
	data, err := yaml.Marshal(cs.config)
	if err != nil {
		return fmt.Errorf("error marshaling config: %v", err)
	}

	configFile := filepath.Join(cs.configPath, cs.configFile+".yaml")
	if err := ioutil.WriteFile(configFile, data, 0644); err != nil {
		return fmt.Errorf("error writing config file: %v", err)
	}

	return nil
}

// UpdateConfig updates the configuration
func (cs *ConfigService) UpdateConfig(config *SimulationConfig) {
	cs.config = config
}

// ValidateConfig validates the configuration
func (cs *ConfigService) ValidateConfig() error {
	if cs.config == nil {
		return fmt.Errorf("no configuration loaded")
	}

	// Validate simulation params
	if cs.config.SimulationParams.TimeScale <= 0 {
		return fmt.Errorf("time scale must be positive")
	}

	// Validate exchanges
	exchangeMap := make(map[string]bool)
	for _, exchange := range cs.config.Exchanges {
		if exchange.ID == "" {
			return fmt.Errorf("exchange ID cannot be empty")
		}
		if exchangeMap[exchange.ID] {
			return fmt.Errorf("duplicate exchange ID: %s", exchange.ID)
		}
		exchangeMap[exchange.ID] = true
	}

	// Validate instruments
	instrumentMap := make(map[string]bool)
	for _, instrument := range cs.config.Instruments {
		if instrument.ID == "" {
			return fmt.Errorf("instrument ID cannot be empty")
		}
		if instrumentMap[instrument.ID] {
			return fmt.Errorf("duplicate instrument ID: %s", instrument.ID)
		}
		instrumentMap[instrument.ID] = true

		// Validate exchanges
		for _, exchangeID := range instrument.Exchanges {
			if !exchangeMap[exchangeID] {
				return fmt.Errorf("invalid exchange ID for instrument %s: %s", instrument.ID, exchangeID)
			}
		}
	}

	// Validate actors
	for _, actor := range cs.config.Actors {
		if actor.ID == "" {
			return fmt.Errorf("actor ID cannot be empty")
		}
		if actor.InitialCapital <= 0 {
			return fmt.Errorf("initial capital must be positive for actor %s", actor.ID)
		}

		// Validate instruments
		for _, instrumentID := range actor.Instruments {
			if !instrumentMap[instrumentID] {
				return fmt.Errorf("invalid instrument ID for actor %s: %s", actor.ID, instrumentID)
			}
		}

		// Validate actor-specific parameters
		switch actor.Type {
		case "market_maker":
			if actor.MarketMakerParams == nil {
				return fmt.Errorf("market maker parameters required for actor %s", actor.ID)
			}
		case "trend_follower":
			if actor.TrendFollowerParams == nil {
				return fmt.Errorf("trend follower parameters required for actor %s", actor.ID)
			}
		case "hft":
			if actor.HFTParams == nil {
				return fmt.Errorf("HFT parameters required for actor %s", actor.ID)
			}
		case "retail":
			if actor.RetailParams == nil {
				return fmt.Errorf("retail trader parameters required for actor %s", actor.ID)
			}
		default:
			return fmt.Errorf("unknown actor type for %s: %s", actor.ID, actor.Type)
		}
	}

	return nil
}

// GenerateDefaultConfig creates a default configuration
func (cs *ConfigService) GenerateDefaultConfig() *SimulationConfig {
	return &SimulationConfig{
		Name:        "Default Simulation",
		Description: "Default financial market simulation configuration",
		Version:     "1.0",
		Tags:        map[string]string{"environment": "development"},

		Cluster: ClusterConfig{
			Enabled:   false,
			Namespace: "financial-sim",
			MinNodes:  1,
			MaxNodes:  3,
			NodeImage: "financial-sim:latest",
			Resources: ResourceConfig{
				RequestsCPU:    "1",
				RequestsMemory: "2Gi",
				LimitsCPU:      "2",
				LimitsMemory:   "4Gi",
			},
		},

		SimulationParams: SimulationParams{
			StartTime:         time.Now(),
			Duration:          "1h",
			TimeScale:         1.0,
			RandomSeed:        42,
			ClockSyncInterval: "5s",
			MarketHours: MarketHours{
				Enabled:   true,
				OpenTime:  "09:30:00",
				CloseTime: "16:00:00",
				TimeZone:  "America/New_York",
			},
			MarketConditions: MarketConditions{
				VolatilityLevel: "medium",
				LiquidityLevel:  "normal",
				SentimentBias:   0.0,
			},
		},

		Exchanges: []ExchangeConfig{
			{
				ID:       "nyse",
				Name:     "New York Stock Exchange",
				Type:     "equity",
				Location: "us-east",
				TickSize: 0.01,
				TradingFees: TradingFees{
					MakerFee: -0.0002,
					TakerFee: 0.0007,
					MinFee:   0.01,
				},
				LatencyModel: LatencyModel{
					BaseLatencyMS: 1,
					JitterMS:      1,
				},
				MatchingEngine: MatchingEngine{
					Type:             "price-time",
					ProcessingTimeNS: 100000,
				},
				OrderTypes: []string{"market", "limit", "stop", "stop_limit"},
			},
		},

		Instruments: []InstrumentConfig{
			{
				ID:           "AAPL",
				Symbol:       "AAPL",
				Name:         "Apple Inc.",
				Type:         "stock",
				InitialPrice: 150.0,
				Currency:     "USD",
				Exchanges:    []string{"nyse"},
				MinTickSize:  0.01,
				PriceDynamics: PriceDynamics{
					Model:       "gbn",
					Volatility:  0.2,
					DriftFactor: 0.0,
				},
			},
			{
				ID:           "MSFT",
				Symbol:       "MSFT",
				Name:         "Microsoft Corporation",
				Type:         "stock",
				InitialPrice: 250.0,
				Currency:     "USD",
				Exchanges:    []string{"nyse"},
				MinTickSize:  0.01,
				PriceDynamics: PriceDynamics{
					Model:       "gbn",
					Volatility:  0.18,
					DriftFactor: 0.0,
				},
			},
		},

		Actors: []ActorConfig{
			{
				ID:             "mm_1",
				Type:           "market_maker",
				Count:          2,
				InitialCapital: 1000000.0,
				Location:       "us-east",
				Instruments:    []string{"AAPL", "MSFT"},
				MarketMakerParams: &MarketMakerParams{
					TargetSpreadBps:  5.0,
					MaxPosition:      1000.0,
					OrderSizePercent: 0.05,
					QuoteRefreshMS:   100,
					InventorySkew:    true,
				},
				BehavioralParams: BehavioralParams{
					RandomSeed:     1234,
					StrategicBias:  0.0,
					NoiseLevel:     0.1,
					AdaptationRate: 0.5,
				},
			},
			{
				ID:             "tf_1",
				Type:           "trend_follower",
				Count:          5,
				InitialCapital: 1000000.0,
				Location:       "us-west",
				Instruments:    []string{"AAPL", "MSFT"},
				TrendFollowerParams: &TrendFollowerParams{
					ShortWindow:         10,
					LongWindow:          30,
					PositionSizePercent: 0.1,
					EntryThreshold:      0.2,
					ExitThreshold:       0.1,
					StopLossPct:         5.0,
				},
				BehavioralParams: BehavioralParams{
					RandomSeed:     5678,
					StrategicBias:  0.2,
					NoiseLevel:     0.2,
					AdaptationRate: 0.3,
				},
			},
		},

		DataSources: []DataSourceConfig{
			{
				ID:               "historical_daily",
				Type:             "historical",
				Provider:         "csv",
				ConnectionString: "data/historical/daily",
				Instruments:      []string{"AAPL", "MSFT"},
				StartDate:        "2020-01-01",
				EndDate:          "2020-12-31",
			},
		},

		MarketEvents: []MarketEventConfig{
			{
				ID:              "earnings_aapl",
				Type:            "earnings",
				ScheduleType:    "fixed",
				ScheduleTime:    "2020-01-28T16:30:00-05:00",
				Instruments:     []string{"AAPL"},
				ImpactMagnitude: 0.03,
				Description:     "Apple Q1 2020 Earnings Report",
			},
		},

		OutputConfig: OutputConfig{
			LogLevel:            "info",
			DataCollectionRate:  "1s",
			OutputFormat:        "csv",
			OutputPath:          "output/",
			Metrics:             []string{"orderbook_depth", "price", "volume", "latency"},
			SnapshotInterval:    "5m",
			CompressOutput:      true,
			EnableMetricsServer: true,
			MetricsPort:         9090,
		},
	}
}

// ExportToKubernetesManifests exports configuration to Kubernetes manifests
func (cs *ConfigService) ExportToKubernetesManifests(outputDir string) error {
	if cs.config == nil {
		return fmt.Errorf("no configuration loaded")
	}

	if !cs.config.Cluster.Enabled {
		return fmt.Errorf("cluster deployment not enabled in configuration")
	}

	// Create output directory if it doesn't exist
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		return fmt.Errorf("failed to create output directory: %v", err)
	}

	// Create CRD manifest
	if err := cs.createSimulationCRD(outputDir); err != nil {
		return fmt.Errorf("failed to create CRD: %v", err)
	}

	// Create Simulation CR manifest
	if err := cs.createSimulationCR(outputDir); err != nil {
		return fmt.Errorf("failed to create Simulation CR: %v", err)
	}

	// Create other Kubernetes resources
	if err := cs.createKubernetesResources(outputDir); err != nil {
		return fmt.Errorf("failed to create Kubernetes resources: %v", err)
	}

	return nil
}

// createSimulationCRD creates the FinancialSimulation CustomResourceDefinition
func (cs *ConfigService) createSimulationCRD(outputDir string) error {
	crd := map[string]interface{}{
		"apiVersion": "apiextensions.k8s.io/v1",
		"kind":       "CustomResourceDefinition",
		"metadata": map[string]interface{}{
			"name": "financialsimulations.sim.financial.io",
		},
		"spec": map[string]interface{}{
			"group": "sim.financial.io",
			"names": map[string]interface{}{
				"kind":     "FinancialSimulation",
				"listKind": "FinancialSimulationList",
				"plural":   "financialsimulations",
				"singular": "financialsimulation",
				"shortNames": []string{
					"fsim",
				},
			},
			"scope": "Namespaced",
			"versions": []map[string]interface{}{
				{
					"name":    "v1",
					"served":  true,
					"storage": true,
					"schema": map[string]interface{}{
						"openAPIV3Schema": map[string]interface{}{
							"type": "object",
							"properties": map[string]interface{}{
								"spec": map[string]interface{}{
									"type": "object",
									"properties": map[string]interface{}{
										"name":             {"type": "string"},
										"description":      {"type": "string"},
										"version":          {"type": "string"},
										"cluster":          {"type": "object"},
										"simulationParams": {"type": "object"},
										"exchanges":        {"type": "array"},
										"instruments":      {"type": "array"},
										"actors":           {"type": "array"},
										"dataSources":      {"type": "array"},
										"marketEvents":     {"type": "array"},
										"outputConfig":     {"type": "object"},
									},
								},
								"status": map[string]interface{}{
									"type": "object",
									"properties": map[string]interface{}{
										"phase":          {"type": "string"},
										"startTime":      {"type": "string", "format": "date-time"},
										"completionTime": {"type": "string", "format": "date-time"},
										"nodesRunning":   {"type": "integer"},
										"conditions":     {"type": "array"},
									},
								},
							},
						},
					},
					"subresources": map[string]interface{}{
						"status": {},
					},
					"additionalPrinterColumns": []map[string]interface{}{
						{
							"name":     "Status",
							"type":     "string",
							"jsonPath": ".status.phase",
						},
						{
							"name":     "Age",
							"type":     "date",
							"jsonPath": ".metadata.creationTimestamp",
						},
					},
				},
			},
		},
	}

	yamlData, err := yaml.Marshal(crd)
	if err != nil {
		return fmt.Errorf("error marshaling CRD: %v", err)
	}

	outputFile := filepath.Join(outputDir, "financial-simulation-crd.yaml")
	if err := ioutil.WriteFile(outputFile, yamlData, 0644); err != nil {
		return fmt.Errorf("error writing CRD file: %v", err)
	}

	return nil
}

// createSimulationCR creates a FinancialSimulation custom resource
func (cs *ConfigService) createSimulationCR(outputDir string) error {
	// Convert our config to a CR
	cr := map[string]interface{}{
		"apiVersion": "sim.financial.io/v1",
		"kind":       "FinancialSimulation",
		"metadata": map[string]interface{}{
			"name":      cs.config.Name,
			"namespace": cs.config.Cluster.Namespace,
		},
		"spec": cs.config,
	}

	yamlData, err := yaml.Marshal(cr)
	if err != nil {
		return fmt.Errorf("error marshaling CR: %v", err)
	}

	outputFile := filepath.Join(outputDir, "simulation-instance.yaml")
	if err := ioutil.WriteFile(outputFile, yamlData, 0644); err != nil {
		return fmt.Errorf("error writing CR file: %v", err)
	}

	return nil
}

// createKubernetesResources creates other Kubernetes resources
func (cs *ConfigService) createKubernetesResources(outputDir string) error {
	// Create namespace
	namespace := map[string]interface{}{
		"apiVersion": "v1",
		"kind":       "Namespace",
		"metadata": map[string]interface{}{
			"name": cs.config.Cluster.Namespace,
		},
	}

	namespaceYAML, err := yaml.Marshal(namespace)
	if err != nil {
		return fmt.Errorf("error marshaling namespace: %v", err)
	}

	namespaceFile := filepath.Join(outputDir, "namespace.yaml")
	if err := ioutil.WriteFile(namespaceFile, namespaceYAML, 0644); err != nil {
		return fmt.Errorf("error writing namespace file: %v", err)
	}

	// Create service account
	serviceAccount := map[string]interface{}{
		"apiVersion": "v1",
		"kind":       "ServiceAccount",
		"metadata": map[string]interface{}{
			"name":      cs.config.Cluster.ServiceAccountName,
			"namespace": cs.config.Cluster.Namespace,
		},
	}

	serviceAccountYAML, err := yaml.Marshal(serviceAccount)
	if err != nil {
		return fmt.Errorf("error marshaling service account: %v", err)
	}

	serviceAccountFile := filepath.Join(outputDir, "service-account.yaml")
	if err := ioutil.WriteFile(serviceAccountFile, serviceAccountYAML, 0644); err != nil {
		return fmt.Errorf("error writing service account file: %v", err)
	}

	// Create controller deployment
	controller := map[string]interface{}{
		"apiVersion": "apps/v1",
		"kind":       "Deployment",
		"metadata": map[string]interface{}{
			"name":      "financial-sim-controller",
			"namespace": cs.config.Cluster.Namespace,
		},
		"spec": map[string]interface{}{
			"replicas": 1,
			"selector": map[string]interface{}{
				"matchLabels": map[string]interface{}{
					"app": "financial-sim-controller",
				},
			},
			"template": map[string]interface{}{
				"metadata": map[string]interface{}{
					"labels": map[string]interface{}{
						"app": "financial-sim-controller",
					},
				},
				"spec": map[string]interface{}{
					"serviceAccountName": cs.config.Cluster.ServiceAccountName,
					"containers": []map[string]interface{}{
						{
							"name":  "controller",
							"image": "financial-sim-controller:latest",
							"resources": map[string]interface{}{
								"limits": map[string]interface{}{
									"cpu":    "500m",
									"memory": "512Mi",
								},
								"requests": map[string]interface{}{
									"cpu":    "100m",
									"memory": "128Mi",
								},
							},
						},
					},
				},
			},
		},
	}

	controllerYAML, err := yaml.Marshal(controller)
	if err != nil {
		return fmt.Errorf("error marshaling controller: %v", err)
	}

	controllerFile := filepath.Join(outputDir, "controller.yaml")
	if err := ioutil.WriteFile(controllerFile, controllerYAML, 0644); err != nil {
		return fmt.Errorf("error writing controller file: %v", err)
	}

	// Create node service
	nodeService := map[string]interface{}{
		"apiVersion": "v1",
		"kind":       "Service",
		"metadata": map[string]interface{}{
			"name":      "financial-sim-nodes",
			"namespace": cs.config.Cluster.Namespace,
		},
		"spec": map[string]interface{}{
			"selector": map[string]interface{}{
				"app": "financial-sim-node",
			},
			"ports": []map[string]interface{}{
				{
					"name":       "grpc",
					"port":       50051,
					"targetPort": 50051,
				},
				{
					"name":       "zeromq",
					"port":       5555,
					"targetPort": 5555,
				},
				{
					"name":       "metrics",
					"port":       9090,
					"targetPort": 9090,
				},
			},
			"clusterIP": "None", // Headless service for direct pod addressing
		},
	}

	nodeServiceYAML, err := yaml.Marshal(nodeService)
	if err != nil {
		return fmt.Errorf("error marshaling node service: %v", err)
	}

	nodeServiceFile := filepath.Join(outputDir, "node-service.yaml")
	if err := ioutil.WriteFile(nodeServiceFile, nodeServiceYAML, 0644); err != nil {
		return fmt.Errorf("error writing node service file: %v", err)
	}

	// Create RBAC roles
	role := map[string]interface{}{
		"apiVersion": "rbac.authorization.k8s.io/v1",
		"kind":       "Role",
		"metadata": map[string]interface{}{
			"name":      "financial-sim-controller-role",
			"namespace": cs.config.Cluster.Namespace,
		},
		"rules": []map[string]interface{}{
			{
				"apiGroups": []string{"sim.financial.io"},
				"resources": []string{"financialsimulations", "financialsimulations/status"},
				"verbs":     []string{"get", "list", "watch", "create", "update", "patch", "delete"},
			},
			{
				"apiGroups": []string{"apps"},
				"resources": []string{"deployments", "statefulsets"},
				"verbs":     []string{"get", "list", "watch", "create", "update", "patch", "delete"},
			},
			{
				"apiGroups": []string{""},
				"resources": []string{"pods", "services", "configmaps", "secrets"},
				"verbs":     []string{"get", "list", "watch", "create", "update", "patch", "delete"},
			},
		},
	}

	roleYAML, err := yaml.Marshal(role)
	if err != nil {
		return fmt.Errorf("error marshaling role: %v", err)
	}

	roleFile := filepath.Join(outputDir, "role.yaml")
	if err := ioutil.WriteFile(roleFile, roleYAML, 0644); err != nil {
		return fmt.Errorf("error writing role file: %v", err)
	}

	// Create role binding
	roleBinding := map[string]interface{}{
		"apiVersion": "rbac.authorization.k8s.io/v1",
		"kind":       "RoleBinding",
		"metadata": map[string]interface{}{
			"name":      "financial-sim-controller-rolebinding",
			"namespace": cs.config.Cluster.Namespace,
		},
		"subjects": []map[string]interface{}{
			{
				"kind":      "ServiceAccount",
				"name":      cs.config.Cluster.ServiceAccountName,
				"namespace": cs.config.Cluster.Namespace,
			},
		},
		"roleRef": map[string]interface{}{
			"apiGroup": "rbac.authorization.k8s.io",
			"kind":     "Role",
			"name":     "financial-sim-controller-role",
		},
	}

	roleBindingYAML, err := yaml.Marshal(roleBinding)
	if err != nil {
		return fmt.Errorf("error marshaling role binding: %v", err)
	}

	roleBindingFile := filepath.Join(outputDir, "role-binding.yaml")
	if err := ioutil.WriteFile(roleBindingFile, roleBindingYAML, 0644); err != nil {
		return fmt.Errorf("error writing role binding file: %v", err)
	}

	return nil
}

// Kubernetes CRD types

// FinancialSimulation is the Kubernetes Custom Resource type
type FinancialSimulation struct {
	metav1.TypeMeta   `json:",inline"`
	metav1.ObjectMeta `json:"metadata,omitempty"`

	Spec   SimulationConfig `json:"spec"`
	Status SimulationStatus `json:"status,omitempty"`
}

// SimulationStatus defines the observed state of FinancialSimulation
type SimulationStatus struct {
	Phase          string       `json:"phase"`
	StartTime      *metav1.Time `json:"startTime,omitempty"`
	CompletionTime *metav1.Time `json:"completionTime,omitempty"`
	NodesRunning   int32        `json:"nodesRunning"`
	Conditions     []Condition  `json:"conditions,omitempty"`
}

// Condition contains details for the current condition of this simulation
type Condition struct {
	Type               string      `json:"type"`
	Status             string      `json:"status"`
	LastTransitionTime metav1.Time `json:"lastTransitionTime,omitempty"`
	Reason             string      `json:"reason,omitempty"`
	Message            string      `json:"message,omitempty"`
}

// FinancialSimulationList contains a list of FinancialSimulation
type FinancialSimulationList struct {
	metav1.TypeMeta `json:",inline"`
	metav1.ListMeta `json:"metadata,omitempty"`
	Items           []FinancialSimulation `json:"items"`
}

// Register the types with the Scheme
var (
	SchemeBuilder = runtime.NewSchemeBuilder(addKnownTypes)
	AddToScheme   = SchemeBuilder.AddToScheme
)

// GroupVersion is the group version used to register these objects
var GroupVersion = schema.GroupVersion{Group: "sim.financial.io", Version: "v1"}

// addKnownTypes adds our types to the API scheme.
func addKnownTypes(scheme *runtime.Scheme) error {
	scheme.AddKnownTypes(GroupVersion,
		&FinancialSimulation{},
		&FinancialSimulationList{},
	)
	metav1.AddToGroupVersion(scheme, GroupVersion)
	return nil
}

// ConfigConverter converts between different configuration formats
type ConfigConverter struct{}

// NewConfigConverter creates a new config converter
func NewConfigConverter() *ConfigConverter {
	return &ConfigConverter{}
}

// ConvertToJSON converts config to JSON format
func (cc *ConfigConverter) ConvertToJSON(config *SimulationConfig) ([]byte, error) {
	return json.MarshalIndent(config, "", "  ")
}

// ConvertToYAML converts config to YAML format
func (cc *ConfigConverter) ConvertToYAML(config *SimulationConfig) ([]byte, error) {
	return yaml.Marshal(config)
}

// LoadFromJSON loads config from JSON
func (cc *ConfigConverter) LoadFromJSON(data []byte) (*SimulationConfig, error) {
	config := &SimulationConfig{}
	if err := json.Unmarshal(data, config); err != nil {
		return nil, err
	}
	return config, nil
}

// LoadFromYAML loads config from YAML
func (cc *ConfigConverter) LoadFromYAML(data []byte) (*SimulationConfig, error) {
	config := &SimulationConfig{}
	if err := yaml.Unmarshal(data, config); err != nil {
		return nil, err
	}
	return config, nil
}

// This allows for easy serialization to Kubernetes manifests
func (s *SimulationConfig) DeepCopyInto(out *SimulationConfig) {
	// Deep copy implementation would copy all fields
	// For simplicity, we'll use JSON marshaling as an easy deep copy method
	data, err := json.Marshal(s)
	if err != nil {
		log.Printf("Error in DeepCopyInto: %v", err)
		return
	}

	if err := json.Unmarshal(data, out); err != nil {
		log.Printf("Error in DeepCopyInto: %v", err)
	}
}

// DeepCopy creates a deep copy of the SimulationConfig
func (s *SimulationConfig) DeepCopy() *SimulationConfig {
	if s == nil {
		return nil
	}
	out := new(SimulationConfig)
	s.DeepCopyInto(out)
	return out
}
