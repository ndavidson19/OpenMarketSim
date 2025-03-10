// simulation_binding.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include <pybind11/chrono.h>
#include <memory>
#include <iostream>

#include "engine.hpp"
#include "order_book.hpp"
#include "actor.hpp"
#include "exchange.hpp"
#include "options.hpp"

namespace py = pybind11;
namespace sim {

// Custom wrapper class for Python interface
class PySimulationBinding {
public:
    PySimulationBinding(const std::unordered_map<std::string, std::string>& config = {}) {
        // Create and initialize simulation engine
        sim_ = std::make_unique<SimulationEngine>();
        sim_->initialize(config);
    }
    
    void reset(const std::unordered_map<std::string, std::string>& config = {}) {
        // Reset simulation engine
        sim_->reset();
        if (!config.empty()) {
            for (const auto& [key, value] : config) {
                sim_->set_config(key, value);
            }
        }
    }
    
    void execute_action(const py::dict& action) {
        // Convert Python action dict to C++ action
        // Extract action parameters
        std::string type = action["type"].cast<std::string>();
        std::string direction = action.contains("direction") ? action["direction"].cast<std::string>() : "BUY";
        double price_offset = action.contains("price_offset") ? action["price_offset"].cast<double>() : 0.0;
        double size = action.contains("size") ? action["size"].cast<double>() : 0.0;
        std::string instrument_id = action.contains("instrument_id") ? action["instrument_id"].cast<std::string>() : "";
        std::string exchange_id = action.contains("exchange_id") ? action["exchange_id"].cast<std::string>() : "";
        
        // Find actor and exchange
        std::string actor_id = active_actor_id_;
        if (actor_id.empty() || exchanges_.empty() || instrument_id.empty()) {
            throw std::runtime_error("Cannot execute action: missing actor, exchange, or instrument");
        }
        
        // Default to first exchange if not specified
        if (exchange_id.empty()) {
            exchange_id = exchanges_.begin()->first;
        }
        
        // Create and execute appropriate action
        if (type == "MARKET" || type == "LIMIT") {
            OrderSide side = (direction == "BUY") ? OrderSide::BUY : OrderSide::SELL;
            OrderType order_type = (type == "MARKET") ? OrderType::MARKET : OrderType::LIMIT;
            
            // Calculate price for limit orders
            double price = 0.0;
            if (order_type == OrderType::LIMIT) {
                // Get current market price as reference
                auto market_data = exchanges_[exchange_id]->get_market_data(instrument_id);
                if (side == OrderSide::BUY) {
                    price = market_data.best_ask.value_or(100.0) * (1.0 + price_offset);
                } else {
                    price = market_data.best_bid.value_or(100.0) * (1.0 - price_offset);
                }
            }
            
            // Create order
            std::string order_id = generate_order_id();
            auto order = std::make_shared<Order>(
                order_id,
                instrument_id,
                side,
                order_type,
                size,
                price,
                TimeInForce::DAY,
                Timestamp::max(),
                actor_id
            );
            
            // Submit order to exchange
            actors_[actor_id]->submit_order(exchanges_[exchange_id], order);
        }
        else if (type == "CANCEL") {
            std::string order_id = action.contains("order_id") ? action["order_id"].cast<std::string>() : "";
            if (!order_id.empty()) {
                actors_[actor_id]->cancel_order(exchanges_[exchange_id], order_id);
            }
        }
    }
    
    void advance_to_next_observation() {
        // Run simulation until next observation is available for active actor
        if (active_actor_id_.empty()) {
            throw std::runtime_error("No active actor set");
        }
        
        // Schedule next observation for the active actor
        actors_[active_actor_id_]->schedule_next_decision(*sim_, Timestamp(1000000)); // 1ms
        
        // Run simulation until decision point
        sim_->run_until_condition([this](const SimulationEngine& engine) {
            // Check if we've reached a decision point for the active actor
            // This is a simplified condition - in a real implementation, we'd need
            // a mechanism to track when the actor's decision function should be called
            return false; // Placeholder for actual condition
        });
    }
    
