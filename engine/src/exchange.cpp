// exchange.cpp
#include "exchange.hpp"
#include "actor.hpp"
#include "engine.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <chrono>

namespace sim {

// LatencyModel implementation
LatencyModel::LatencyModel(Timestamp base_latency, Timestamp jitter, double congestion_factor)
    : base_latency_(base_latency),
      jitter_(jitter),
      congestion_factor_(congestion_factor),
      current_congestion_(1.0) {
    // Initialize random number generator
    std::random_device rd;
    rng_.seed(rd());
}

Timestamp LatencyModel::calculate_order_latency(const std::string& actor_id, const std::string& exchange_id) {
    // Calculate base latency with jitter
    std::uniform_real_distribution<double> jitter_dist(-1.0, 1.0);
    double jitter_factor = 1.0 + jitter_dist(rng_) * (jitter_.count() / static_cast<double>(base_latency_.count()));
    
    // Apply congestion factor
    double latency = base_latency_.count() * jitter_factor * current_congestion_;
    
    return Timestamp(static_cast<int64_t>(latency));
}

Timestamp LatencyModel::calculate_market_data_latency(const std::string& exchange_id, const std::string& actor_id) {
    // Market data typically has lower latency than orders
    return calculate_order_latency(actor_id, exchange_id) / 2;
}

void LatencyModel::update_congestion(double congestion_level) {
    current_congestion_ = std::max(1.0, congestion_level);
}

// FeeModel implementation
FeeModel::FeeModel(double maker_rate, double taker_rate, double min_fee)
    : maker_rate_(maker_rate),
      taker_rate_(taker_rate),
      min_fee_(min_fee) {
}

double FeeModel::calculate_fee(const Trade& trade, bool is_maker) {
    double base_fee = trade.price * trade.quantity * (is_maker ? maker_rate_ : taker_rate_);
    return std::max(base_fee, min_fee_);
}

// OrderSubmissionEvent implementation
OrderSubmissionEvent::OrderSubmissionEvent(Timestamp timestamp, 
                                          std::shared_ptr<Order> order,
                                          std::string actor_id,
                                          std::string exchange_id)
    : Event(timestamp),
      order_(std::move(order)),
      actor_id_(std::move(actor_id)),
      exchange_id_(std::move(exchange_id)) {
}

void OrderSubmissionEvent::process(SimulationEngine& engine) {
    // Find the exchange
    for (const auto& exchange : engine.exchanges_) {
        if (exchange->id() == exchange_id_) {
            exchange->process_order_submission(order_, actor_id_);
            return;
        }
    }
    
    // Exchange not found
    std::cerr << "Error: Exchange not found: " << exchange_id_ << std::endl;
}

// OrderCancellationEvent implementation
OrderCancellationEvent::OrderCancellationEvent(Timestamp timestamp,
                                              std::string order_id,
                                              std::string actor_id,
                                              std::string exchange_id)
    : Event(timestamp),
      order_id_(std::move(order_id)),
      actor_id_(std::move(actor_id)),
      exchange_id_(std::move(exchange_id)) {
}

void OrderCancellationEvent::process(SimulationEngine& engine) {
    // Find the exchange
    for (const auto& exchange : engine.exchanges_) {
        if (exchange->id() == exchange_id_) {
            exchange->process_order_cancellation(order_id_, actor_id_);
            return;
        }
    }
    
    // Exchange not found
    std::cerr << "Error: Exchange not found: " << exchange_id_ << std::endl;
}

// MarketDataEvent implementation
MarketDataEvent::MarketDataEvent(Timestamp timestamp,
                               MarketDataUpdate update,
                               std::string exchange_id,
                               std::string target_actor_id)
    : Event(timestamp),
      update_(std::move(update)),
      exchange_id_(std::move(exchange_id)),
      target_actor_id_(std::move(target_actor_id)) {
}

void MarketDataEvent::process(SimulationEngine& engine) {
    // Convert MarketDataUpdate to MarketObservation
    MarketObservation observation;
    observation.instrument_id = update_.instrument_id;
    observation.timestamp = update_.timestamp;
    observation.best_bid = update_.best_bid;
    observation.best_ask = update_.best_ask;
    observation.bid_levels = update_.bid_levels;
    observation.ask_levels = update_.ask_levels;
    
    if (update_.last_trade.has_value()) {
        observation.last_trade_price = update_.last_trade->price;
        observation.last_trade_volume = update_.last_trade->quantity;
    } else {
        observation.last_trade_price = std::nullopt;
        observation.last_trade_volume = std::nullopt;
    }
    
    // Generate some reasonable values for daily stats
    if (update_.best_bid && update_.best_ask) {
        double mid_price = (*update_.best_bid + *update_.best_ask) / 2.0;
        observation.daily_open = mid_price * 0.99; // Slightly lower than current price
        observation.daily_high = mid_price * 1.02; // Slightly higher
        observation.daily_low = mid_price * 0.98;  // Slightly lower
        observation.daily_volume = 10000.0;        // Placeholder volume
    } else {
        observation.daily_open = 0.0;
        observation.daily_high = 0.0;
        observation.daily_low = 0.0;
        observation.daily_volume = 0.0;
    }
    
    // Find the actor and notify
    if (target_actor_id_.empty()) {
        // Broadcast to all actors
        for (const auto& actor : engine.actors_) {
            actor->on_market_observation(observation);
        }
    } else {
        // Send to specific actor
        for (const auto& actor : engine.actors_) {
            if (actor->id() == target_actor_id_) {
                actor->on_market_observation(observation);
                break;
            }
        }
    }
}

// Exchange implementation
Exchange::Exchange(std::string id, SimulationEngine& engine)
    : id_(std::move(id)),
      engine_(engine),
      is_trading_open_(false),
      latency_model_(nullptr),
      fee_model_(nullptr) {
}

void Exchange::add_instrument(const std::string& instrument_id, double initial_price) {
    // Create order book for the instrument
    auto order_book = std::make_shared<OrderBook>(instrument_id);
    order_books_[instrument_id] = order_book;
    
    // Set up callback to get notified of order book changes
    order_book->register_callback(
        [this](const OrderBook& book, const std::shared_ptr<Order>& order, const std::optional<Trade>& trade) {
            this->on_order_book_update(book, order, trade);
        }
    );
    
    // Create matching engine for the instrument
    auto matching_engine = std::make_shared<MatchingEngine>(order_book);
    matching_engines_[instrument_id] = matching_engine;
    
    // Register callback for trade notifications
    matching_engine->register_trade_callback(
        [this](const Trade& trade) {
            process_trade(trade);
        }
    );
    
    // Initialize order book with some liquidity if an initial price is provided
    if (initial_price > 0) {
        // Add some initial bids and asks around the initial price
        double bid_price = initial_price * 0.99;
        double ask_price = initial_price * 1.01;
        
        // Add a few levels of liquidity
        for (int i = 0; i < 5; ++i) {
            double bid_size = 100.0 * (5 - i);
            double ask_size = 100.0 * (5 - i);
            
            // Create and add orders from a virtual liquidity provider
            auto bid_order = std::make_shared<Order>(
                "init_bid_" + instrument_id + "_" + std::to_string(i),
                instrument_id,
                OrderSide::BUY,
                OrderType::LIMIT,
                bid_size,
                bid_price - i * 0.01 * initial_price,
                TimeInForce::GTC,
                Timestamp::max(),
                "liquidity_provider"
            );
            
            auto ask_order = std::make_shared<Order>(
                "init_ask_" + instrument_id + "_" + std::to_string(i),
                instrument_id,
                OrderSide::SELL,
                OrderType::LIMIT,
                ask_size,
                ask_price + i * 0.01 * initial_price,
                TimeInForce::GTC,
                Timestamp::max(),
                "liquidity_provider"
            );
            
            order_book->add_order(bid_order);
            order_book->add_order(ask_order);
        }
        
        // Record last trade at initial price
        Trade initial_trade;
        initial_trade.trade_id = "init_trade_" + instrument_id;
        initial_trade.buy_order_id = "init_trade_buy";
        initial_trade.sell_order_id = "init_trade_sell";
        initial_trade.instrument_id = instrument_id;
        initial_trade.price = initial_price;
        initial_trade.quantity = 100.0;
        initial_trade.timestamp = engine_.now();
        initial_trade.buyer_id = "liquidity_provider";
        initial_trade.seller_id = "liquidity_provider";
        
        last_trades_[instrument_id] = initial_trade;
    }
    
    // Broadcast initial market data
    broadcast_market_data(instrument_id);
}

void Exchange::process_order_submission(std::shared_ptr<Order> order, const std::string& actor_id) {
    // Check if trading is open
    if (!is_trading_open_) {
        // Reject the order and notify the actor
        order->set_status(OrderStatus::REJECTED);
        
        // Notify the actor
        for (const auto& actor : engine_.actors_) {
            if (actor->id() == actor_id) {
                // Create a dummy trade with zero quantity for notification
                Trade dummy_trade;
                dummy_trade.trade_id = "rejected_" + order->order_id();
                dummy_trade.buy_order_id = (order->side() == OrderSide::BUY) ? order->order_id() : "";
                dummy_trade.sell_order_id = (order->side() == OrderSide::SELL) ? order->order_id() : "";
                dummy_trade.instrument_id = order->instrument_id();
                dummy_trade.price = order->price();
                dummy_trade.quantity = 0.0;  // Zero quantity for rejected orders
                dummy_trade.timestamp = engine_.now();
                dummy_trade.buyer_id = (order->side() == OrderSide::BUY) ? actor_id : "";
                dummy_trade.seller_id = (order->side() == OrderSide::SELL) ? actor_id : "";
                
                actor->on_order_executed(order, dummy_trade);
                break;
            }
        }
        
        return;
    }
    
    // Get the matching engine for the instrument
    auto instrument_id = order->instrument_id();
    auto it = matching_engines_.find(instrument_id);
    if (it == matching_engines_.end()) {
        // Unknown instrument - reject the order
        order->set_status(OrderStatus::REJECTED);
        
        // Notify the actor
        for (const auto& actor : engine_.actors_) {
            if (actor->id() == actor_id) {
                // Create a dummy trade with zero quantity for notification
                Trade dummy_trade;
                dummy_trade.trade_id = "rejected_" + order->order_id();
                dummy_trade.buy_order_id = (order->side() == OrderSide::BUY) ? order->order_id() : "";
                dummy_trade.sell_order_id = (order->side() == OrderSide::SELL) ? order->order_id() : "";
                dummy_trade.instrument_id = order->instrument_id();
                dummy_trade.price = order->price();
                dummy_trade.quantity = 0.0;  // Zero quantity for rejected orders
                dummy_trade.timestamp = engine_.now();
                dummy_trade.buyer_id = (order->side() == OrderSide::BUY) ? actor_id : "";
                dummy_trade.seller_id = (order->side() == OrderSide::SELL) ? actor_id : "";
                
                actor->on_order_executed(order, dummy_trade);
                break;
            }
        }
        
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
            
            // Find the actor to notify them of the cancellation
            for (const auto& actor : engine_.actors_) {
                if (actor->id() == actor_id) {
                    // Get the canceled order (if we had a reference to it)
                    // For now, we're just notifying without the actual order reference
                    
                    // Create a dummy order and trade for notification
                    auto dummy_order = std::make_shared<Order>(
                        order_id,
                        instrument_id,
                        OrderSide::BUY,  // Doesn't matter for cancellation
                        OrderType::LIMIT,
                        0.0,
                        0.0,
                        TimeInForce::GTC
                    );
                    
                    dummy_order->set_status(OrderStatus::CANCELED);
                    
                    Trade dummy_trade;
                    dummy_trade.trade_id = "canceled_" + order_id;
                    dummy_trade.buy_order_id = order_id;
                    dummy_trade.sell_order_id = "";
                    dummy_trade.instrument_id = instrument_id;
                    dummy_trade.price = 0.0;
                    dummy_trade.quantity = 0.0;
                    dummy_trade.timestamp = engine_.now();
                    dummy_trade.buyer_id = actor_id;
                    dummy_trade.seller_id = "";
                    
                    actor->on_order_executed(dummy_order, dummy_trade);
                    break;
                }
            }
            
            return;
        }
    }
    
