// order_book.cpp
#include "order_book.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace sim {

// Order implementation
Order::Order(
    std::string order_id,
    std::string instrument_id,
    OrderSide side,
    OrderType type,
    double quantity,
    double price,
    TimeInForce tif,
    Timestamp expiry,
    std::string actor_id
) : order_id_(std::move(order_id)),
    instrument_id_(std::move(instrument_id)),
    side_(side),
    type_(type),
    quantity_(quantity),
    remaining_quantity_(quantity),
    price_(price),
    time_in_force_(tif),
    expiry_(expiry),
    status_(OrderStatus::NEW),
    actor_id_(std::move(actor_id)),
    entry_time_(Timestamp::zero()) {
}

void Order::execute(double exec_quantity, double exec_price) {
    if (exec_quantity <= 0 || exec_quantity > remaining_quantity_) {
        throw std::invalid_argument("Invalid execution quantity");
    }
    
    remaining_quantity_ -= exec_quantity;
    
    if (remaining_quantity_ <= 0) {
        status_ = OrderStatus::FILLED;
    } else {
        status_ = OrderStatus::PARTIALLY_FILLED;
    }
}

bool Order::can_match_at(double match_price) const {
    if (status_ != OrderStatus::NEW && status_ != OrderStatus::PARTIALLY_FILLED) {
        return false;
    }
    
    switch (type_) {
        case OrderType::MARKET:
            return true;  // Market orders match at any price
        
        case OrderType::LIMIT:
            // For buy orders, match_price must be <= price
            // For sell orders, match_price must be >= price
            return (side_ == OrderSide::BUY && match_price <= price_) ||
                   (side_ == OrderSide::SELL && match_price >= price_);
        
        default:
            return false;  // Unsupported order types for now
    }
}

std::shared_ptr<Order> Order::clone() const {
    return std::make_shared<Order>(
        order_id_, instrument_id_, side_, type_, quantity_, price_,
        time_in_force_, expiry_, actor_id_
    );
}

// PriceLevel implementation
PriceLevel::PriceLevel(double price) : price_(price), total_volume_(0.0) {
}

void PriceLevel::add_order(std::shared_ptr<Order> order) {
    orders_.push_back(order);
    order_map_[order->order_id()] = --orders_.end();
    total_volume_ += order->remaining_quantity();
}

bool PriceLevel::remove_order(const std::string& order_id) {
    auto it = order_map_.find(order_id);
    if (it == order_map_.end()) {
        return false;
    }
    
    total_volume_ -= (*it->second)->remaining_quantity();
    orders_.erase(it->second);
    order_map_.erase(it);
    return true;
}

double PriceLevel::total_volume() const {
    return total_volume_;
}

// OrderBook implementation
OrderBook::OrderBook(std::string instrument_id) 
    : instrument_id_(std::move(instrument_id)) {
}

void OrderBook::add_order(std::shared_ptr<Order> order) {
    // Store the order in the lookup map
    orders_[order->order_id()] = order;
    
    // Add to appropriate price level
    if (order->side() == OrderSide::BUY) {
        auto& level = bid_levels_[order->price()];
        if (level.price() == 0.0) {  // New level
            level = PriceLevel(order->price());
        }
        level.add_order(order);
    } else {  // SELL
        auto& level = ask_levels_[order->price()];
        if (level.price() == 0.0) {  // New level
            level = PriceLevel(order->price());
        }
        level.add_order(order);
    }
    
    // Notify callbacks
    notify_callbacks(order);
}

bool OrderBook::remove_order(const std::string& order_id) {
    // Find the order
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;  // Order not found
    }
    
    auto order = it->second;
    bool result = false;
    
    // Remove from appropriate price level
    if (order->side() == OrderSide::BUY) {
        auto level_it = bid_levels_.find(order->price());
        if (level_it != bid_levels_.end()) {
            result = level_it->second.remove_order(order_id);
            if (level_it->second.is_empty()) {
                bid_levels_.erase(level_it);
            }
        }
    } else {  // SELL
        auto level_it = ask_levels_.find(order->price());
        if (level_it != ask_levels_.end()) {
            result = level_it->second.remove_order(order_id);
            if (level_it->second.is_empty()) {
                ask_levels_.erase(level_it);
            }
        }
    }
    
    // Remove from order map
    orders_.erase(it);
    
    // Notify callbacks
    notify_callbacks(order);
    
    return result;
}

bool OrderBook::modify_order(const std::string& order_id, double new_quantity, double new_price) {
    // Find the order
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;  // Order not found
    }
    
    auto order = it->second;
    
    // Shortcut for price-only modification
    if (new_quantity == order->remaining_quantity() && new_price == order->price()) {
        return true;  // No changes needed
    }
    
    // For simplicity, implement as cancel and replace
    if (!remove_order(order_id)) {
        return false;
    }
    
    // Create new order with modified parameters
    auto new_order = order->clone();
    // Assuming we have setters for these properties:
    // new_order->set_price(new_price);
    // new_order->set_remaining_quantity(new_quantity);
    
    // Add the modified order
    add_order(new_order);
    
    return true;
}

