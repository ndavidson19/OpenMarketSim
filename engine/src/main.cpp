// main.cpp - Distributed version with gRPC and ZeroMQ
#include "engine.hpp"
#include "exchange.hpp"
#include "actor.hpp"
#include "order_book.hpp"
#include <iostream>
#include <memory>
#include <thread>
#include <string>
#include <atomic>
#include <chrono>
#include <csignal>

// gRPC and Protocol Buffers includes
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include "simulation_service.grpc.pb.h"

// ZeroMQ includes
#include <zmq.hpp>

// Global control flags
std::atomic<bool> g_running(false);
std::atomic<bool> g_shutdown_requested(false);

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    std::cout << "Received signal " << signal << ", initiating shutdown..." << std::endl;
    g_shutdown_requested = true;
}

// gRPC Service Implementation
class SimulationServiceImpl final : public sim::grpc::SimulationService::Service {
public:
    SimulationServiceImpl(std::shared_ptr<sim::SimulationEngine> engine) 
        : engine_(engine) {}
    
    grpc::Status Initialize(grpc::ServerContext* context, 
                           const sim::grpc::InitializeRequest* request,
                           sim::grpc::InitializeResponse* response) override {
        std::cout << "Received Initialize request from orchestrator" << std::endl;
        
        try {
            // Convert gRPC config to internal config
            std::unordered_map<std::string, std::string> config;
            for (const auto& pair : request->config()) {
                config[pair.first] = pair.second;
            }
            
            // Initialize the engine
            engine_->initialize(config);
            
            // Set response status
            response->set_success(true);
            response->set_message("Simulation engine initialized successfully");
            
            return grpc::Status::OK;
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_message(std::string("Initialization failed: ") + e.what());
            return grpc::Status(grpc::StatusCode::INTERNAL, "Initialization failed");
        }
    }
    
    grpc::Status CreateExchange(grpc::ServerContext* context,
                               const sim::grpc::CreateExchangeRequest* request,
                               sim::grpc::CreateExchangeResponse* response) override {
        std::cout << "Creating exchange: " << request->exchange_id() << std::endl;
        
        try {
            auto exchange = std::make_shared<sim::Exchange>(request->exchange_id(), *engine_);
            engine_->register_exchange(exchange);
            exchanges_[request->exchange_id()] = exchange;
            
            // Configure latency and fee models if specified
            if (request->has_latency_model()) {
                auto latency_model = std::make_shared<sim::LatencyModel>(
                    sim::Timestamp(request->latency_model().base_latency_ns()),
                    sim::Timestamp(request->latency_model().jitter_ns()),
                    request->latency_model().congestion_factor()
                );
                exchange->set_latency_model(latency_model);
            }
            
            if (request->has_fee_model()) {
                auto fee_model = std::make_shared<sim::FeeModel>(
                    request->fee_model().maker_rate(),
                    request->fee_model().taker_rate(),
                    request->fee_model().min_fee()
                );
                exchange->set_fee_model(fee_model);
            }
            
            response->set_success(true);
            response->set_message("Exchange created successfully");
            return grpc::Status::OK;
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_message(std::string("Failed to create exchange: ") + e.what());
            return grpc::Status(grpc::StatusCode::INTERNAL, "Exchange creation failed");
        }
    }
    
    grpc::Status AddInstrument(grpc::ServerContext* context,
                              const sim::grpc::AddInstrumentRequest* request,
                              sim::grpc::AddInstrumentResponse* response) override {
        std::cout << "Adding instrument " << request->instrument_id() 
                  << " to exchange " << request->exchange_id() << std::endl;
        
        try {
            auto exchange_it = exchanges_.find(request->exchange_id());
            if (exchange_it == exchanges_.end()) {
                response->set_success(false);
                response->set_message("Exchange not found");
                return grpc::Status(grpc::StatusCode::NOT_FOUND, "Exchange not found");
            }
            
            exchange_it->second->add_instrument(request->instrument_id(), request->initial_price());
            
            response->set_success(true);
            response->set_message("Instrument added successfully");
            return grpc::Status::OK;
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_message(std::string("Failed to add instrument: ") + e.what());
            return grpc::Status(grpc::StatusCode::INTERNAL, "Instrument addition failed");
        }
    }
    
