// market_events.hpp
#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <random>
#include "engine.hpp"

namespace sim {

/**
 * @brief Market event type enumeration
 */
enum class EventType {
    EARNINGS,           // Company earnings announcements
    ECONOMIC_DATA,      // Economic indicators (GDP, unemployment, etc.)
    MONETARY_POLICY,    // Central bank decisions (rate changes, QE, etc.)
    GEOPOLITICAL,       // Political events affecting markets
    REGULATORY,         // Regulatory changes affecting markets
    SECTOR_NEWS,        // Industry/sector specific news
    COMPANY_NEWS,       // Company specific news (excluding earnings)
    TECHNICAL_EVENT,    // Technical market events (circuit breakers, etc.)
    SENTIMENT_SHIFT,    // Major shifts in market sentiment
    NATURAL_DISASTER,   // Natural disasters with economic impact
    MARKET_STRUCTURE,   // Changes to market structure or rules
    LIQUIDITY_EVENT     // Sudden changes in market liquidity
};

/**
 * @brief Impact direction enumeration
 */
enum class ImpactDirection {
    POSITIVE,           // Positive impact on price/sentiment
    NEGATIVE,           // Negative impact on price/sentiment
    MIXED,              // Mixed or unclear impact
    VOLATILE            // Increases volatility without clear direction
};

/**
 * @brief Market event base class
 */
class MarketEvent {
public:
    MarketEvent(
        std::string id,
        EventType type,
        Timestamp timestamp,
        std::vector<std::string> affected_instruments,
        ImpactDirection direction,
        double magnitude,
        std::string description
    );
    
    virtual ~MarketEvent() = default;
    
    // Basic event properties
    const std::string& id() const { return id_; }
    EventType type() const { return type_; }
    Timestamp timestamp() const { return timestamp_; }
    const std::vector<std::string>& affected_instruments() const { return affected_instruments_; }
    ImpactDirection direction() const { return direction_; }
    double magnitude() const { return magnitude_; }
    const std::string& description() const { return description_; }
    
    // Get event data as key-value pairs
    virtual std::unordered_map<std::string, std::string> get_data() const;
    
    // Calculate price impact for a specific instrument
    virtual double calculate_price_impact(const std::string& instrument_id) const;
    
    // Calculate volatility impact for a specific instrument
    virtual double calculate_volatility_impact(const std::string& instrument_id) const;
    
    // Calculate sentiment impact for a specific instrument or the market
    virtual double calculate_sentiment_impact(const std::string& instrument_id = "") const;
    
    // Calculate liquidity impact for a specific instrument
    virtual double calculate_liquidity_impact(const std::string& instrument_id) const;
    
    // Clone the event
    virtual std::shared_ptr<MarketEvent> clone() const;
    
protected:
    std::string id_;
    EventType type_;
    Timestamp timestamp_;
    std::vector<std::string> affected_instruments_;
    ImpactDirection direction_;
    double magnitude_;  // 0.0 to 1.0 scale of impact magnitude
    std::string description_;
};

/**
 * @brief Earnings announcement event
 */
class EarningsEvent : public MarketEvent {
public:
    EarningsEvent(
        std::string id,
        Timestamp timestamp,
        std::string company_id,
        double eps_actual,
        double eps_estimate,
        double revenue_actual,
        double revenue_estimate,
        std::string guidance,
        std::string description
    );
    
    // Earnings-specific data
    double eps_actual() const { return eps_actual_; }
    double eps_estimate() const { return eps_estimate_; }
    double revenue_actual() const { return revenue_actual_; }
    double revenue_estimate() const { return revenue_estimate_; }
    const std::string& guidance() const { return guidance_; }
    
    // Override base methods
    std::unordered_map<std::string, std::string> get_data() const override;
    double calculate_price_impact(const std::string& instrument_id) const override;
    double calculate_volatility_impact(const std::string& instrument_id) const override;
    std::shared_ptr<MarketEvent> clone() const override;
    
private:
    double eps_actual_;
    double eps_estimate_;
    double revenue_actual_;
    double revenue_estimate_;
    std::string guidance_;  // positive, neutral, negative
};

/**
 * @brief Economic data announcement event
 */
class EconomicDataEvent : public MarketEvent {
public:
    EconomicDataEvent(
        std::string id,
        Timestamp timestamp,
        std::string indicator_name,
        double actual_value,
        double expected_value,
        double previous_value,
        std::vector<std::string> affected_sectors,
        ImpactDirection direction,
        double magnitude,
        std::string description
    );
    
