// actor.cpp
#include "actor.hpp"
#include "exchange.hpp"
#include "order_book.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace sim {

// Base Actor implementation
Actor::Actor(std::string id, double initial_capital)
    : id_(std::move(id)), capital_(initial_capital) {
    
    // Initialize random number generator with actor-specific seed
    std::random_device rd;
    rng_.seed(rd());
}

void Actor::initialize(SimulationEngine& engine) {
    // Default implementation does nothing
}

void Actor::on_market_observation(const MarketObservation& observation) {
    // Default implementation does nothing
}

void Actor::on_order_executed(std::shared_ptr<Order> order, const Trade& trade) {
    // Update position based on the trade
    update_position(trade);
    
    // Remove from active orders if fully executed
    if (order->status() == OrderStatus::FILLED) {
        active_orders_.erase(order->order_id());
    }
}

void Actor::on_market_event(const std::string& event_type, const std::unordered_map<std::string, std::string>& event_data) {
    // Default implementation does nothing
}

void Actor::schedule_next_decision(SimulationEngine& engine, Timestamp delay) {
    // Create and schedule a decision event
    struct DecisionEvent : public Event {
        DecisionEvent(Timestamp timestamp, Actor& actor, SimulationEngine& engine)
            : Event(timestamp), actor_(actor), engine_(engine) {}
        
        void process(SimulationEngine& engine) override {
            actor_.make_trading_decisions(engine_);
        }
        
    private:
        Actor& actor_;
        SimulationEngine& engine_;
    };
    
    auto event = std::make_shared<DecisionEvent>(
        engine.now() + delay,
        *this,
        engine
    );
    
    engine.schedule_event(event);
}

void Actor::submit_order(std::shared_ptr<Exchange> exchange, std::shared_ptr<Order> order) {
    // Add to active orders
    active_orders_[order->order_id()] = order;
    
    // Submit to exchange
    exchange->process_order_submission(order, id_);
}

void Actor::cancel_order(std::shared_ptr<Exchange> exchange, const std::string& order_id) {
    // Check if order is active
    if (active_orders_.find(order_id) == active_orders_.end()) {
        return;  // Order not found or already executed/canceled
    }
    
    // Cancel at exchange
    exchange->process_order_cancellation(order_id, id_);
    
    // Remove from active orders
    active_orders_.erase(order_id);
}

void Actor::update_position(const Trade& trade) {
    // Determine if we're the buyer or seller
    bool is_buyer = (trade.buyer_id == id_);
    
    // Get position for this instrument
    auto& position = positions_[trade.instrument_id];
    
    // Update position quantity
    double old_quantity = position.quantity;
    double old_avg_price = position.avg_price;
    
    if (is_buyer) {
        // Add to position
        double new_quantity = old_quantity + trade.quantity;
        
        // Calculate new average price
        if (old_quantity > 0) {
            position.avg_price = (old_quantity * old_avg_price + trade.quantity * trade.price) / new_quantity;
        } else {
            // Short position becoming less short or going long
            if (new_quantity > 0) {
                // Crossed zero - realized P&L on the short side
                double short_qty = std::min(trade.quantity, -old_quantity);
                position.realized_pnl += short_qty * (old_avg_price - trade.price);
                
                // Calculate avg price for remaining long position
                double long_qty = trade.quantity - short_qty;
                position.avg_price = long_qty > 0 ? trade.price : 0.0;
            } else {
                // Still short, but less short
                position.realized_pnl += trade.quantity * (old_avg_price - trade.price);
                position.avg_price = old_avg_price;  // Avg price unchanged for short
            }
        }
        
        // Update position quantity
        position.quantity = new_quantity;
        
        // Update capital (money spent)
        capital_ -= trade.quantity * trade.price;
    } else {
        // Reduce position (selling)
        double new_quantity = old_quantity - trade.quantity;
        
        // Calculate P&L and update position
        if (old_quantity > 0) {
            // Long position becoming less long or going short
            if (new_quantity < 0) {
                // Crossed zero - realized P&L on the long side
                double long_qty = std::min(trade.quantity, old_quantity);
                position.realized_pnl += long_qty * (trade.price - old_avg_price);
                
                // Calculate avg price for remaining short position
                double short_qty = trade.quantity - long_qty;
                position.avg_price = short_qty > 0 ? trade.price : 0.0;
            } else {
                // Still long, but less long
                position.realized_pnl += trade.quantity * (trade.price - old_avg_price);
                position.avg_price = old_avg_price;  // Avg price unchanged for long
            }
        } else {
            // Short position becoming more short
            double new_quantity = old_quantity - trade.quantity;
            if (old_quantity < 0) {
                position.avg_price = (old_quantity * old_avg_price - trade.quantity * trade.price) / new_quantity;
            } else {
                position.avg_price = trade.price;
            }
        }
        
        // Update position quantity
        position.quantity = new_quantity;
        
        // Update capital (money received)
        capital_ += trade.quantity * trade.price;
    }
    
    // Update timestamp
    position.last_update = trade.timestamp;
    position.instrument_id = trade.instrument_id;
}