std::vector<Trade> OrderBook::process_market_order(OrderSide side, double quantity, const std::string& actor_id) {
    std::vector<Trade> trades;
    double remaining = quantity;
    
    if (side == OrderSide::BUY) {
        // Match against ask levels from lowest to highest
        while (remaining > 0 && !ask_levels_.empty()) {
            auto level_it = ask_levels_.begin();
            auto& level = level_it->second;
            
            // Match orders at this level
            auto order_it = level.orders().begin();
            while (order_it != level.orders().end() && remaining > 0) {
                auto& order = *order_it;
                
                // Calculate match quantity
                double match_qty = std::min(remaining, order->remaining_quantity());
                
                // Execute the trade
                order->execute(match_qty, level.price());
                remaining -= match_qty;
                
                // Create trade record
                trades.push_back(create_trade(
                    std::make_shared<Order>(
                        "market_" + std::to_string(trades.size()),
                        instrument_id_,
                        OrderSide::BUY,
                        OrderType::MARKET,
                        match_qty,
                        level.price(),
                        TimeInForce::IOC,
                        Timestamp::max(),
                        actor_id
                    ),
                    order,
                    match_qty,
                    level.price()
                ));
                
                // Move to next order or remove filled order
                if (order->status() == OrderStatus::FILLED) {
                    order_it = level.orders().erase(order_it);
                } else {
                    ++order_it;
                }
            }
            
            // Remove empty level
            if (level.is_empty()) {
                ask_levels_.erase(level_it);
            }
        }
    } else {  // SELL
        // Match against bid levels from highest to lowest
        while (remaining > 0 && !bid_levels_.empty()) {
            auto level_it = bid_levels_.begin();
            auto& level = level_it->second;
            
            // Match orders at this level
            auto order_it = level.orders().begin();
            while (order_it != level.orders().end() && remaining > 0) {
                auto& order = *order_it;
                
                // Calculate match quantity
                double match_qty = std::min(remaining, order->remaining_quantity());
                
                // Execute the trade
                order->execute(match_qty, level.price());
                remaining -= match_qty;
                
                // Create trade record
                trades.push_back(create_trade(
                    order,
                    std::make_shared<Order>(
                        "market_" + std::to_string(trades.size()),
                        instrument_id_,
                        OrderSide::SELL,
                        OrderType::MARKET,
                        match_qty,
                        level.price(),
                        TimeInForce::IOC,
                        Timestamp::max(),
                        actor_id
                    ),
                    match_qty,
                    level.price()
                ));
                
                // Move to next order or remove filled order
                if (order->status() == OrderStatus::FILLED) {
                    order_it = level.orders().erase(order_it);
                } else {
                    ++order_it;
                }
            }
            
            // Remove empty level
            if (level.is_empty()) {
                bid_levels_.erase(level_it);
            }
        }
    }
    
    // Notify callbacks about each trade
    for (const auto& trade : trades) {
        notify_callbacks(nullptr, trade);
    }
    
    return trades;
}

std::vector<Trade> OrderBook::match_orders() {
    std::vector<Trade> trades;
    
    // Continue matching as long as there are overlapping bid/ask levels
    while (!bid_levels_.empty() && !ask_levels_.empty()) {
        auto high_bid = bid_levels_.begin();
        auto low_ask = ask_levels_.begin();
        
        // Check if levels overlap (bid >= ask)
        if (high_bid->first < low_ask->first) {
            break;  // No overlap, we're done
        }
        
        // We have an overlap, match orders
        auto& bid_level = high_bid->second;
        auto& ask_level = low_ask->second;
        
        // Determine trade price (usually the resting order's price)
        double trade_price = ask_level.price();  // For simplicity, use ask price
        
        // Match orders at these levels
        auto bid_it = bid_level.orders().begin();
        auto ask_it = ask_level.orders().begin();
        
        while (bid_it != bid_level.orders().end() && ask_it != ask_level.orders().end()) {
            auto& bid_order = *bid_it;
            auto& ask_order = *ask_it;
            
            // Calculate match quantity
            double match_qty = std::min(bid_order->remaining_quantity(), ask_order->remaining_quantity());
            
            // Execute the trade
            bid_order->execute(match_qty, trade_price);
            ask_order->execute(match_qty, trade_price);
            
            // Create trade record
            trades.push_back(create_trade(bid_order, ask_order, match_qty, trade_price));
            
            // Update iterators
            if (bid_order->status() == OrderStatus::FILLED) {
                bid_it = bid_level.orders().erase(bid_it);
            } else {
                ++bid_it;
            }
            
            if (ask_order->status() == OrderStatus::FILLED) {
                ask_it = ask_level.orders().erase(ask_it);
            } else {
                ++ask_it;
            }
        }
        
        // Remove empty levels
        if (bid_level.is_empty()) {
            bid_levels_.erase(high_bid);
        }
        if (ask_level.is_empty()) {
            ask_levels_.erase(low_ask);
        }
    }
    
    // Notify callbacks about each trade
    for (const auto& trade : trades) {
        notify_callbacks(nullptr, trade);
    }
    
    return trades;
}

