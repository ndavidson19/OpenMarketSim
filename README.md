# Open Market Simulator

![Open Market Simulator](logo.png)
![Build Status](https://img.shields.io/github/workflow/status/yourusername/financial-sim/CI)
![License](https://img.shields.io/github/license/yourusername/financial-sim)
![Version](https://img.shields.io/github/v/release/yourusername/financial-sim)

# Financial Market Simulation

## Overview

This project implements a high-performance, multi-agent financial market simulation designed for realistic market microstructure modeling and algorithmic trading research. Built with a hybrid C++/Go architecture, it provides a detailed simulation of order books, market participants, and trading dynamics while interfacing with popular machine learning frameworks.

## Key Features

- **High-fidelity market microstructure** with realistic order book dynamics
- **Diverse actor ecosystem** with multiple trading strategies and behaviors
- **Option and derivative markets** with advanced pricing models
- **Market friction modeling** including latency, slippage, and transaction costs
- **Machine learning compatibility** via OpenAI Gym and Ray RLlib interfaces
- **Distributed simulation capabilities** for large-scale experiments
- **Real-time visualization tools** for market monitoring and analysis

## System Architecture

The system is built using a layered architecture:

### Core Simulation (C++)

- **Event Engine**: Nanosecond-precision virtual clock and event scheduling
- **Order Book**: Efficient limit order book with price-time priority
- **Matching Engine**: Order execution with support for various order types
- **Actor Framework**: Base system for implementing market participants
- **Options Pricing**: Black-Scholes, Binomial Trees, and Heston models
- **Risk Management**: Position tracking and risk metric calculation

### Orchestration Layer (Go)

- **Node Management**: Distributed simulation coordination
- **Workload Distribution**: Efficient task allocation across nodes
- **Synchronization**: Clock synchronization for multi-node simulation
- **Data Collection**: Centralized metrics aggregation and storage

### Python Interface

- **OpenAI Gym**: Standard reinforcement learning environment
- **Ray RLlib**: Distributed RL training interface
- **Visualization Tools**: Real-time monitoring and playback tools
- **Analysis Utilities**: Market data processing and performance evaluation

## Market Actors

The simulation features several types of market participants:

### Market Makers

Market makers provide liquidity by maintaining two-sided quotes (bids and asks). They aim to profit from the bid-ask spread while managing inventory risk.

Key behaviors:
- Adaptive quote placement based on volatility
- Position-based quote skewing
- Dynamic spread adjustment
- Inventory risk management

### High-Frequency Traders

HFTs use ultra-low-latency strategies to capture small price inefficiencies. They typically have minimal overnight positions.

Key behaviors:
- Order book imbalance trading
- Statistical arbitrage across correlated instruments
- Large order detection and front-running
- Short-term momentum trading

### Trend Followers

These actors identify and follow price trends, typically using moving averages and momentum indicators.

Key behaviors:
- Multi-timeframe trend identification
- Position sizing based on trend strength
- Stop loss placement and trailing stops
- Breakout detection

### Retail Traders

Retail traders represent individual investors with behavioral biases and less sophisticated execution.

Key behaviors:
- FOMO (Fear Of Missing Out) during strong trends
- Panic selling during market drops
- Anchoring to purchase prices
- Loss aversion and disposition effect

### Institutional Investors

These represent large funds executing significant position changes over time.

Key behaviors:
- Large order execution with minimal market impact
- TWAP/VWAP execution algorithms
- Strategic order splitting
- Block trading

## Market Environment

### Order Types Supported

- **Market Orders**: Executed immediately at best available price
- **Limit Orders**: Executed only at specified price or better
- **Stop Orders**: Become market orders when price reaches trigger level
- **Iceberg Orders**: Large orders with hidden quantity
- **Fill-or-Kill**: Must be filled completely or canceled
- **Immediate-or-Cancel**: Fill what's possible immediately, cancel the rest

### Market Frictions

The simulation models realistic market frictions:

- **Latency**: Network transmission and processing delays
- **Transaction Costs**: Exchange fees and broker commissions
- **Slippage**: Price impact from large orders
- **Information Asymmetry**: Varying information quality among actors

## Machine Learning Interface

The system provides a standard Gym environment interface for reinforcement learning:

```python
# Basic usage
import gym
import financial_market_gym

env = gym.make('financial_market')
observation = env.reset()

for _ in range(1000):
    action = my_agent.decide(observation)
    observation, reward, done, info = env.step(action)
    if done:
        observation = env.reset()
```

### Observation Space

The observation space includes:
- Order book data (price levels and volumes)
- Market data (prices, spreads, volatility)
- Position and portfolio information

### Action Space

Actions represent trading decisions including:
- Order type (market, limit, etc.)
- Direction (buy/sell)
- Price (for limit orders)
- Size (quantity to trade)

### Rewards

Customizable reward functions based on:
- Profit and loss (PnL)
- Sharpe ratio
- Execution quality
- Risk-adjusted returns

## Visualization Tools

The system includes visualization tools for monitoring and analyzing simulations:

- **Real-time Market Monitor**: Live order book, price, and volume charts
- **Portfolio Tracker**: Position and P&L monitoring
- **Performance Analytics**: Key metrics and statistics
- **Simulation Playback**: Record and replay simulation runs

## Getting Started

### Prerequisites

- C++17 compatible compiler
- Go 1.16+
- Python 3.7+
- CMake 3.14+

### Installation

```bash
# Clone the repository
git clone https://github.com/yourusername/financial-market-sim.git
cd financial-market-sim

# Build C++ core
mkdir build && cd build
cmake ..
make -j4

# Install Python package
cd ../python
pip install -e .
```

### Basic Usage

```python
from financial_market_gym import FinancialMarketEnv

# Create environment with configuration
env = FinancialMarketEnv(config={
    'instruments': ['AAPL', 'MSFT', 'GOOGL'],
    'initial_capital': 1000000,
    'episode_length': 1000,
    'num_market_makers': 3,
    'num_hfts': 2
})

# Run a simple loop
observation = env.reset()
for i in range(100):
    action = {
        'type': 'LIMIT',
        'direction': 'BUY',
        'price_offset': -0.001,  # Slightly below market
        'size': 0.1,  # 10% of available capital
        'instrument_id': 'AAPL'
    }
    observation, reward, done, info = env.step(action)
    print(f"Step {i}, Reward: {reward}")
```

## Advanced Configuration

The simulation supports extensive configuration options:

```python
config = {
    # Market structure
    'instruments': ['AAPL', 'MSFT', 'GOOGL', 'AMZN', 'META'],
    'initial_prices': [150.0, 250.0, 2000.0, 100.0, 300.0],
    'trading_hours': {'start': '09:30:00', 'end': '16:00:00'},
    'tick_size': 0.01,
    
    # Market participants
    'num_market_makers': 5,
    'num_hfts': 3,
    'num_trend_followers': 7,
    'num_retail': 15,
    'num_institutional': 2,
    
    # Agent configuration
    'initial_capital': 1000000.0,
    'max_position': 500000.0,
    
    # Market dynamics
    'volatility': 'medium',  # low, medium, high, extreme
    'liquidity': 'normal',   # low, normal, high
    'enable_circuit_breakers': True,
    
    # Friction models
    'latency': {'base': 0.001, 'jitter': 0.0005},  # in seconds
    'transaction_costs': {'maker': -0.0002, 'taker': 0.0003},
    'slippage_model': 'square_root',
    
    # RL configuration
    'observation_interval': '1m',
    'reward_function': 'sharpe',
    'episode_length': 1000
}
```

## Project Structure

```
financial-market-sim/
├── cpp/                  # C++ core implementation
│   ├── include/          # Header files
│   ├── src/              # Source files
│   └── tests/            # Unit tests
├── go/                   # Go orchestration layer
│   ├── manager/          # Node management
│   ├── api/              # API service
│   └── metrics/          # Metrics collection
├── python/               # Python bindings and tools
│   ├── financial_market_gym/  # Gym environment
│   ├── visualization/    # Visualization tools
│   └── examples/         # Example scripts
├── docs/                 # Documentation
└── scripts/              # Utility scripts
```

## Performance Considerations

The system is designed for high performance:

- **C++ Core**: Efficient data structures and algorithms
- **Memory Management**: Custom allocators for minimal GC overhead
- **Parallelism**: Multi-threaded execution where appropriate
- **Distributed Operation**: Scale across multiple nodes

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

- This project draws inspiration from real market microstructure research
- Special thanks to the open-source communities around Gym, RLlib, and financial modeling

## Future Work

- Support for more asset classes (futures, forex)
- Improved market impact models
- Machine learning for realistic agent behavior
- Enhanced visualization capabilities