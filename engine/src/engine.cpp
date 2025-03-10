// engine.cpp
#include "engine.hpp"
#include "actor.hpp"
#include "exchange.hpp"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <fstream>

namespace sim {

// Initialize static member
std::atomic<uint64_t> Event::next_id_(0);

SimulationEngine::SimulationEngine() : state_(std::make_unique<SimulationState>()), is_running_(false) {
    // Default initialization
}

SimulationEngine::~SimulationEngine() {
    // Ensure simulation is stopped
    is_running_ = false;
}

void SimulationEngine::initialize(const std::unordered_map<std::string, std::string>& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Store configuration
    config_ = config;
    
    // Reset the clock and state
    clock_.reset();
    state_ = std::make_unique<SimulationState>();
    
    // Clear event queue
    std::priority_queue<
        std::shared_ptr<Event>,
        std::vector<std::shared_ptr<Event>>,
        EventComparator
    > empty_queue;
    std::swap(event_queue_, empty_queue);
    
    // Clear actors and exchanges
    actors_.clear();
    exchanges_.clear();
    
    // Clear periodic callbacks
    periodic_callbacks_.clear();
}

void SimulationEngine::reset() {
    initialize(config_);
}

void SimulationEngine::schedule_event(std::shared_ptr<Event> event) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_queue_.push(event);
    cv_.notify_one();  // Notify any waiting threads
}

void SimulationEngine::run_until(Timestamp end_time) {
    is_running_ = true;
    
    while (is_running_) {
        // Process next event if available
        if (!process_next_event()) {
            break;  // No more events
        }
        
        // Check if we've reached the end time
        if (clock_.now() >= end_time) {
            break;
        }
    }
    
    is_running_ = false;
}

void SimulationEngine::run_for(Timestamp duration) {
    run_until(clock_.now() + duration);
}

void SimulationEngine::run_until_condition(std::function<bool(const SimulationEngine&)> condition) {
    is_running_ = true;
    
    while (is_running_) {
        // Check condition
        if (condition(*this)) {
            break;
        }
        
        // Process next event if available
        if (!process_next_event()) {
            break;  // No more events
        }
    }
    
    is_running_ = false;
}

bool SimulationEngine::process_next_event() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Wait for events if queue is empty
    if (event_queue_.empty()) {
        return false;  // No more events to process
    }
    
    // Get next event
    auto event = event_queue_.top();
    event_queue_.pop();
    
    // Update simulation clock to event time
    clock_.advance_to(event->timestamp());
    
    // Process periodic callbacks that should run before this event
    for (auto& cb : periodic_callbacks_) {
        if (clock_.now() >= cb.next_call) {
            // Execute callback
            cb.callback(*this);
            
            // Schedule next call
            while (cb.next_call <= clock_.now()) {
                cb.next_call += cb.interval;
            }
        }
    }
    
    // Release lock before processing event to avoid deadlocks
    lock.unlock();
    
    // Process the event
    event->process(*this);
    
    return true;
}

void SimulationEngine::register_actor(std::shared_ptr<Actor> actor) {
    std::lock_guard<std::mutex> lock(mutex_);
    actors_.push_back(actor);
    
    // Initialize the actor
    actor->initialize(*this);
}

void SimulationEngine::register_exchange(std::shared_ptr<Exchange> exchange) {
    std::lock_guard<std::mutex> lock(mutex_);
    exchanges_.push_back(exchange);
}

void SimulationEngine::set_config(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_[key] = value;
}

void SimulationEngine::save_checkpoint(const std::string& filename) {
    state_->save_to_file(filename);
}

void SimulationEngine::load_checkpoint(const std::string& filename) {
    state_->load_from_file(filename);
}

void SimulationEngine::add_periodic_callback(Timestamp interval, std::function<void(SimulationEngine&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    PeriodicCallback cb;
    cb.interval = interval;
    cb.next_call = clock_.now() + interval;
    cb.callback = callback;
    
    periodic_callbacks_.push_back(cb);
}

// SimulationState implementation

void SimulationState::save_to_file(const std::string& filename) const {
    // Simple binary serialization for demonstration
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to open file for writing: " + filename);
    }
    
    // Write serialization version
    uint32_t version = 1;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write state data (placeholder implementation)
    // In a real implementation, you would serialize all relevant state
    
    out.close();
}

void SimulationState::load_from_file(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open file for reading: " + filename);
    }
    
    // Read serialization version
    uint32_t version;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    if (version != 1) {
        throw std::runtime_error("Unsupported checkpoint version: " + std::to_string(version));
    }
    
    // Read state data (placeholder implementation)
    // In a real implementation, you would deserialize all relevant state
    
    in.close();
}

void SimulationState::record_trade(/* trade parameters */) {
    // Implement trade recording
}

void SimulationState::update_market_state(/* market state parameters */) {
    // Implement market state updates
}

void SimulationState::update_actor_state(/* actor state parameters */) {
    // Implement actor state updates
}

} // namespace sim