void OrderBook::register_callback(OrderBookCallback callback) {
    callbacks_.push_back(callback);
}

std::optional<double> OrderBook::best_bid() const {
    if (bid_levels_.empty()) {
        return std::nullopt;
    }
    return bid_levels_.begin()->first;
}

std::optional<double> OrderBook::best_ask() const {
    if (ask_levels_.empty()) {
        return std::nullopt;
    }
    return ask_levels_.begin()->first;
}

std::vector<std::pair<double, double>> OrderBook::get_bids(size_t depth) const {
    std::vector<std::pair<double, double>> result;
    result.reserve(std::min(depth, bid_levels_.size()));
    
    for (auto it = bid_levels_.begin(); it != bid_levels_.end() && result.size() < depth; ++it) {
        result.emplace_back(it->first, it->second.total_volume());
    }
    
    return result;
}

std::vector<std::pair<double, double>> OrderBook::get_asks(size_t depth) const {
    std::vector<std::pair<double, double>> result;
    result.reserve(std::min(depth, ask_levels_.size()));
    
    for (auto it = ask_levels_.begin(); it != ask_levels_.end() && result.size() < depth; ++it) {
        result.emplace_back(it->first, it->second.total_volume());
    }
    
    return result;
}

std::vector<std::shared_ptr<Order>> OrderBook::get_all_orders() const {
    std::vector<std::shared_ptr<Order>> result;
    result.reserve(orders_.size());
    
    for (const auto& [_, order] : orders_) {
        result.push_back(order);
    }
    
    return result;
}

std::shared_ptr<Order> OrderBook::get_order(const std::string& order_id) const {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return nullptr;
    }
    return it->second;
}

std::optional<double> OrderBook::mid_price() const {
    auto b = best_bid();
    auto a = best_ask();
    
    if (!b || !a) {
        return std::nullopt;
    }
    
    return (*b + *a) / 2.0;
}

std::optional<double> OrderBook::spread() const {
    auto b = best_bid();
    auto a = best_ask();
    
    if (!b || !a) {
        return std::nullopt;
    }
    
    return *a - *b;
}

void OrderBook::clear() {
    bid_levels_.clear();
    ask_levels_.clear();
    orders_.clear();
}

void OrderBook::notify_callbacks(const std::shared_ptr<Order>& order, const std::optional<Trade>& trade) {
    for (const auto& callback : callbacks_) {
        callback(*this, order, trade);
    }
}

Trade OrderBook::create_trade(const std::shared_ptr<Order>& buy_order, 
                             const std::shared_ptr<Order>& sell_order,
                             double quantity, double price) {
    static uint64_t next_trade_id = 0;
    
    Trade trade;
    trade.trade_id = "trade_" + std::to_string(++next_trade_id);
    trade.buy_order_id = buy_order->order_id();
    trade.sell_order_id = sell_order->order_id();
    trade.instrument_id = instrument_id_;
    trade.price = price;
    trade.quantity = quantity;
    trade.timestamp = Timestamp(std::chrono::system_clock::now().time_since_epoch().count());
    trade.buyer_id = buy_order->actor_id();
    trade.seller_id = sell_order->actor_id();
    
    return trade;
}

// MatchingEngine implementation
MatchingEngine::MatchingEngine(std::shared_ptr<OrderBook> order_book)
    : order_book_(order_book) {
    // Register for order book updates
    order_book_->register_callback(
        [this](const OrderBook& book, const std::shared_ptr<Order>& order, const std::optional<Trade>& trade) {
            if (trade) {
                notify_trade_callbacks(*trade);
            }
        }
    );
}

std::vector<Trade> MatchingEngine::process_order(std::shared_ptr<Order> order) {
    std::vector<Trade> trades;
    
    // Handle different order types
    switch (order->type()) {
        case OrderType::MARKET:
            // Market orders are executed immediately
            if (order->side() == OrderSide::BUY) {
                trades = order_book_->process_market_order(OrderSide::BUY, order->quantity(), order->actor_id());
            } else {
                trades = order_book_->process_market_order(OrderSide::SELL, order->quantity(), order->actor_id());
            }
            break;
            
        case OrderType::LIMIT:
            // Add limit order to book
            order_book_->add_order(order);
            
            // Try to match orders
            auto matched_trades = order_book_->match_orders();
            trades.insert(trades.end(), matched_trades.begin(), matched_trades.end());
            break;
            
        default:
            // Unsupported order type
            order->set_status(OrderStatus::REJECTED);
            break;
    }
    
    return trades;
}

bool MatchingEngine::cancel_order(const std::string& order_id) {
    return order_book_->remove_order(order_id);
}

void MatchingEngine::register_trade_callback(std::function<void(const Trade&)> callback) {
    trade_callbacks_.push_back(callback);
}

void MatchingEngine::notify_trade_callbacks(const Trade& trade) {
    for (const auto& callback : trade_callbacks_) {
        callback(trade);
    }
}

} // namespace sim