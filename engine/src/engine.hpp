// simulation_engine.hpp
#pragma once

#include <queue>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace sim {

// Forward declarations
class Event;
class Actor;
class Exchange;
class SimulationState;

// Timestamp with nanosecond precision
using Timestamp = std::chrono::nanoseconds;

/**
 * @brief Virtual clock for the simulation
 */
class VirtualClock {
public:
    VirtualClock() : current_time_(0) {}
    
    // Get current simulation time
    Timestamp now() const { return current_time_; }
    
    // Advance clock to specific time
    void advance_to(Timestamp time) {
        if (time > current_time_) {
            current_time_ = time;
        }
    }
    
    // Reset clock to zero
    void reset() { current_time_ = Timestamp(0); }
    
private:
    Timestamp current_time_;
};

/**
 * @brief Base class for all simulation events
 */
class Event {
public:
    Event(Timestamp timestamp) : timestamp_(timestamp), id_(next_id_++) {}
    virtual ~Event() = default;
    
    // When this event should be processed
    Timestamp timestamp() const { return timestamp_; }
    
    // Unique ID for the event (for ordering events with same timestamp)
    uint64_t id() const { return id_; }
    
    // Process this event (implemented by derived classes)
    virtual void process(class SimulationEngine& engine) = 0;
    
    // Compare events for priority queue (earlier timestamp = higher priority)
    bool operator>(const Event& other) const {
        if (timestamp_ == other.timestamp_) {
            return id_ > other.id_; // FIFO for same timestamp
        }
        return timestamp_ > other.timestamp_;
    }
    
private:
    Timestamp timestamp_;
    uint64_t id_;
    static std::atomic<uint64_t> next_id_;
};

// Define comparison for shared_ptr<Event> for priority queue
struct EventComparator {
    bool operator()(const std::shared_ptr<Event>& a, const std::shared_ptr<Event>& b) const {
        return *a > *b;
    }
};

/**
 * @brief Main simulation engine class
 */
class SimulationEngine {
public:
    SimulationEngine();
    ~SimulationEngine();
    
    // Initialize the simulation with configuration
    void initialize(const std::unordered_map<std::string, std::string>& config);
    
    // Reset the simulation to initial state
    void reset();
    
    // Schedule an event to be processed at its timestamp
    void schedule_event(std::shared_ptr<Event> event);
    
    // Run simulation until specified end time
    void run_until(Timestamp end_time);
    
    // Run simulation for a specified duration from current time
    void run_for(Timestamp duration);
    
    // Run simulation until no more events or condition is met
    void run_until_condition(std::function<bool(const SimulationEngine&)> condition);
    
    // Get current simulation time
    Timestamp now() const { return clock_.now(); }
    
    // Register an actor with the simulation
    void register_actor(std::shared_ptr<Actor> actor);
    
    // Register an exchange with the simulation
    void register_exchange(std::shared_ptr<Exchange> exchange);
    
    // Set a configuration parameter
    void set_config(const std::string& key, const std::string& value);
    
    // Save current simulation state to a checkpoint
    void save_checkpoint(const std::string& filename);
    
    // Restore simulation state from a checkpoint
    void load_checkpoint(const std::string& filename);
    
    // Add a callback to be called at specified simulation intervals
    void add_periodic_callback(Timestamp interval, std::function<void(SimulationEngine&)> callback);
    
    // Get current simulation state
    const SimulationState& state() const { return *state_; }
    
private:
    // Process the next event in the queue
    bool process_next_event();
    
    // Core simulation components
    VirtualClock clock_;
    std::priority_queue<
        std::shared_ptr<Event>,
        std::vector<std::shared_ptr<Event>>,
        EventComparator
    > event_queue_;
    
    // Configuration settings
    std::unordered_map<std::string, std::string> config_;
    
    // Registered actors and exchanges
    std::vector<std::shared_ptr<Actor>> actors_;
    std::vector<std::shared_ptr<Exchange>> exchanges_;
    
    // Current simulation state
    std::unique_ptr<SimulationState> state_;
    
    // Periodic callbacks
    struct PeriodicCallback {
        Timestamp interval;
        Timestamp next_call;
        std::function<void(SimulationEngine&)> callback;
    };
    std::vector<PeriodicCallback> periodic_callbacks_;
    
    // Synchronization for parallel access
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool is_running_;
};

/**
 * @brief Class to store and manage complete simulation state
 */
class SimulationState {
public:
    SimulationState() = default;
    
    // Save/load methods for serialization
    void save_to_file(const std::string& filename) const;
    void load_from_file(const std::string& filename);
    
    // State tracking methods
    void record_trade(/* trade parameters */);
    void update_market_state(/* market state parameters */);
    void update_actor_state(/* actor state parameters */);
    
    // Get various statistics and state information
    // (to be implemented based on requirements)
};

} // namespace sim