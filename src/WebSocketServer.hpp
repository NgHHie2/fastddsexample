#pragma once
#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <cstdint>
#include <atomic>
#include <set>

class WebSocketServer {
private:
    typedef websocketpp::server<websocketpp::config::asio> server_t;
    typedef server_t::message_ptr message_ptr;
    typedef websocketpp::connection_hdl connection_hdl;
    
    server_t m_server;
    std::atomic<bool> m_running;
    std::set<connection_hdl, std::owner_less<connection_hdl>> m_connections;
    
    // Callback handlers
    void on_open(connection_hdl hdl);
    void on_close(connection_hdl hdl);
    void on_message(connection_hdl hdl, message_ptr msg);
    
public:
    WebSocketServer();
    void run(uint16_t port);
    void stop();
    void broadcast(const std::string& message);
};