// order_book.hpp
#pragma once

#include <string>
#include <map>
#include <unordered_map>
#include <list>
#include <memory>
#include <vector>
#include <functional>
#include <optional>
#include "engine.hpp"

namespace sim {

/**
 * @brief Order side enumeration
 */
enum class OrderSide {
    BUY,
    SELL
};

/**
 * @brief Order type enumeration
 */
enum class OrderType {
    MARKET,
    LIMIT,
    STOP,
    STOP_LIMIT,
    ICEBERG
};

/**
 * @brief Time-in-force for orders
 */
enum class TimeInForce {
    DAY,             // Valid for the trading day
    GTC,             // Good till canceled
    IOC,             // Immediate or cancel
    FOK,             // Fill or kill
    GTD              // Good till date
};

/**
 * @brief Order status enumeration
 */
enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED,
    EXPIRED
};

/**
 * @brief Base class for all order types
 */
class Order {
public:
    Order(
        std::string order_id,
        std::string instrument_id,
        OrderSide side,
        OrderType type,
        double quantity,
        double price,
        TimeInForce tif,
        Timestamp expiry = Timestamp::max(),
        std::string actor_id = ""
    );
    
    virtual ~Order() = default;
    
    // Basic order properties
    const std::string& order_id() const { return order_id_; }
    const std::string& instrument_id() const { return instrument_id_; }
    OrderSide side() const { return side_; }
    OrderType type() const { return type_; }
    double quantity() const { return quantity_; }
    double remaining_quantity() const { return remaining_quantity_; }
    double executed_quantity() const { return quantity_ - remaining_quantity_; }
    double price() const { return price_; }
    TimeInForce time_in_force() const { return time_in_force_; }
    Timestamp expiry() const { return expiry_; }
    const std::string& actor_id() const { return actor_id_; }
    
    // Order status
    OrderStatus status() const { return status_; }
    void set_status(OrderStatus status) { status_ = status; }
    
    // Order execution
    virtual void execute(double exec_quantity, double exec_price);
    
    // Check if the order can be matched at the given price
    virtual bool can_match_at(double match_price) const;
    
    // Clone the order (for creating copies)
    virtual std::shared_ptr<Order> clone() const;
    
protected:
    std::string order_id_;
    std::string instrument_id_;
    OrderSide side_;
    OrderType type_;
    double quantity_;
    double remaining_quantity_;
    double price_;
    TimeInForce time_in_force_;
    Timestamp expiry_;
    OrderStatus status_;
    std::string actor_id_;
    Timestamp entry_time_;
};

/**
 * @brief Trade represents a match between two orders
 */
struct Trade {
    std::string trade_id;
    std::string buy_order_id;
    std::string sell_order_id;
    std::string instrument_id;
    double price;
    double quantity;
    Timestamp timestamp;
    std::string buyer_id;
    std::string seller_id;
};

/**
 * @brief Price level in the order book
 */
class PriceLevel {
public:
    PriceLevel(double price);
    
    // Add an order to this price level
    void add_order(std::shared_ptr<Order> order);
    
    // Remove an order from this price level
    bool remove_order(const std::string& order_id);
    
    // Get total volume at this price level
    double total_volume() const;
    
    // Get the price of this level
    double price() const { return price_; }
    
    // Get all orders at this level
    const std::list<std::shared_ptr<Order>>& orders() const { return orders_; }
    
    // Check if this level is empty
    bool is_empty() const { return orders_.empty(); }
    
private:
    double price_;
    std::list<std::shared_ptr<Order>> orders_;
    std::unordered_map<std::string, std::list<std::shared_ptr<Order>>::iterator> order_map_;
    double total_volume_;
};

/**
 * @brief Callback for order book events
 */
using OrderBookCallback = std::function<void(const class OrderBook&, const std::shared_ptr<Order>&, const std::optional<Trade>&)>;

/**
 * @brief Order book implementation with price-time priority
 */
class OrderBook {
public:
    OrderBook(std::string instrument_id);
    
    // Add an order to the book
    void add_order(std::shared_ptr<Order> order);
    
    // Remove an order from the book
    bool remove_order(const std::string& order_id);
    
    // Modify an existing order (cancel and replace)
    bool modify_order(const std::string& order_id, double new_quantity, double new_price);
    
    // Process a market order and generate trades
    std::vector<Trade> process_market_order(OrderSide side, double quantity, const std::string& actor_id);
    
    // Check for matching between resting orders
    std::vector<Trade> match_orders();
    
    // Register a callback for order book events
    void register_callback(OrderBookCallback callback);
    
    // Get current best bid/ask
    std::optional<double> best_bid() const;
    std::optional<double> best_ask() const;
    
    // Get market depth (price levels and volumes)
    std::vector<std::pair<double, double>> get_bids(size_t depth = 10) const;
    std::vector<std::pair<double, double>> get_asks(size_t depth = 10) const;
    
    // Get all orders in the book
    std::vector<std::shared_ptr<Order>> get_all_orders() const;
    
    // Get specific order by ID
    std::shared_ptr<Order> get_order(const std::string& order_id) const;
    
    // Get instrument ID
    const std::string& instrument_id() const { return instrument_id_; }
    
    // Calculate mid price
    std::optional<double> mid_price() const;
    
    // Calculate spread
    std::optional<double> spread() const;
    
    // Clear all orders
    void clear();
    
private:
    std::string instrument_id_;
    
    // Price levels for bids (buy orders) - sorted high to low
    std::map<double, PriceLevel, std::greater<double>> bid_levels_;
    
    // Price levels for asks (sell orders) - sorted low to high
    std::map<double, PriceLevel, std::less<double>> ask_levels_;
    
    // Quick lookup for orders by ID
    std::unordered_map<std::string, std::shared_ptr<Order>> orders_;
    
    // Callbacks for order book events
    std::vector<OrderBookCallback> callbacks_;
    
    // Notify all callbacks
    void notify_callbacks(const std::shared_ptr<Order>& order, const std::optional<Trade>& trade = std::nullopt);
    
    // Create a trade record
    Trade create_trade(const std::shared_ptr<Order>& buy_order, 
                      const std::shared_ptr<Order>& sell_order,
                      double quantity, double price);
};

/**
 * @brief Matching engine processes orders and updates the order book
 */
class MatchingEngine {
public:
    MatchingEngine(std::shared_ptr<OrderBook> order_book);
    
    // Process an incoming order
    std::vector<Trade> process_order(std::shared_ptr<Order> order);
    
    // Cancel an order
    bool cancel_order(const std::string& order_id);
    
    // Register a callback for trades
    void register_trade_callback(std::function<void(const Trade&)> callback);
    
private:
    std::shared_ptr<OrderBook> order_book_;
    std::vector<std::function<void(const Trade&)>> trade_callbacks_;
    
    // Notify all trade callbacks
    void notify_trade_callbacks(const Trade& trade);
};

} // namespace sim