double Actor::portfolio_value(const std::unordered_map<std::string, double>& current_prices) const {
    double total_value = capital_;
    
    for (const auto& [instrument_id, position] : positions_) {
        auto price_it = current_prices.find(instrument_id);
        if (price_it != current_prices.end()) {
            total_value += position.quantity * price_it->second;
        }
    }
    
    return total_value;
}

double Actor::calculate_var(double confidence_level, const std::unordered_map<std::string, std::vector<double>>& returns_history) const {
    // Implement Value at Risk calculation
    // This is a simplified implementation using historical simulation method
    
    // Collect historical portfolio returns
    std::vector<double> portfolio_returns;
    
    // For each historical period
    if (!returns_history.empty()) {
        // Get first instrument to determine number of periods
        const auto& first_instrument = returns_history.begin()->second;
        size_t num_periods = first_instrument.size();
        
        portfolio_returns.resize(num_periods, 0.0);
        
        // For each position
        for (const auto& [instrument_id, position] : positions_) {
            auto it = returns_history.find(instrument_id);
            if (it != returns_history.end()) {
                const auto& instrument_returns = it->second;
                
                // Add weighted returns to portfolio returns
                for (size_t i = 0; i < std::min(num_periods, instrument_returns.size()); ++i) {
                    portfolio_returns[i] += position.quantity * instrument_returns[i];
                }
            }
        }
    }
    
    if (portfolio_returns.empty()) {
        return 0.0;  // No data to calculate VaR
    }
    
    // Sort returns to find percentile
    std::sort(portfolio_returns.begin(), portfolio_returns.end());
    
    // Find the VaR at the specified confidence level
    size_t index = static_cast<size_t>(portfolio_returns.size() * (1.0 - confidence_level));
    if (index >= portfolio_returns.size()) {
        index = portfolio_returns.size() - 1;
    }
    
    return -portfolio_returns[index];  // Return positive value for loss
}

double Actor::calculate_position_value(const std::string& instrument_id, double current_price) const {
    auto it = positions_.find(instrument_id);
    if (it == positions_.end()) {
        return 0.0;  // No position
    }
    
    return it->second.quantity * current_price;
}

double Actor::calculate_unrealized_pnl(const std::string& instrument_id, double current_price) const {
    auto it = positions_.find(instrument_id);
    if (it == positions_.end()) {
        return 0.0;  // No position
    }
    
    const auto& position = it->second;
    if (position.quantity == 0) {
        return 0.0;  // No position
    }
    
    return position.quantity * (current_price - position.avg_price);
}

// MarketMaker implementation
MarketMaker::MarketMaker(std::string id, double initial_capital,
                         double target_spread, double max_position,
                         double order_size_pct)
    : Actor(std::move(id), initial_capital),
      target_spread_(target_spread),
      max_position_(max_position),
      order_size_pct_(order_size_pct) {
}

void MarketMaker::initialize(SimulationEngine& engine) {
    Actor::initialize(engine);
    
    // Schedule first decision
    schedule_next_decision(engine, Timestamp(0));
}

void MarketMaker::on_market_observation(const MarketObservation& observation) {
    Actor::on_market_observation(observation);
    
    // Update our latest observation for this instrument
    latest_observations_[observation.instrument_id] = observation;
}