    py::dict get_observation() {
        // Create market observation for active actor
        if (active_actor_id_.empty() || exchanges_.empty()) {
            throw std::runtime_error("Cannot get observation: missing actor or exchange");
        }
        
        py::dict observation;
        
        // For each exchange and instrument, collect market data
        py::dict order_books;
        py::dict market_data;
        py::dict position_data;
        
        for (const auto& [exchange_id, exchange] : exchanges_) {
            py::dict exchange_data;
            
            // Collect data for each instrument
            for (const auto& instrument_id : instruments_) {
                auto market_update = exchange->get_market_data(instrument_id);
                
                // Convert order book levels to numpy arrays
                py::array_t<double> bids(market_update.bid_levels.size() * 2);
                py::array_t<double> asks(market_update.ask_levels.size() * 2);
                
                auto bids_ptr = bids.mutable_data();
                auto asks_ptr = asks.mutable_data();
                
                for (size_t i = 0; i < market_update.bid_levels.size(); ++i) {
                    bids_ptr[i*2] = market_update.bid_levels[i].first;     // price
                    bids_ptr[i*2+1] = market_update.bid_levels[i].second;  // volume
                }
                
                for (size_t i = 0; i < market_update.ask_levels.size(); ++i) {
                    asks_ptr[i*2] = market_update.ask_levels[i].first;     // price
                    asks_ptr[i*2+1] = market_update.ask_levels[i].second;  // volume
                }
                
                // Reshape arrays to [levels, 2] format
                bids.resize({static_cast<py::ssize_t>(market_update.bid_levels.size()), 2});
                asks.resize({static_cast<py::ssize_t>(market_update.ask_levels.size()), 2});
                
                // Create dictionary for this instrument
                py::dict ob_data;
                ob_data["bids"] = bids;
                ob_data["asks"] = asks;
                ob_data["best_bid"] = market_update.best_bid.value_or(0.0);
                ob_data["best_ask"] = market_update.best_ask.value_or(0.0);
                ob_data["mid_price"] = (market_update.best_bid.value_or(0.0) + market_update.best_ask.value_or(0.0)) / 2.0;
                
                // Add last trade information if available
                if (market_update.last_trade.has_value()) {
                    ob_data["last_price"] = market_update.last_trade->price;
                    ob_data["last_quantity"] = market_update.last_trade->quantity;
                    ob_data["last_timestamp"] = market_update.last_trade->timestamp.count();
                }
                
                order_books[py::str(instrument_id)] = ob_data;
                
                // Add market data features (could include more metrics)
                py::dict md_data;
                md_data["timestamp"] = market_update.timestamp.count();
                md_data["spread"] = market_update.best_ask.value_or(0.0) - market_update.best_bid.value_or(0.0);
                market_data[py::str(instrument_id)] = md_data;
            }
            
            exchange_data["order_books"] = order_books;
            exchange_data["market_data"] = market_data;
            observation[py::str(exchange_id)] = exchange_data;
        }
        
        // Add position data for the active actor
        auto& actor = actors_[active_actor_id_];
        py::dict positions;
        
        for (const auto& [instrument_id, position] : actor->positions()) {
            py::dict pos_data;
            pos_data["quantity"] = position.quantity;
            pos_data["avg_price"] = position.avg_price;
            pos_data["realized_pnl"] = position.realized_pnl;
            pos_data["unrealized_pnl"] = position.unrealized_pnl;
            positions[py::str(instrument_id)] = pos_data;
        }
        
        // Add overall capital information
        py::dict capital_data;
        capital_data["capital"] = actor->capital();
        
        position_data["positions"] = positions;
        position_data["capital"] = capital_data;
        observation["position"] = position_data;
        
        return observation;
    }
    
    double calculate_reward() {
        // Calculate reward based on PnL, risk, or other metrics
        if (active_actor_id_.empty()) {
            return 0.0;
        }
        
        auto& actor = actors_[active_actor_id_];
        
        // Get current prices for all instruments
        std::unordered_map<std::string, double> current_prices;
        for (const auto& instrument_id : instruments_) {
            // Use mid price from first exchange as current price
            auto& exchange = exchanges_.begin()->second;
            auto market_data = exchange->get_market_data(instrument_id);
            double mid_price = (market_data.best_bid.value_or(0.0) + market_data.best_ask.value_or(0.0)) / 2.0;
            current_prices[instrument_id] = mid_price;
        }
        
        // Calculate portfolio value change as reward
        double current_portfolio_value = actor->portfolio_value(current_prices);
        double portfolio_change = current_portfolio_value - last_portfolio_value_;
        last_portfolio_value_ = current_portfolio_value;
        
        // Can implement more sophisticated reward functions here
        return portfolio_change;
    }
    