    grpc::Status RegisterActor(grpc::ServerContext* context,
                              const sim::grpc::RegisterActorRequest* request,
                              sim::grpc::RegisterActorResponse* response) override {
        std::cout << "Registering actor: " << request->actor_id() 
                  << " of type " << request->actor_type() << std::endl;
        
        try {
            std::shared_ptr<sim::Actor> actor;
            
            // Create the appropriate actor type
            if (request->actor_type() == "market_maker") {
                actor = std::make_shared<sim::MarketMaker>(
                    request->actor_id(),
                    request->initial_capital(),
                    request->params().count("target_spread") ? std::stod(request->params().at("target_spread")) : 0.05,
                    request->params().count("max_position") ? std::stod(request->params().at("max_position")) : 1000.0,
                    request->params().count("order_size_pct") ? std::stod(request->params().at("order_size_pct")) : 0.05
                );
            } else if (request->actor_type() == "trend_follower") {
                actor = std::make_shared<sim::TrendFollower>(
                    request->actor_id(),
                    request->initial_capital(),
                    request->params().count("short_window") ? std::stoi(request->params().at("short_window")) : 10,
                    request->params().count("long_window") ? std::stoi(request->params().at("long_window")) : 30,
                    request->params().count("position_size_pct") ? std::stod(request->params().at("position_size_pct")) : 0.1
                );
            } else if (request->actor_type() == "hft") {
                actor = std::make_shared<sim::HighFrequencyTrader>(
                    request->actor_id(),
                    request->initial_capital(),
                    request->params().count("min_edge") ? std::stod(request->params().at("min_edge")) : 0.0001,
                    sim::Timestamp(request->params().count("reaction_time") ? 
                        std::stoll(request->params().at("reaction_time")) : 100000)
                );
            } else if (request->actor_type() == "retail") {
                actor = std::make_shared<sim::RetailTrader>(
                    request->actor_id(),
                    request->initial_capital(),
                    request->params().count("risk_aversion") ? std::stod(request->params().at("risk_aversion")) : 0.5,
                    request->params().count("fomo_susceptibility") ? std::stod(request->params().at("fomo_susceptibility")) : 0.3,
                    request->params().count("loss_aversion") ? std::stod(request->params().at("loss_aversion")) : 2.0
                );
            } else {
                response->set_success(false);
                response->set_message("Unknown actor type: " + request->actor_type());
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Unknown actor type");
            }
            
            // Register the actor with the engine
            engine_->register_actor(actor);
            actors_[request->actor_id()] = actor;
            
            // Subscribe to market data if specified
            for (const auto& instrument_id : request->instruments()) {
                for (const auto& [exchange_id, exchange] : exchanges_) {
                    exchange->subscribe_to_market_data(request->actor_id(), instrument_id);
                }
            }
            
            response->set_success(true);
            response->set_message("Actor registered successfully");
            return grpc::Status::OK;
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_message(std::string("Failed to register actor: ") + e.what());
            return grpc::Status(grpc::StatusCode::INTERNAL, "Actor registration failed");
        }
    }
    
    grpc::Status ExecuteOrder(grpc::ServerContext* context,
                             const sim::grpc::OrderRequest* request,
                             sim::grpc::OrderResponse* response) override {
        std::cout << "Executing order for actor " << request->actor_id() << std::endl;
        
        try {
            auto actor_it = actors_.find(request->actor_id());
            if (actor_it == actors_.end()) {
                response->set_success(false);
                response->set_message("Actor not found");
                return grpc::Status(grpc::StatusCode::NOT_FOUND, "Actor not found");
            }
            
            auto exchange_it = exchanges_.find(request->exchange_id());
            if (exchange_it == exchanges_.end()) {
                response->set_success(false);
                response->set_message("Exchange not found");
                return grpc::Status(grpc::StatusCode::NOT_FOUND, "Exchange not found");
            }
            
            // Create order
            sim::OrderSide side = request->side() == "BUY" ? sim::OrderSide::BUY : sim::OrderSide::SELL;
            sim::OrderType type;
            
            if (request->type() == "MARKET") {
                type = sim::OrderType::MARKET;
            } else if (request->type() == "LIMIT") {
                type = sim::OrderType::LIMIT;
            } else {
                response->set_success(false);
                response->set_message("Unsupported order type: " + request->type());
                return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Unsupported order type");
            }
            
            auto order = std::make_shared<sim::Order>(
                request->order_id(),
                request->instrument_id(),
                side,
                type,
                request->quantity(),
                request->price(),
                sim::TimeInForce::DAY,
                sim::Timestamp::max(),
                request->actor_id()
            );
            
            // Submit the order
            actor_it->second->submit_order(exchange_it->second, order);
            
            response->set_success(true);
            response->set_message("Order submitted successfully");
            return grpc::Status::OK;
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_message(std::string("Failed to execute order: ") + e.what());
            return grpc::Status(grpc::StatusCode::INTERNAL, "Order execution failed");
        }
    }
    