void MarketMaker::make_trading_decisions(SimulationEngine& engine) {
    // Cancel existing quotes and place new ones for each instrument
    for (const auto& [instrument_id, observation] : latest_observations_) {
        adjust_quotes(engine, instrument_id);
    }
    
    // Schedule next decision
    std::uniform_real_distribution<double> dist(0.8, 1.2);  // Random jitter for decision timing
    Timestamp next_decision_delay = Timestamp(static_cast<int64_t>(dist(rng_) * 1000000000));  // ~1 second with jitter
    schedule_next_decision(engine, next_decision_delay);
}

void MarketMaker::on_order_executed(std::shared_ptr<Order> order, const Trade& trade) {
    Actor::on_order_executed(order, trade);
    
    // Remove executed quote from active quotes
    for (auto& [instrument_id, quotes] : active_quotes_) {
        if (quotes.first == order->order_id()) {
            quotes.first = "";  // Clear bid quote ID
        }
        if (quotes.second == order->order_id()) {
            quotes.second = "";  // Clear ask quote ID
        }
    }
    
    // Rebalance immediately if needed
    std::string instrument_id = order->instrument_id();
    if (positions_[instrument_id].quantity > max_position_ || 
        positions_[instrument_id].quantity < -max_position_) {
        manage_inventory(engine, instrument_id);
    }
}

void MarketMaker::adjust_quotes(SimulationEngine& engine, const std::string& instrument_id) {
    // Get the latest observation
    auto obs_it = latest_observations_.find(instrument_id);
    if (obs_it == latest_observations_.end()) {
        return;  // No observation available
    }
    
    const auto& obs = obs_it->second;
    
    // Check if we have a valid market (both bid and ask)
    if (!obs.best_bid || !obs.best_ask) {
        return;  // Market is too thin
    }
    
    // Cancel existing quotes
    auto quotes_it = active_quotes_.find(instrument_id);
    if (quotes_it != active_quotes_.end()) {
        auto& [bid_id, ask_id] = quotes_it->second;
        
        // Find exchange for this instrument (simplified - using first exchange)
        auto exchange = std::shared_ptr<Exchange>(nullptr);  // Placeholder
        
        // Would need to find actual exchange in a real implementation
        // For now, assume we have a reference to the right exchange
        
        if (!bid_id.empty()) {
            cancel_order(exchange, bid_id);
            bid_id = "";
        }
        if (!ask_id.empty()) {
            cancel_order(exchange, ask_id);
            ask_id = "";
        }
    }
    
    // Calculate new quote prices
    double bid_price = calculate_bid_price(obs);
    double ask_price = calculate_ask_price(obs);
    
    // Calculate order size
    double order_size = calculate_order_size(instrument_id);
    
    // Skip if size is too small
    if (order_size <= 0) {
        return;
    }
    
    // Find exchange for this instrument (simplified - using first exchange)
    auto exchange = std::shared_ptr<Exchange>(nullptr);  // Placeholder
    
    // Create and submit orders
    bool should_skew = should_skew_quotes(instrument_id);
    
    // If inventory suggests skewing quotes, adjust accordingly
    if (should_skew) {
        auto pos_it = positions_.find(instrument_id);
        double position = (pos_it != positions_.end()) ? pos_it->second.quantity : 0.0;
        
        if (position > 0) {
            // Long position - skew to sell more / buy less
            ask_price = std::max(ask_price * 0.99, bid_price + 0.01);  // More aggressive ask
            order_size *= 1.5;  // Larger size on ask
            
            // Create and submit ask order
            std::string order_id = id_ + "_ask_" + instrument_id;
            auto ask_order = std::make_shared<Order>(
                order_id,
                instrument_id,
                OrderSide::SELL,
                OrderType::LIMIT,
                order_size,
                ask_price,
                TimeInForce::DAY
            );
            
            submit_order(exchange, ask_order);
            active_quotes_[instrument_id].second = order_id;
        } else if (position < 0) {
            // Short position - skew to buy more / sell less
            bid_price = std::min(bid_price * 1.01, ask_price - 0.01);  // More aggressive bid
            order_size *= 1.5;  // Larger size on bid
            
            // Create and submit bid order
            std::string order_id = id_ + "_bid_" + instrument_id;
            auto bid_order = std::make_shared<Order>(
                order_id,
                instrument_id,
                OrderSide::BUY,
                OrderType::LIMIT,
                order_size,
                bid_price,
                TimeInForce::DAY
            );
            
            submit_order(exchange, bid_order);
            active_quotes_[instrument_id].first = order_id;
        } else {
            // Balanced position - submit both sides
            std::string bid_id = id_ + "_bid_" + instrument_id;
            auto bid_order = std::make_shared<Order>(
                bid_id,
                instrument_id,
                OrderSide::BUY,
                OrderType::LIMIT,
                order_size,
                bid_price,
                TimeInForce::DAY
            );
            
            std::string ask_id = id_ + "_ask_" + instrument_id;
            auto ask_order = std::make_shared<Order>(
                ask_id,
                instrument_id,
                OrderSide::SELL,
                OrderType::LIMIT,
                order_size,
                ask_price,
                TimeInForce::DAY
            );
            
            submit_order(exchange, bid_order);
            submit_order(exchange, ask_order);
            
            active_quotes_[instrument_id] = {bid_id, ask_id};
        }
    } else {
        // Balanced quoting - submit both sides
        std::string bid_id = id_ + "_bid_" + instrument_id;
        auto bid_order = std::make_shared<Order>(
            bid_id,
            instrument_id,
            OrderSide::BUY,
            OrderType::LIMIT,
            order_size,
            bid_price,
            TimeInForce::DAY
        );
        
        std::string ask_id = id_ + "_ask_" + instrument_id;
        auto ask_order = std::make_shared<Order>(
            ask_id,
            instrument_id,
            OrderSide::SELL,
            OrderType::LIMIT,
            order_size,
            ask_price,
            TimeInForce::DAY
        );
        
        submit_order(exchange, bid_order);
        submit_order(exchange, ask_order);
        
        active_quotes_[instrument_id] = {bid_id, ask_id};
    }
}