    bool is_done() {
        // Check if simulation is complete
        // For example, check if we've reached the end time or if some termination condition is met
        return false;  // Placeholder
    }
    
    py::dict get_info() {
        // Get additional information about the simulation state
        py::dict info;
        
        // Add simulation time
        info["sim_time"] = sim_->now().count();
        
        // Add trading statistics
        py::dict trading_stats;
        // (Populate with relevant statistics)
        
        info["trading_stats"] = trading_stats;
        
        return info;
    }
    
    void set_active_actor(const std::string& actor_id) {
        // Set the active actor for the simulation
        if (actors_.find(actor_id) == actors_.end()) {
            throw std::runtime_error("Actor not found: " + actor_id);
        }
        active_actor_id_ = actor_id;
        
        // Initialize portfolio value tracking
        std::unordered_map<std::string, double> current_prices;
        for (const auto& instrument_id : instruments_) {
            // Use mid price from first exchange as current price
            auto& exchange = exchanges_.begin()->second;
            auto market_data = exchange->get_market_data(instrument_id);
            double mid_price = (market_data.best_bid.value_or(0.0) + market_data.best_ask.value_or(0.0)) / 2.0;
            current_prices[instrument_id] = mid_price;
        }
        
        last_portfolio_value_ = actors_[active_actor_id_]->portfolio_value(current_prices);
    }
    
    void register_actor(const std::string& actor_type, const std::string& actor_id, double initial_capital) {
        // Create and register a new actor
        std::shared_ptr<Actor> actor;
        
        if (actor_type == "market_maker") {
            actor = std::make_shared<MarketMaker>(actor_id, initial_capital, 0.01, 100.0, 0.1);
        }
        else if (actor_type == "trend_follower") {
            actor = std::make_shared<TrendFollower>(actor_id, initial_capital, 10, 30, 0.2);
        }
        else if (actor_type == "hft") {
            actor = std::make_shared<HighFrequencyTrader>(actor_id, initial_capital, 0.0001, Timestamp(100000));  // 100μs
        }
        else if (actor_type == "retail") {
            actor = std::make_shared<RetailTrader>(actor_id, initial_capital, 0.5, 0.3, 2.0);
        }
        else {
            throw std::runtime_error("Unknown actor type: " + actor_type);
        }
        
        sim_->register_actor(actor);
        actors_[actor_id] = actor;
    }
    
    void register_exchange(const std::string& exchange_id) {
        // Create and register a new exchange
        auto exchange = std::make_shared<Exchange>(exchange_id, *sim_);
        sim_->register_exchange(exchange);
        exchanges_[exchange_id] = exchange;
    }
    
    void add_instrument(const std::string& exchange_id, const std::string& instrument_id, double initial_price) {
        // Add an instrument to an exchange
        if (exchanges_.find(exchange_id) == exchanges_.end()) {
            throw std::runtime_error("Exchange not found: " + exchange_id);
        }
        
        exchanges_[exchange_id]->add_instrument(instrument_id, initial_price);
        instruments_.insert(instrument_id);
    }
    
    std::string render_human() {
        // Generate human-readable representation of the simulation state
        std::string output = "Simulation State at t=" + std::to_string(sim_->now().count()) + "\n";
        output += "Active Actor: " + active_actor_id_ + "\n\n";
        
        // Add order book snapshots
        for (const auto& [exchange_id, exchange] : exchanges_) {
            output += "Exchange: " + exchange_id + "\n";
            
            for (const auto& instrument_id : instruments_) {
                auto market_data = exchange->get_market_data(instrument_id);
                
                output += "  Instrument: " + instrument_id + "\n";
                output += "    Best Bid: " + std::to_string(market_data.best_bid.value_or(0.0)) + "\n";
                output += "    Best Ask: " + std::to_string(market_data.best_ask.value_or(0.0)) + "\n";
                output += "    Bid Levels:\n";
                
                for (const auto& [price, volume] : market_data.bid_levels) {
                    output += "      " + std::to_string(price) + ": " + std::to_string(volume) + "\n";
                }
                
                output += "    Ask Levels:\n";
                for (const auto& [price, volume] : market_data.ask_levels) {
                    output += "      " + std::to_string(price) + ": " + std::to_string(volume) + "\n";
                }
                
                output += "\n";
            }
        }
        
        // Add position information
        if (!active_actor_id_.empty()) {
            auto& actor = actors_[active_actor_id_];
            output += "Portfolio:\n";
            output += "  Capital: " + std::to_string(actor->capital()) + "\n";
            output += "  Positions:\n";
            
            for (const auto& [instrument_id, position] : actor->positions()) {
                output += "    " + instrument_id + ": " + 
                      std::to_string(position.quantity) + " @ " + 
                      std::to_string(position.avg_price) + " (PnL: " + 
                      std::to_string(position.realized_pnl + position.unrealized_pnl) + ")\n";
            }
        }
        
        return output;
    }
    
