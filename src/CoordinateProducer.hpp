#pragma once
#define ASIO_STANDALONE
#include <asio.hpp>
#include <memory>
#include <atomic>
#include <chrono>
#include <iostream>
#include "SharedCoordinateState.hpp"
#include "CoordinateGenerator.hpp"

class CoordinateProducer {
private:
    asio::io_context io_context_;
    asio::steady_timer timer_;
    std::shared_ptr<SharedCoordinateState> state_;
    CoordinateGenerator generator_;
    
    std::chrono::steady_clock::time_point next_deadline_;
    std::chrono::milliseconds period_;
    
    std::atomic<bool> running_;
    std::atomic<uint32_t> sequence_;
    
    void schedule_next() {
        if (!running_.load()) {
            return;
        }
        
        timer_.expires_at(next_deadline_);
        timer_.async_wait([this](const websocketpp::lib::error_code& ec) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    std::cerr << "[CoordinateProducer] Timer error: " << ec.message() << std::endl;
                }
                return;
            }
            
            // Generate tọa độ mới
            auto coords = generator_.get_next_coordinate();
            auto timestamp = CoordinateGenerator::get_timestamp();
            uint32_t seq = sequence_.fetch_add(1) + 1;
            
            // Update shared state
            state_->update(coords.first, coords.second, timestamp, seq);
            
            // Log định kỳ
            if (seq % 100 == 0) {
                std::cout << "[CoordinateProducer] Generated " << seq 
                         << " samples. Latest: [" << coords.first 
                         << ", " << coords.second << "]" << std::endl;
            }
            
            // Schedule next tick
            next_deadline_ += period_;
            schedule_next();
        });
    }
    
public:
    CoordinateProducer(std::shared_ptr<SharedCoordinateState> state,
                      std::chrono::milliseconds period = std::chrono::milliseconds(20),
                      double center_lon = 107.02243,
                      double center_lat = 20.76300)
        : timer_(io_context_)
        , state_(state)
        , generator_(center_lon, center_lat)
        , period_(period)
        , running_(false)
        , sequence_(0)
    {
    }
    
    void start() {
        if (running_.exchange(true)) {
            return; // Already running
        }
        
        std::cout << "[CoordinateProducer] Starting with period: " 
                 << period_.count() << "ms (~" 
                 << (1000.0 / period_.count()) << "Hz)" << std::endl;
        
        next_deadline_ = std::chrono::steady_clock::now() + period_;
        schedule_next();
    }
    
    void stop() {
        std::cout << "[CoordinateProducer] Stopping... Total generated: " 
                 << sequence_.load() << std::endl;
        running_.store(false);
        timer_.cancel();
        io_context_.stop();
    }
    
    void run() {
        start();
        io_context_.run();
    }
    
    bool is_running() const {
        return running_.load();
    }
    
    uint32_t get_sequence() const {
        return sequence_.load();
    }
};