double MarketMaker::calculate_bid_price(const MarketObservation& obs) {
    // Default implementation - quote inside the spread
    double mid_price = (obs.best_bid.value() + obs.best_ask.value()) / 2.0;
    double half_spread = target_spread_ / 2.0;
    
    return mid_price - half_spread;
}

double MarketMaker::calculate_ask_price(const MarketObservation& obs) {
    // Default implementation - quote inside the spread
    double mid_price = (obs.best_bid.value() + obs.best_ask.value()) / 2.0;
    double half_spread = target_spread_ / 2.0;
    
    return mid_price + half_spread;
}

double MarketMaker::calculate_order_size(const std::string& instrument_id) {
    // Simple implementation - use a percentage of available capital
    return capital_ * order_size_pct_;
}

void MarketMaker::manage_inventory(SimulationEngine& engine, const std::string& instrument_id) {
    // Simplified inventory management - if position exceeds threshold, aggressively rebalance
    auto pos_it = positions_.find(instrument_id);
    if (pos_it == positions_.end()) {
        return;  // No position to manage
    }
    
    double position = pos_it->second.quantity;
    
    // Check if position exceeds limits
    if (std::abs(position) <= max_position_) {
        return;  // Within limits
    }
    
    // Find exchange for this instrument (simplified - using first exchange)
    auto exchange = std::shared_ptr<Exchange>(nullptr);  // Placeholder
    
    // Get current market price
    auto obs_it = latest_observations_.find(instrument_id);
    if (obs_it == latest_observations_.end()) {
        return;  // No observation available
    }
    
    const auto& obs = obs_it->second;
    
    // Check if we have a valid market
    if (!obs.best_bid || !obs.best_ask) {
        return;  // Market is too thin
    }
    
    // Calculate rebalance size
    double excess = std::abs(position) - max_position_;
    double rebalance_size = std::min(excess, std::abs(position) * 0.2);  // Rebalance up to 20% of position
    
    if (position > 0) {
        // Long position - sell to reduce
        std::string order_id = id_ + "_rebalance_sell_" + instrument_id;
        auto sell_order = std::make_shared<Order>(
            order_id,
            instrument_id,
            OrderSide::SELL,
            OrderType::MARKET,  // Use market order for immediate execution
            rebalance_size,
            0.0,  // Price not relevant for market orders
            TimeInForce::IOC
        );
        
        submit_order(exchange, sell_order);
    } else {
        // Short position - buy to reduce
        std::string order_id = id_ + "_rebalance_buy_" + instrument_id;
        auto buy_order = std::make_shared<Order>(
            order_id,
            instrument_id,
            OrderSide::BUY,
            OrderType::MARKET,  // Use market order for immediate execution
            rebalance_size,
            0.0,  // Price not relevant for market orders
            TimeInForce::IOC
        );
        
        submit_order(exchange, buy_order);
    }
}