    // Economic data specific information
    const std::string& indicator_name() const { return indicator_name_; }
    double actual_value() const { return actual_value_; }
    double expected_value() const { return expected_value_; }
    double previous_value() const { return previous_value_; }
    const std::vector<std::string>& affected_sectors() const { return affected_sectors_; }
    
    // Override base methods
    std::unordered_map<std::string, std::string> get_data() const override;
    double calculate_price_impact(const std::string& instrument_id) const override;
    std::shared_ptr<MarketEvent> clone() const override;
    
private:
    std::string indicator_name_;
    double actual_value_;
    double expected_value_;
    double previous_value_;
    std::vector<std::string> affected_sectors_;
};

/**
 * @brief Monetary policy event
 */
class MonetaryPolicyEvent : public MarketEvent {
public:
    MonetaryPolicyEvent(
        std::string id,
        Timestamp timestamp,
        std::string central_bank,
        double rate_change,
        double previous_rate,
        double new_rate,
        bool quantitative_easing,
        double qe_amount,
        std::string policy_statement,
        ImpactDirection direction,
        double magnitude,
        std::string description
    );
    
    // Monetary policy specific data
    const std::string& central_bank() const { return central_bank_; }
    double rate_change() const { return rate_change_; }
    double previous_rate() const { return previous_rate_; }
    double new_rate() const { return new_rate_; }
    bool quantitative_easing() const { return quantitative_easing_; }
    double qe_amount() const { return qe_amount_; }
    const std::string& policy_statement() const { return policy_statement_; }
    
    // Override base methods
    std::unordered_map<std::string, std::string> get_data() const override;
    double calculate_price_impact(const std::string& instrument_id) const override;
    double calculate_sentiment_impact(const std::string& instrument_id = "") const override;
    std::shared_ptr<MarketEvent> clone() const override;
    
private:
    std::string central_bank_;
    double rate_change_;
    double previous_rate_;
    double new_rate_;
    bool quantitative_easing_;
    double qe_amount_;
    std::string policy_statement_;
};

/**
 * @brief Sentiment event
 */
class SentimentEvent : public MarketEvent {
public:
    SentimentEvent(
        std::string id,
        Timestamp timestamp,
        std::string source,
        double sentiment_score,
        std::vector<std::string> affected_instruments,
        std::vector<std::string> related_topics,
        ImpactDirection direction,
        double magnitude,
        std::string description
    );
    
    // Sentiment specific data
    const std::string& source() const { return source_; }
    double sentiment_score() const { return sentiment_score_; }
    const std::vector<std::string>& related_topics() const { return related_topics_; }
    
    // Override base methods
    std::unordered_map<std::string, std::string> get_data() const override;
    double calculate_sentiment_impact(const std::string& instrument_id = "") const override;
    double calculate_volatility_impact(const std::string& instrument_id) const override;
    std::shared_ptr<MarketEvent> clone() const override;
    
private:
    std::string source_;
    double sentiment_score_;  // -1.0 (extremely negative) to 1.0 (extremely positive)
    std::vector<std::string> related_topics_;
};

/**
 * @brief News event
 */
class NewsEvent : public MarketEvent {
public:
    NewsEvent(
        std::string id,
        Timestamp timestamp,
        std::string headline,
        std::string source,
        std::vector<std::string> categories,
        std::vector<std::string> entities,
        double sentiment_score,
        std::vector<std::string> affected_instruments,
        ImpactDirection direction,
        double magnitude,
        std::string description
    );
    
    // News specific data
    const std::string& headline() const { return headline_; }
    const std::string& source() const { return source_; }
    const std::vector<std::string>& categories() const { return categories_; }
    const std::vector<std::string>& entities() const { return entities_; }
    double sentiment_score() const { return sentiment_score_; }
    