    // Order not found - no action needed
}

void Exchange::subscribe_to_market_data(const std::string& actor_id, const std::string& instrument_id) {
    market_data_subscriptions_[actor_id].insert(instrument_id);
    
    // Send initial market data snapshot
    auto it = order_books_.find(instrument_id);
    if (it != order_books_.end()) {
        MarketDataUpdate update;
        update.instrument_id = instrument_id;
        update.timestamp = engine_.now();
        update.best_bid = it->second->best_bid();
        update.best_ask = it->second->best_ask();
        update.bid_levels = it->second->get_bids(10);
        update.ask_levels = it->second->get_asks(10);
        
        auto trade_it = last_trades_.find(instrument_id);
        if (trade_it != last_trades_.end()) {
            update.last_trade = trade_it->second;
        }
        
        // Calculate latency
        Timestamp latency(0);
        if (latency_model_) {
            latency = latency_model_->calculate_market_data_latency(id_, actor_id);
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

void Exchange::unsubscribe_from_market_data(const std::string& actor_id, const std::string& instrument_id) {
    auto it = market_data_subscriptions_.find(actor_id);
    if (it != market_data_subscriptions_.end()) {
        it->second.erase(instrument_id);
        
        // Clean up if no more subscriptions
        if (it->second.empty()) {
            market_data_subscriptions_.erase(it);
        }
    }
}

std::shared_ptr<OrderBook> Exchange::get_order_book(const std::string& instrument_id) {
    auto it = order_books_.find(instrument_id);
    if (it == order_books_.end()) {
        return nullptr;
    }
    return it->second;
}

MarketDataUpdate Exchange::get_market_data(const std::string& instrument_id) {
    MarketDataUpdate update;
    update.instrument_id = instrument_id;
    update.timestamp = engine_.now();
    
    auto it = order_books_.find(instrument_id);
    if (it != order_books_.end()) {
        update.best_bid = it->second->best_bid();
        update.best_ask = it->second->best_ask();
        update.bid_levels = it->second->get_bids(10);
        update.ask_levels = it->second->get_asks(10);
    }
    
    auto trade_it = last_trades_.find(instrument_id);
    if (trade_it != last_trades_.end()) {
        update.last_trade = trade_it->second;
    }
    
    return update;
}

void Exchange::set_latency_model(std::shared_ptr<LatencyModel> model) {
    latency_model_ = model;
}

void Exchange::set_fee_model(std::shared_ptr<FeeModel> model) {
    fee_model_ = model;
}

void Exchange::open_trading_session() {
    is_trading_open_ = true;
    
    // Broadcast market open event
    // This would be implemented for a real exchange
}

void Exchange::close_trading_session() {
    is_trading_open_ = false;
    
    // Cancel all open orders
    // In a real exchange, we might want different behavior depending on order types
    for (auto& [instrument_id, order_book] : order_books_) {
        auto orders = order_book->get_all_orders();
        for (auto& order : orders) {
            order_book->remove_order(order->order_id());
        }
        
        // Broadcast final market data
        broadcast_market_data(instrument_id);
    }
}

void Exchange::on_order_book_update(const OrderBook& book, const std::shared_ptr<Order>& order, const std::optional<Trade>& trade) {
    // This callback is triggered when the order book changes
    // Here we can implement logic like updating market data displays,
    // triggering trading halts on extreme price movements, etc.
    
    // For now, we'll just broadcast market data on significant changes
    if (order || trade) {
        broadcast_market_data(book.instrument_id());
    }
}

void Exchange::broadcast_market_data(const std::string& instrument_id) {
    auto it = order_books_.find(instrument_id);
    if (it == order_books_.end()) {
        return;  // No order book for this instrument
    }
    
    // Create market data update
    MarketDataUpdate update;
    update.instrument_id = instrument_id;
    update.timestamp = engine_.now();
    update.best_bid = it->second->best_bid();
    update.best_ask = it->second->best_ask();
    update.bid_levels = it->second->get_bids(10);
    update.ask_levels = it->second->get_asks(10);
    
    auto trade_it = last_trades_.find(instrument_id);
    if (trade_it != last_trades_.end()) {
        update.last_trade = trade_it->second;
    }
    
    // Send to all subscribed actors
    for (const auto& [actor_id, subscriptions] : market_data_subscriptions_) {
        if (subscriptions.find(instrument_id) != subscriptions.end()) {
            // Calculate latency
            Timestamp latency(0);
            if (latency_model_) {
                latency = latency_model_->calculate_market_data_latency(id_, actor_id);
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
    
    // Apply fees if a fee model is set
    apply_fees(trade);
    
    // Notify the involved actors
    for (const auto& actor : engine_.actors_) {
        if (actor->id() == trade.buyer_id || actor->id() == trade.seller_id) {
            // Find the corresponding order for this actor
            std::string order_id;
            if (actor->id() == trade.buyer_id) {
                order_id = trade.buy_order_id;
            } else {
                order_id = trade.sell_order_id;
            }
            
            // Get the order from the order book
            auto order_book = get_order_book(trade.instrument_id);
            if (order_book) {
                auto order = order_book->get_order(order_id);
                if (order) {
                    actor->on_order_executed(order, trade);
                }
            }
        }
    }
}

void Exchange::apply_fees(const Trade& trade) {
    if (!fee_model_) {
        return;  // No fee model set
    }
    
    // Calculate and apply fees for buyer and seller
    // In a real implementation, we would adjust actor capital based on fees
    double buyer_fee = fee_model_->calculate_fee(trade, false);  // Taker
    double seller_fee = fee_model_->calculate_fee(trade, true);  // Maker
    
    // Apply fees to actors (if we had direct access to credit/debit their accounts)
    // For now, we'll just calculate the fees
    
    // Notify actors of fees
    // This would be included in trade notification
}

} // namespace sim