// exchange.hpp
#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <queue>
#include "engine.hpp"
#include "order_book.hpp"

namespace sim {

/**
 * @brief Market data update event
 */
struct MarketDataUpdate {
    std::string instrument_id;
    Timestamp timestamp;
    std::optional<double> best_bid;
    std::optional<double> best_ask;
    std::vector<std::pair<double, double>> bid_levels;
    std::vector<std::pair<double, double>> ask_levels;
    std::optional<Trade> last_trade;
};

/**
 * @brief Latency model for network and processing delays
 */
class LatencyModel {
public:
    LatencyModel(Timestamp base_latency, Timestamp jitter, double congestion_factor);
    
    // Calculate delay for an order sent from an actor to an exchange
    Timestamp calculate_order_latency(const std::string& actor_id, const std::string& exchange_id);
    
    // Calculate delay for market data sent from exchange to an actor
    Timestamp calculate_market_data_latency(const std::string& exchange_id, const std::string& actor_id);
    
    // Update congestion level (called during high activity periods)
    void update_congestion(double congestion_level);
    
private:
    Timestamp base_latency_;
    Timestamp jitter_;
    double congestion_factor_;
    double current_congestion_;
    std::mt19937 rng_;
};

/**
 * @brief Fee model for transaction costs
 */
class FeeModel {
public:
    FeeModel(double maker_rate, double taker_rate, double min_fee);
    
    // Calculate fee for a trade
    double calculate_fee(const Trade& trade, bool is_maker);
    
private:
    double maker_rate_;  // Fee rate for passive (maker) orders
    double taker_rate_;  // Fee rate for aggressive (taker) orders
    double min_fee_;     // Minimum fee per trade
};

/**
 * @brief Order submission event
 */
class OrderSubmissionEvent : public Event {
public:
    OrderSubmissionEvent(Timestamp timestamp, 
                        std::shared_ptr<Order> order,
                        std::string actor_id,
                        std::string exchange_id);
    
    void process(SimulationEngine& engine) override;
    
private:
    std::shared_ptr<Order> order_;
    std::string actor_id_;
    std::string exchange_id_;
};

/**
 * @brief Order cancellation event
 */
class OrderCancellationEvent : public Event {
public:
    OrderCancellationEvent(Timestamp timestamp,
                          std::string order_id,
                          std::string actor_id,
                          std::string exchange_id);
    
    void process(SimulationEngine& engine) override;
    
private:
    std::string order_id_;
    std::string actor_id_;
    std::string exchange_id_;
};

/**
 * @brief Market data dissemination event
 */
class MarketDataEvent : public Event {
public:
    MarketDataEvent(Timestamp timestamp,
                   MarketDataUpdate update,
                   std::string exchange_id,
                   std::string target_actor_id = "");
    
    void process(SimulationEngine& engine) override;
    
private:
    MarketDataUpdate update_;
    std::string exchange_id_;
    std::string target_actor_id_;  // Empty means broadcast to all subscribed actors
};

/**
 * @brief Exchange class representing a trading venue
 */
class Exchange {
public:
    Exchange(std::string id, SimulationEngine& engine);
    
    // Exchange identifier
    const std::string& id() const { return id_; }
    
    // Add a new tradable instrument to the exchange
    void add_instrument(const std::string& instrument_id, double initial_price);
    
    // Process an incoming order submission
    void process_order_submission(std::shared_ptr<Order> order, const std::string& actor_id);
    
    // Process an order cancellation
    void process_order_cancellation(const std::string& order_id, const std::string& actor_id);
    
    // Subscribe an actor to market data for an instrument
    void subscribe_to_market_data(const std::string& actor_id, const std::string& instrument_id);
    
    // Unsubscribe from market data
    void unsubscribe_from_market_data(const std::string& actor_id, const std::string& instrument_id);
    
    // Get the order book for an instrument
    std::shared_ptr<OrderBook> get_order_book(const std::string& instrument_id);
    
    // Get current market data for an instrument
    MarketDataUpdate get_market_data(const std::string& instrument_id);
    
    // Set the latency model for the exchange
    void set_latency_model(std::shared_ptr<LatencyModel> model);
    
    // Set the fee model for the exchange
    void set_fee_model(std::shared_ptr<FeeModel> model);
    
    // Open the exchange for trading
    void open_trading_session();
    
    // Close the exchange for trading
    void close_trading_session();
    