    grpc::Status CancelOrder(grpc::ServerContext* context,
                            const sim::grpc::CancelOrderRequest* request,
                            sim::grpc::CancelOrderResponse* response) override {
        std::cout << "Cancelling order " << request->order_id() 
                  << " for actor " << request->actor_id() << std::endl;
        
        try {
            auto actor_it = actors_.find(request->actor_id());
            if (actor_it == actors_.end()) {
                response->set_success(false);
                response->set_message("Actor not found");
                return grpc::Status(grpc::StatusCode::NOT_FOUND, "Actor not found");
            }
            
            auto exchange_it = exchanges_.find(request->exchange_id());
            if (exchange_it == exchanges_.end()) {
                response->set_success(false);
                response->set_message("Exchange not found");
                return grpc::Status(grpc::StatusCode::NOT_FOUND, "Exchange not found");
            }
            
            // Cancel the order
            actor_it->second->cancel_order(exchange_it->second, request->order_id());
            
            response->set_success(true);
            response->set_message("Order cancellation requested");
            return grpc::Status::OK;
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_message(std::string("Failed to cancel order: ") + e.what());
            return grpc::Status(grpc::StatusCode::INTERNAL, "Order cancellation failed");
        }
    }
    
    grpc::Status RunSimulation(grpc::ServerContext* context,
                              const sim::grpc::RunSimulationRequest* request,
                              sim::grpc::RunSimulationResponse* response) override {
        std::cout << "Running simulation for " << request->duration_ns() << " ns" << std::endl;
        
        try {
            // Check if already running
            if (g_running) {
                response->set_success(false);
                response->set_message("Simulation already running");
                return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Simulation already running");
            }
            
            // Set the running flag
            g_running = true;
            
            // Open all exchanges
            for (auto& [id, exchange] : exchanges_) {
                exchange->open_trading_session();
            }
            
            // Run the simulation in a separate thread
            std::thread simulation_thread([this, request]() {
                engine_->run_for(sim::Timestamp(request->duration_ns()));
                
                // Close all exchanges when done
                for (auto& [id, exchange] : exchanges_) {
                    exchange->close_trading_session();
                }
                
                g_running = false;
            });
            
            simulation_thread.detach();
            
            response->set_success(true);
            response->set_message("Simulation started");
            return grpc::Status::OK;
        } catch (const std::exception& e) {
            g_running = false;
            response->set_success(false);
            response->set_message(std::string("Failed to run simulation: ") + e.what());
            return grpc::Status(grpc::StatusCode::INTERNAL, "Simulation run failed");
        }
    }
    