    py::array_t<uint8_t> render_rgb() {
        // Generate RGB array representing the simulation state
        // This is a placeholder implementation
        int width = 800;
        int height = 600;
        py::array_t<uint8_t> result({height, width, 3});
        
        // Fill with a simple gradient (real implementation would draw actual visualization)
        auto buf = result.mutable_unchecked<3>();
        for (ssize_t i = 0; i < height; i++) {
            for (ssize_t j = 0; j < width; j++) {
                buf(i, j, 0) = static_cast<uint8_t>(i * 255 / height);  // R
                buf(i, j, 1) = static_cast<uint8_t>(j * 255 / width);   // G
                buf(i, j, 2) = 100;                                     // B
            }
        }
        
        return result;
    }
    
private:
    std::unique_ptr<SimulationEngine> sim_;
    std::unordered_map<std::string, std::shared_ptr<Actor>> actors_;
    std::unordered_map<std::string, std::shared_ptr<Exchange>> exchanges_;
    std::unordered_set<std::string> instruments_;
    std::string active_actor_id_;
    double last_portfolio_value_ = 0.0;
    uint64_t next_order_id_ = 0;
    
    // Generate a unique order ID
    std::string generate_order_id() {
        return "order_" + std::to_string(next_order_id_++);
    }
};

} // namespace sim

PYBIND11_MODULE(simulation_binding, m) {
    m.doc() = "Financial market simulation bindings for Python";
    
    // Bind the PySimulationBinding class
    py::class_<sim::PySimulationBinding>(m, "Simulation")
        .def(py::init<const std::unordered_map<std::string, std::string>&>(), 
             py::arg("config") = std::unordered_map<std::string, std::string>())
        .def("reset", &sim::PySimulationBinding::reset, py::arg("config") = std::unordered_map<std::string, std::string>())
        .def("execute_action", &sim::PySimulationBinding::execute_action)
        .def("advance_to_next_observation", &sim::PySimulationBinding::advance_to_next_observation)
        .def("get_observation", &sim::PySimulationBinding::get_observation)
        .def("calculate_reward", &sim::PySimulationBinding::calculate_reward)
        .def("is_done", &sim::PySimulationBinding::is_done)
        .def("get_info", &sim::PySimulationBinding::get_info)
        .def("set_active_actor", &sim::PySimulationBinding::set_active_actor)
        .def("register_actor", &sim::PySimulationBinding::register_actor)
        .def("register_exchange", &sim::PySimulationBinding::register_exchange)
        .def("add_instrument", &sim::PySimulationBinding::add_instrument)
        .def("render_human", &sim::PySimulationBinding::render_human)
        .def("render_rgb", &sim::PySimulationBinding::render_rgb);
    
    // Bind enum types if needed for Python
    py::enum_<sim::OrderSide>(m, "OrderSide")
        .value("BUY", sim::OrderSide::BUY)
        .value("SELL", sim::OrderSide::SELL)
        .export_values();
    
    py::enum_<sim::OrderType>(m, "OrderType")
        .value("MARKET", sim::OrderType::MARKET)
        .value("LIMIT", sim::OrderType::LIMIT)
        .value("STOP", sim::OrderType::STOP)
        .value("STOP_LIMIT", sim::OrderType::STOP_LIMIT)
        .value("ICEBERG", sim::OrderType::ICEBERG)
        .export_values();
    
    py::enum_<sim::OptionType>(m, "OptionType")
        .value("CALL", sim::OptionType::CALL)
        .value("PUT", sim::OptionType::PUT)
        .export_values();
}