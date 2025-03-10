// actor.hpp
#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <random>
#include "engine.hpp"
#include "order_book.hpp"

namespace sim {

// Forward declarations
class Exchange;
class Order;
class SimulationEngine;

/**
 * @brief Market observation containing current market state
 */
struct MarketObservation {
    std::string instrument_id;
    Timestamp timestamp;
    std::optional<double> best_bid;
    std::optional<double> best_ask;
    std::vector<std::pair<double, double>> bid_levels;  // price, volume
    std::vector<std::pair<double, double>> ask_levels;  // price, volume
    std::optional<double> last_trade_price;
    std::optional<double> last_trade_volume;
    double daily_volume;
    double daily_high;
    double daily_low;
    double daily_open;
};

/**
 * @brief Position information for an instrument
 */
struct Position {
    std::string instrument_id;
    double quantity;
    double avg_price;
    double realized_pnl;
    double unrealized_pnl;
    Timestamp last_update;
};

/**
 * @brief Base actor class for all market participants
 */
class Actor {
public:
    Actor(std::string id, double initial_capital);
    virtual ~Actor() = default;
    
    // Called when the actor is registered with the simulation
    virtual void initialize(SimulationEngine& engine);
    
    // Actor identifier
    const std::string& id() const { return id_; }
    
    // Actor capital and positions
    double capital() const { return capital_; }
    const std::unordered_map<std::string, Position>& positions() const { return positions_; }
    
    // Called when a market observation is available
    virtual void on_market_observation(const MarketObservation& observation);
    
    // Called when one of actor's orders is executed
    virtual void on_order_executed(std::shared_ptr<Order> order, const Trade& trade);
    
    // Called when a market event occurs (e.g., news)
    virtual void on_market_event(const std::string& event_type, const std::unordered_map<std::string, std::string>& event_data);
    
    // Called periodically to make trading decisions
    virtual void make_trading_decisions(SimulationEngine& engine) = 0;
    
    // Schedule the next decision time
    void schedule_next_decision(SimulationEngine& engine, Timestamp delay);
    
    // Submit an order to an exchange
    void submit_order(std::shared_ptr<Exchange> exchange, std::shared_ptr<Order> order);
    
    // Cancel an order
    void cancel_order(std::shared_ptr<Exchange> exchange, const std::string& order_id);
    
    // Update position after a trade
    void update_position(const Trade& trade);
    
    // Calculate current portfolio value
    double portfolio_value(const std::unordered_map<std::string, double>& current_prices) const;
    
    // Calculate risk metrics
    double calculate_var(double confidence_level, const std::unordered_map<std::string, std::vector<double>>& returns_history) const;
    
protected:
    std::string id_;
    double capital_;
    std::unordered_map<std::string, Position> positions_;
    std::unordered_map<std::string, std::shared_ptr<Order>> active_orders_;
    std::mt19937 rng_;  // Random number generator
    
    // Utility functions for derived classes
    double calculate_position_value(const std::string& instrument_id, double current_price) const;
    double calculate_unrealized_pnl(const std::string& instrument_id, double current_price) const;
};

/**
 * @brief Market maker actor that provides liquidity on both sides
 */
class MarketMaker : public Actor {
public:
    MarketMaker(std::string id, double initial_capital, 
                double target_spread, double max_position, 
                double order_size_pct);
    
    // Initialize with market making parameters
    void initialize(SimulationEngine& engine) override;
    
    // React to market observations
    void on_market_observation(const MarketObservation& observation) override;
    
    // Make market making decisions
    void make_trading_decisions(SimulationEngine& engine) override;
    
    // React to order executions
    void on_order_executed(std::shared_ptr<Order> order, const Trade& trade) override;
    
private:
    // Market making parameters
    double target_spread_;
    double max_position_;
    double order_size_pct_;
    
    // Current market making state
    std::unordered_map<std::string, MarketObservation> latest_observations_;
    std::unordered_map<std::string, std::pair<std::string, std::string>> active_quotes_;  // instrument -> (bid_id, ask_id)
    
    // Market making logic
    void adjust_quotes(SimulationEngine& engine, const std::string& instrument_id);
    double calculate_bid_price(const MarketObservation& obs);
    double calculate_ask_price(const MarketObservation& obs);
    double calculate_order_size(const std::string& instrument_id);
    