    grpc::Status GetSimulationState(grpc::ServerContext* context,
                                  const sim::grpc::GetStateRequest* request,
                                  sim::grpc::SimulationState* response) override {
        // Fill in simulation state response
        response->set_current_time_ns(engine_->now().count());
        response->set_running(g_running);
        
        // Add exchange states
        for (const auto& [exchange_id, exchange] : exchanges_) {
            auto exchange_state = response->add_exchanges();
            exchange_state->set_exchange_id(exchange_id);
            exchange_state->set_trading_open(exchange->is_trading_open());
            
            // Add instrument states
            for (const auto& instrument_id : request->instruments()) {
                auto order_book = exchange->get_order_book(instrument_id);
                if (!order_book) continue;
                
                auto instrument_state = exchange_state->add_instruments();
                instrument_state->set_instrument_id(instrument_id);
                
                if (order_book->best_bid()) {
                    instrument_state->set_best_bid(*order_book->best_bid());
                }
                
                if (order_book->best_ask()) {
                    instrument_state->set_best_ask(*order_book->best_ask());
                }
                
                if (order_book->mid_price()) {
                    instrument_state->set_mid_price(*order_book->mid_price());
                }
                
                // Add bid levels
                auto bid_levels = order_book->get_bids(5);
                for (const auto& [price, volume] : bid_levels) {
                    auto level = instrument_state->add_bid_levels();
                    level->set_price(price);
                    level->set_volume(volume);
                }
                
                // Add ask levels
                auto ask_levels = order_book->get_asks(5);
                for (const auto& [price, volume] : ask_levels) {
                    auto level = instrument_state->add_ask_levels();
                    level->set_price(price);
                    level->set_volume(volume);
                }
            }
        }
        
        // Add actor states
        for (const auto& actor_id : request->actors()) {
            auto actor_it = actors_.find(actor_id);
            if (actor_it == actors_.end()) continue;
            
            auto actor = actor_it->second;
            auto actor_state = response->add_actors();
            actor_state->set_actor_id(actor_id);
            actor_state->set_capital(actor->capital());
            
            // Add positions
            for (const auto& [instrument_id, position] : actor->positions()) {
                auto pos = actor_state->add_positions();
                pos->set_instrument_id(instrument_id);
                pos->set_quantity(position.quantity);
                pos->set_avg_price(position.avg_price);
                pos->set_realized_pnl(position.realized_pnl);
                pos->set_unrealized_pnl(position.unrealized_pnl);
            }
        }
        
        return grpc::Status::OK;
    }
    
private:
    std::shared_ptr<sim::SimulationEngine> engine_;
    std::unordered_map<std::string, std::shared_ptr<sim::Exchange>> exchanges_;
    std::unordered_map<std::string, std::shared_ptr<sim::Actor>> actors_;
};

// ZeroMQ Market Data Publisher
class MarketDataPublisher {
public:
    MarketDataPublisher(const std::string& endpoint = "tcp://*:5555") 
        : context_(1), socket_(context_, zmq::socket_type::pub) {
        socket_.bind(endpoint);
        std::cout << "Market data publisher bound to " << endpoint << std::endl;
    }
    
    void publish_market_data(const std::string& instrument_id, const sim::MarketDataUpdate& update) {
        // Create simplified market data message
        std::stringstream ss;
        
        ss << instrument_id << " "
           << update.timestamp.count() << " ";
           
        if (update.best_bid) {
            ss << *update.best_bid << " ";
        } else {
            ss << "0.0 ";
        }
        
        if (update.best_ask) {
            ss << *update.best_ask << " ";
        } else {
            ss << "0.0 ";
        }
        
        if (update.last_trade) {
            ss << update.last_trade->price << " " 
               << update.last_trade->quantity;
        } else {
            ss << "0.0 0.0";
        }
        
        // Create ZMQ message and send
        zmq::message_t message(ss.str());
        socket_.send(message, zmq::send_flags::none);
    }
    
private:
    zmq::context_t context_;
    zmq::socket_t socket_;
};

int main(int argc, char** argv) {
    // Parse command line arguments
    std::string server_address = "0.0.0.0:50051";
    if (argc > 1) {
        server_address = argv[1];
    }
    
    // Set up signal handling for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Create the simulation engine
    auto engine = std::make_shared<sim::SimulationEngine>();
    
    // Create market data publisher
    MarketDataPublisher market_data_publisher;
    
    // Set up the gRPC service
    SimulationServiceImpl service(engine);
    
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    grpc::ServerBuilder builder;
    
    // Listen on the given address without authentication
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    
    // Register the service
    builder.RegisterService(&service);
    
    // Start the server
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Simulation node started, listening on " << server_address << std::endl;
    
    // Keep running until shutdown is requested
    while (!g_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Shutdown requested, stopping server..." << std::endl;
    
    // Stop the server
    server->Shutdown();
    
    std::cout << "Server stopped. Exiting." << std::endl;
    
    return 0;
}