    // Override base methods
    std::unordered_map<std::string, std::string> get_data() const override;
    double calculate_price_impact(const std::string& instrument_id) const override;
    double calculate_sentiment_impact(const std::string& instrument_id = "") const override;
    std::shared_ptr<MarketEvent> clone() const override;
    
private:
    std::string headline_;
    std::string source_;
    std::vector<std::string> categories_;
    std::vector<std::string> entities_;
    double sentiment_score_;
};

/**
 * @brief Market structure event (circuit breakers, trading halts, etc.)
 */
class MarketStructureEvent : public MarketEvent {
public:
    MarketStructureEvent(
        std::string id,
        Timestamp timestamp,
        std::string event_name,
        std::string exchange_id,
        std::vector<std::string> affected_instruments,
        Timestamp duration,
        std::string reason,
        ImpactDirection direction,
        double magnitude,
        std::string description
    );
    
    // Market structure specific data
    const std::string& event_name() const { return event_name_; }
    const std::string& exchange_id() const { return exchange_id_; }
    Timestamp duration() const { return duration_; }
    const std::string& reason() const { return reason_; }
    
    // Override base methods
    std::unordered_map<std::string, std::string> get_data() const override;
    double calculate_liquidity_impact(const std::string& instrument_id) const override;
    double calculate_volatility_impact(const std::string& instrument_id) const override;
    std::shared_ptr<MarketEvent> clone() const override;
    
private:
    std::string event_name_;
    std::string exchange_id_;
    Timestamp duration_;
    std::string reason_;
};

/**
 * @brief Event generator interface
 */
class EventGenerator {
public:
    virtual ~EventGenerator() = default;
    
    // Generate events for a specific time period
    virtual std::vector<std::shared_ptr<MarketEvent>> generate_events(
        Timestamp start_time,
        Timestamp end_time,
        const std::vector<std::string>& instruments
    ) = 0;
};

/**
 * @brief Scheduled event generator for predictable events
 */
class ScheduledEventGenerator : public EventGenerator {
public:
    ScheduledEventGenerator();
    
    // Add a scheduled event
    void add_scheduled_event(std::shared_ptr<MarketEvent> event);
    
    // Generate events that fall within the specified time range
    std::vector<std::shared_ptr<MarketEvent>> generate_events(
        Timestamp start_time,
        Timestamp end_time,
        const std::vector<std::string>& instruments
    ) override;
    
private:
    std::vector<std::shared_ptr<MarketEvent>> scheduled_events_;
};

/**
 * @brief Random event generator based on probabilities
 */
class RandomEventGenerator : public EventGenerator {
public:
    RandomEventGenerator(unsigned int seed = 0);
    
    // Configure event probabilities
    void set_event_probability(EventType type, double hourly_probability);
    
    // Configure event parameters
    void set_event_parameters(EventType type, const std::unordered_map<std::string, double>& params);
    
    // Generate random events
    std::vector<std::shared_ptr<MarketEvent>> generate_events(
        Timestamp start_time,
        Timestamp end_time,
        const std::vector<std::string>& instruments
    ) override;
    
private:
    std::mt19937 rng_;
    std::unordered_map<EventType, double> event_probabilities_;
    std::unordered_map<EventType, std::unordered_map<std::string, double>> event_parameters_;
    
    // Generate a specific type of event
    std::shared_ptr<MarketEvent> generate_event(
        EventType type,
        Timestamp time,
        const std::vector<std::string>& instruments
    );
};

/**
 * @brief Event distribution system
 */
class EventDistributor {
public:
    EventDistributor(SimulationEngine& engine);
    
    // Register an event generator
    void register_generator(std::shared_ptr<EventGenerator> generator);
    
    // Register an event handler for an actor
    void register_handler(
        const std::string& actor_id,
        std::function<void(const MarketEvent&)> handler
    );
    
    // Generate and distribute events for a time period
    void distribute_events(
        Timestamp start_time,
        Timestamp end_time,
        const std::vector<std::string>& instruments
    );
    
    // Distribute a specific event
    void distribute_event(std::shared_ptr<MarketEvent> event);
    
private:
    SimulationEngine& engine_;
    std::vector<std::shared_ptr<EventGenerator>> generators_;
    std::unordered_map<std::string, std::function<void(const MarketEvent&)>> handlers_;
};

/**
 * @brief Event for the simulation engine
 */
class EventOccurrenceEvent : public Event {
public:
    EventOccurrenceEvent(
        Timestamp timestamp,
        std::shared_ptr<MarketEvent> market_event
    );
    
    // Process this event
    void process(SimulationEngine& engine) override;
    
private:
    std::shared_ptr<MarketEvent> market_event_;
};

} // namespace sim