    // Risk management
    void manage_inventory(SimulationEngine& engine, const std::string& instrument_id);
    bool should_skew_quotes(const std::string& instrument_id);
};

/**
 * @brief Trend follower actor that follows price momentum
 */
class TrendFollower : public Actor {
public:
    TrendFollower(std::string id, double initial_capital, 
                 int short_window, int long_window,
                 double position_size_pct);
    
    // Initialize with trend following parameters
    void initialize(SimulationEngine& engine) override;
    
    // Record price observations for trend calculation
    void on_market_observation(const MarketObservation& observation) override;
    
    // Make trend following decisions
    void make_trading_decisions(SimulationEngine& engine) override;
    
private:
    // Trend following parameters
    int short_window_;
    int long_window_;
    double position_size_pct_;
    
    // Price history for trend calculation
    std::unordered_map<std::string, std::vector<double>> price_history_;
    
    // Current trading signals
    std::unordered_map<std::string, double> trend_signals_;  // instrument -> signal [-1,1]
    
    // Trend calculation logic
    double calculate_trend_signal(const std::string& instrument_id);
    double calculate_simple_moving_average(const std::vector<double>& prices, int window);
    
    // Position sizing
    double calculate_position_size(const std::string& instrument_id, double signal_strength);
};

/**
 * @brief High-frequency trader with ultra-low latency
 */
class HighFrequencyTrader : public Actor {
public:
    HighFrequencyTrader(std::string id, double initial_capital, 
                        double min_edge, Timestamp reaction_time);
    
    // Initialize with HFT parameters
    void initialize(SimulationEngine& engine) override;
    
    // React quickly to market observations
    void on_market_observation(const MarketObservation& observation) override;
    
    // Make ultra-fast trading decisions
    void make_trading_decisions(SimulationEngine& engine) override;
    
    // React to order executions
    void on_order_executed(std::shared_ptr<Order> order, const Trade& trade) override;
    
private:
    // HFT parameters
    double min_edge_;           // Minimum required edge to trade
    Timestamp reaction_time_;   // How quickly the HFT reacts
    
    // Market microstructure state
    std::unordered_map<std::string, std::vector<MarketObservation>> recent_observations_;
    std::unordered_map<std::string, std::unordered_map<double, double>> order_book_imbalance_;  // instrument -> (price -> imbalance)
    
    // Order book analysis
    double calculate_order_book_imbalance(const MarketObservation& obs);
    bool detect_large_order(const MarketObservation& obs);
    std::optional<double> predict_short_term_direction(const std::string& instrument_id);
    
    // Order execution strategies
    void place_opportunistic_orders(SimulationEngine& engine, const std::string& instrument_id);
    void execute_statistical_arbitrage(SimulationEngine& engine, const std::vector<std::string>& correlated_instruments);
};

/**
 * @brief Retail trader with behavioral biases
 */
class RetailTrader : public Actor {
public:
    RetailTrader(std::string id, double initial_capital,
                double risk_aversion, double fomo_susceptibility,
                double loss_aversion);
    
    // Initialize with behavioral parameters
    void initialize(SimulationEngine& engine) override;
    
    // React to market observations
    void on_market_observation(const MarketObservation& observation) override;
    
    // Make trading decisions with behavioral biases
    void make_trading_decisions(SimulationEngine& engine) override;
    
    // React to market events (e.g., news)
    void on_market_event(const std::string& event_type, const std::unordered_map<std::string, std::string>& event_data) override;
    
private:
    // Behavioral parameters
    double risk_aversion_;         // How risk-averse the trader is [0-1]
    double fomo_susceptibility_;   // How susceptible to FOMO [0-1]
    double loss_aversion_;         // How loss-averse (Kahneman-Tversky) [1-3]
    
    // Psychological state
    double market_sentiment_;      // Current sentiment about the market [-1,1]
    std::unordered_map<std::string, double> asset_sentiment_;  // Per-asset sentiment
    
    // Decision making logic
    bool should_buy_due_to_fomo(const std::string& instrument_id);
    bool should_sell_due_to_panic(const std::string& instrument_id);
    double calculate_position_size_with_biases(const std::string& instrument_id, bool is_buying);
    void update_sentiment(const std::string& instrument_id, double price_change_pct);