bool MarketMaker::should_skew_quotes(const std::string& instrument_id) {
    // Determine if we should skew quotes based on current position
    auto pos_it = positions_.find(instrument_id);
    if (pos_it == positions_.end()) {
        return false;  // No position, no need to skew
    }
    
    double position = pos_it->second.quantity;
    double position_ratio = std::abs(position) / max_position_;
    
    // Skew if position is more than 50% of max
    return position_ratio > 0.5;
}

// TrendFollower implementation
TrendFollower::TrendFollower(std::string id, double initial_capital,
                            int short_window, int long_window,
                            double position_size_pct)
    : Actor(std::move(id), initial_capital),
      short_window_(short_window),
      long_window_(long_window),
      position_size_pct_(position_size_pct) {
}

void TrendFollower::initialize(SimulationEngine& engine) {
    Actor::initialize(engine);
    
    // Schedule first decision
    schedule_next_decision(engine, Timestamp(0));
}

void TrendFollower::on_market_observation(const MarketObservation& observation) {
    Actor::on_market_observation(observation);
    
    // Record mid price for trend calculation
    if (observation.best_bid && observation.best_ask) {
        double mid_price = (observation.best_bid.value() + observation.best_ask.value()) / 2.0;
        price_history_[observation.instrument_id].push_back(mid_price);
        
        // Keep history within limits
        if (price_history_[observation.instrument_id].size() > static_cast<size_t>(long_window_ * 2)) {
            price_history_[observation.instrument_id].erase(
                price_history_[observation.instrument_id].begin(),
                price_history_[observation.instrument_id].begin() + 
                (price_history_[observation.instrument_id].size() - long_window_ * 2)
            );
        }
    }
}

