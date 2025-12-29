#include "WebSocketServer.hpp"
#include <iostream>

WebSocketServer::WebSocketServer() : m_running(false) {
}

void WebSocketServer::on_open(connection_hdl hdl) {
    m_connections.insert(hdl);
    std::cout << "[WebSocket] Client connected. Total clients: " << m_connections.size() << std::endl;
}

void WebSocketServer::on_close(connection_hdl hdl) {
    m_connections.erase(hdl);
    std::cout << "[WebSocket] Client disconnected. Total clients: " << m_connections.size() << std::endl;
}

void WebSocketServer::on_message(connection_hdl hdl, message_ptr msg) {
    std::string payload = msg->get_payload();
    
    std::cout << "==================================" << std::endl;
    std::cout << "[WebSocket] Received message:" << std::endl;
    std::cout << "Length: " << payload.length() << " bytes" << std::endl;
    std::cout << "Content: " << payload << std::endl;
    std::cout << "==================================" << std::endl;
    
    // Echo lại cho client (tùy chọn)
    try {
        m_server.send(hdl, "Server received: " + payload, msg->get_opcode());
    } catch (const websocketpp::exception& e) {
        std::cerr << "Echo failed: " << e.what() << std::endl;
    }
}

void WebSocketServer::run(uint16_t port) {
    m_running = true;
    
    try {
        // Khởi tạo ASIO
        m_server.init_asio();
        
        // Đăng ký các callback
        m_server.set_open_handler(std::bind(&WebSocketServer::on_open, this, std::placeholders::_1));
        m_server.set_close_handler(std::bind(&WebSocketServer::on_close, this, std::placeholders::_1));
        m_server.set_message_handler(std::bind(&WebSocketServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
        
        // Thiết lập log
        m_server.set_error_channels(websocketpp::log::elevel::all);
        m_server.set_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_payload);
        
        // Lắng nghe
        m_server.listen(port);
        m_server.start_accept();
        
        std::cout << "[WebSocket] Server listening on port " << port << std::endl;
        
        // Chạy với vòng lặp kiểm tra flag
        while (m_running) {
            m_server.poll_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
    } catch (const websocketpp::exception& e) {
        std::cerr << "[WebSocket] Exception: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Error: " << e.what() << std::endl;
    }
}

void WebSocketServer::stop() {
    std::cout << "[WebSocket] Stopping server..." << std::endl;
    m_running = false;
    
    try {
        // Đóng tất cả connections
        for (auto& conn : m_connections) {
            m_server.close(conn, websocketpp::close::status::going_away, "Server shutting down");
        }
        
        m_server.stop_listening();
        m_server.stop();
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Error stopping: " << e.what() << std::endl;
    }
}

void WebSocketServer::broadcast(const std::string& message) {
    for (auto& conn : m_connections) {
        try {
            m_server.send(conn, message, websocketpp::frame::opcode::text);
        } catch (const websocketpp::exception& e) {
            std::cerr << "[WebSocket] Broadcast error: " << e.what() << std::endl;
        }
    }
}