    // Check if the exchange is open for trading
    bool is_trading_open() const { return is_trading_open_; }
    
private:
    std::string id_;
    SimulationEngine& engine_;
    
    // Order books for each instrument
    std::unordered_map<std::string, std::shared_ptr<OrderBook>> order_books_;
    
    // Matching engines for each instrument
    std::unordered_map<std::string, std::shared_ptr<MatchingEngine>> matching_engines_;
    
    // Market data subscriptions (actor_id -> set of instrument_ids)
    std::unordered_map<std::string, std::unordered_set<std::string>> market_data_subscriptions_;
    
    // Last trades for each instrument
    std::unordered_map<std::string, Trade> last_trades_;
    
    // Trading session state
    bool is_trading_open_;
    
    // Models for realistic market frictions
    std::shared_ptr<LatencyModel> latency_model_;
    std::shared_ptr<FeeModel> fee_model_;
    
    // Internal methods
    void broadcast_market_data(const std::string& instrument_id);
    void process_trade(const Trade& trade);
    void apply_fees(const Trade& trade);
    
    // Callback for order book updates
    void on_order_book_update(const OrderBook& book, const std::shared_ptr<Order>& order, const std::optional<Trade>& trade);
};

// Implementation of key methods follows

void Exchange::process_order_submission(std::shared_ptr<Order> order, const std::string& actor_id) {
    // Check if trading is open
    if (!is_trading_open_) {
        // Reject the order and notify the actor
        order->set_status(OrderStatus::REJECTED);
        // Schedule notification with appropriate latency
        return;
    }
    
    // Get the matching engine for the instrument
    auto instrument_id = order->instrument_id();
    auto it = matching_engines_.find(instrument_id);
    if (it == matching_engines_.end()) {
        // Unknown instrument - reject the order
        order->set_status(OrderStatus::REJECTED);
        // Schedule notification with appropriate latency
        return;
    }
    
    // Process the order through the matching engine
    auto& matching_engine = it->second;
    auto trades = matching_engine->process_order(order);
    
    // Process resulting trades
    for (const auto& trade : trades) {
        process_trade(trade);
    }
    
    // Broadcast updated market data
    broadcast_market_data(instrument_id);
}

void Exchange::process_order_cancellation(const std::string& order_id, const std::string& actor_id) {
    // Find which order book contains this order
    for (auto& [instrument_id, matching_engine] : matching_engines_) {
        if (matching_engine->cancel_order(order_id)) {
            // Order found and canceled
            broadcast_market_data(instrument_id);
            return;
        }
    }
    
    // Order not found - no action needed
}

void Exchange::broadcast_market_data(const std::string& instrument_id) {
    auto order_book = order_books_.at(instrument_id);
    
    // Create market data update
    MarketDataUpdate update;
    update.instrument_id = instrument_id;
    update.timestamp = engine_.now();
    update.best_bid = order_book->best_bid();
    update.best_ask = order_book->best_ask();
    update.bid_levels = order_book->get_bids(10);  // Top 10 levels
    update.ask_levels = order_book->get_asks(10);  // Top 10 levels
    
    // Include last trade if available
    auto last_trade_it = last_trades_.find(instrument_id);
    if (last_trade_it != last_trades_.end()) {
        update.last_trade = last_trade_it->second;
    }
    
    // Send to all subscribed actors with appropriate latencies
    for (auto& [actor_id, subscriptions] : market_data_subscriptions_) {
        if (subscriptions.find(instrument_id) != subscriptions.end()) {
            // Calculate latency for this actor
            Timestamp latency;
            if (latency_model_) {
                latency = latency_model_->calculate_market_data_latency(id_, actor_id);
            } else {
                latency = Timestamp(0);  // No latency model, instant delivery
            }
            
            // Schedule market data event
            auto event = std::make_shared<MarketDataEvent>(
                engine_.now() + latency,
                update,
                id_,
                actor_id
            );
            engine_.schedule_event(event);
        }
    }
}

void Exchange::process_trade(const Trade& trade) {
    // Record the last trade
    last_trades_[trade.instrument_id] = trade;
    
    // Apply fees
    apply_fees(trade);
    
    // Schedule notifications to the involved actors
    // (implementation depends on how actors are notified)
}

void Exchange::apply_fees(const Trade& trade) {
    if (!fee_model_) {
        return;  // No fee model set
    }
    
    // Calculate and apply fees for buyer and seller
    // Implementation depends on how fees are tracked and applied
}

} // namespace sim