void TrendFollower::make_trading_decisions(SimulationEngine& engine) {
    // Calculate trend signals for each instrument
    for (auto& [instrument_id, prices] : price_history_) {
        // Only calculate if we have enough data
        if (prices.size() > static_cast<size_t>(long_window_)) {
            trend_signals_[instrument_id] = calculate_trend_signal(instrument_id);
        }
    }
    
    // Make trading decisions based on signals
    for (const auto& [instrument_id, signal] : trend_signals_) {
        // Find exchange for this instrument (simplified - using first exchange)
        auto exchange = std::shared_ptr<Exchange>(nullptr);  // Placeholder
        
        // Get current position
        auto pos_it = positions_.find(instrument_id);
        double current_position = (pos_it != positions_.end()) ? pos_it->second.quantity : 0.0;
        
        // Calculate target position based on signal
        double signal_strength = std::abs(signal);
        double target_size = calculate_position_size(instrument_id, signal_strength);
        
        if (signal > 0.2) {  // Strong positive trend
            if (current_position <= 0) {
                // Close short position and go long
                if (current_position < 0) {
                    // Close short position
                    std::string order_id = id_ + "_close_short_" + instrument_id;
                    auto buy_order = std::make_shared<Order>(
                        order_id,
                        instrument_id,
                        OrderSide::BUY,
                        OrderType::MARKET,
                        std::abs(current_position),
                        0.0,
                        TimeInForce::IOC
                    );
                    submit_order(exchange, buy_order);
                }
                
                // Go long
                std::string order_id = id_ + "_go_long_" + instrument_id;
                auto buy_order = std::make_shared<Order>(
                    order_id,
                    instrument_id,
                    OrderSide::BUY,
                    OrderType::MARKET,
                    target_size,
                    0.0,
                    TimeInForce::IOC
                );
                submit_order(exchange, buy_order);
            } else if (current_position < target_size) {
                // Increase long position
                std::string order_id = id_ + "_increase_long_" + instrument_id;
                auto buy_order = std::make_shared<Order>(
                    order_id,
                    instrument_id,
                    OrderSide::BUY,
                    OrderType::MARKET,
                    target_size - current_position,
                    0.0,
                    TimeInForce::IOC
                );
                submit_order(exchange, buy_order);
            }
        } else if (signal < -0.2) {  // Strong negative trend
            if (current_position >= 0) {
                // Close long position and go short
                if (current_position > 0) {
                    // Close long position
                    std::string order_id = id_ + "_close_long_" + instrument_id;
                    auto sell_order = std::make_shared<Order>(
                        order_id,
                        instrument_id,
                        OrderSide::SELL,
                        OrderType::MARKET,
                        current_position,
                        0.0,
                        TimeInForce::IOC
                    );
                    submit_order(exchange, sell_order);
                }
                
                // Go short
                std::string order_id = id_ + "_go_short_" + instrument_id;
                auto sell_order = std::make_shared<Order>(
                    order_id,
                    instrument_id,
                    OrderSide::SELL,
                    OrderType::MARKET,
                    target_size,
                    0.0,
                    TimeInForce::IOC
                );
                submit_order(exchange, sell_order);
            } else if (std::abs(current_position) < target_size) {
                // Increase short position
                std::string order_id = id_ + "_increase_short_" + instrument_id;
                auto sell_order = std::make_shared<Order>(
                    order_id,
                    instrument_id,
                    OrderSide::SELL,
                    OrderType::MARKET,
                    target_size - std::abs(current_position),
                    0.0,
                    TimeInForce::IOC
                );
                submit_order(exchange, sell_order);
            }
        } else {  // Weak or no trend
            if (std::abs(current_position) > 0) {
                // Reduce position size
                double reduction_size = std::abs(current_position) * 0.5;  // Reduce by 50%
                
                if (current_position > 0) {
                    // Reduce long position
                    std::string order_id = id_ + "_reduce_long_" + instrument_id;
                    auto sell_order = std::make_shared<Order>(
                        order_id,
                        instrument_id,
                        OrderSide::SELL,
                        OrderType::MARKET,
                        reduction_size,
                        0.0,
                        TimeInForce::IOC
                    );
                    submit_order(exchange, sell_order);
                } else {
                    // Reduce short position
                    std::string order_id = id_ + "_reduce_short_" + instrument_id;
                    auto buy_order = std::make_shared<Order>(
                        order_id,
                        instrument_id,
                        OrderSide::BUY,
                        OrderType::MARKET,
                        reduction_size,
                        0.0,
                        TimeInForce::IOC
                    );
                    submit_order(exchange, buy_order);
                }
            }
        }
    }
    
    // Schedule next decision
    std::uniform_real_distribution<double> dist(0.9, 1.1);  // Random jitter
    Timestamp next_decision_delay = Timestamp(static_cast<int64_t>(dist(rng_) * 5000000000));  // ~5 seconds with jitter
    schedule_next_decision(engine, next_decision_delay);
}

double TrendFollower::calculate_trend_signal(const std::string& instrument_id) {
    auto prices_it = price_history_.find(instrument_id);
    if (prices_it == price_history_.end() || 
        prices_it->second.size() < static_cast<size_t>(long_window_)) {
        return 0.0;  // No signal without sufficient data
    }
    
    const auto& prices = prices_it->second;
    
    // Calculate short and long moving averages
    double short_ma = calculate_simple_moving_average(
        prices, 
        short_window_
    );
    
    double long_ma = calculate_simple_moving_average(
        prices, 
        long_window_
    );
    
    // Calculate signal [-1, 1] based on MA crossover
    double price_scale = (prices.back() != 0) ? prices.back() : 1.0;
    double signal = (short_ma - long_ma) / price_scale;
    
    // Normalize signal to range [-1, 1]
    signal = std::min(1.0, std::max(-1.0, signal * 20.0));
    
    return signal;
}

double TrendFollower::calculate_simple_moving_average(const std::vector<double>& prices, int window) {
    if (prices.empty() || static_cast<size_t>(window) > prices.size()) {
        return 0.0;
    }
    
    double sum = std::accumulate(
        prices.end() - window,
        prices.end(),
        0.0
    );
    
    return sum / window;
}

double TrendFollower::calculate_position_size(const std::string& instrument_id, double signal_strength) {
    // Calculate position size based on capital and signal strength
    return capital_ * position_size_pct_ * signal_strength;
}

// Other actor implementations would follow similar patterns
// (Implementation for HighFrequencyTrader and RetailTrader omitted for brevity)

} // namespace sim