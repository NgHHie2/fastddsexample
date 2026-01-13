#pragma once
#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <cstdint>
#include <atomic>
#include <set>
#include <memory>

class SharedCoordinateState;

class WebSocketServer {
private:
    typedef websocketpp::server<websocketpp::config::asio> server_t;
    typedef server_t::message_ptr message_ptr;
    typedef websocketpp::connection_hdl connection_hdl;
    
    server_t m_server;
    std::atomic<bool> m_running;
    std::set<connection_hdl, std::owner_less<connection_hdl>> m_connections;
    
    // Shared state for broadcasting
    std::shared_ptr<SharedCoordinateState> shared_state_;
    uint32_t last_broadcast_sequence_;
    uint32_t broadcast_rate_ms_;
    uint32_t broadcasts_sent_;
    
    // Callback handlers
    void on_open(connection_hdl hdl);
    void on_close(connection_hdl hdl);
    void on_message(connection_hdl hdl, message_ptr msg);
    
public:
    WebSocketServer(uint32_t broadcast_rate_ms = 50); // Default ~10Hz
    
    void run(uint16_t port);
    void stop();
    void broadcast(const std::string& message);
    
    void set_shared_state(std::shared_ptr<SharedCoordinateState> state);
};