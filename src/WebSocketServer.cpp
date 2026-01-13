#include "WebSocketServer.hpp"
#include "SharedCoordinateState.hpp"
#include <iostream>
#include <chrono>
#include <thread>

WebSocketServer::WebSocketServer(uint32_t broadcast_rate_ms) 
    : m_running(false)
    , last_broadcast_sequence_(0)
    , broadcast_rate_ms_(broadcast_rate_ms)
    , broadcasts_sent_(0)
{
}

void WebSocketServer::on_open(connection_hdl hdl) {
    try {
        auto con = m_server.get_con_from_hdl(hdl);
        auto& socket = con->get_socket();

        socket.set_option(asio::ip::tcp::no_delay(true));
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] TCP_NODELAY failed: " << e.what() << std::endl;
    }

    m_connections.insert(hdl);
    std::cout << "[WebSocket] Client connected. Total clients: "
              << m_connections.size() << std::endl;
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
    
    try {
        m_server.send(hdl, "Server received: " + payload, msg->get_opcode());
    } catch (const websocketpp::exception& e) {
        std::cerr << "[WebSocket] Echo failed: " << e.what() << std::endl;
    }
}

void WebSocketServer::run(uint16_t port) {
    m_running = true;

    try {
        // 1. init asio
        m_server.init_asio();

        // 3. handlers
        m_server.set_open_handler(
            std::bind(&WebSocketServer::on_open, this, std::placeholders::_1));
        m_server.set_close_handler(
            std::bind(&WebSocketServer::on_close, this, std::placeholders::_1));
        m_server.set_message_handler(
            std::bind(&WebSocketServer::on_message, this,
                      std::placeholders::_1, std::placeholders::_2));

        // 4. logging
        m_server.set_error_channels(websocketpp::log::elevel::all);
        m_server.set_access_channels(
            websocketpp::log::alevel::all ^
            websocketpp::log::alevel::frame_payload);

        // 5. listen
        m_server.listen(port);
        m_server.start_accept();

        std::cout << "[WebSocket] Server listening on port " << port << std::endl;

        // ===============================
        // Deadline-based broadcast (DDS-style)
        // ===============================
        auto& io = m_server.get_io_service();
        auto period = std::chrono::milliseconds(broadcast_rate_ms_);

        auto timer = std::make_shared<asio::steady_timer>(io);
        auto next_deadline = std::chrono::steady_clock::now() + period;

        std::function<void()> schedule;
        schedule = [this, timer, period, &next_deadline, &schedule]() {
            timer->expires_at(next_deadline);
            timer->async_wait([this, timer, period, &next_deadline, &schedule]
                              (const websocketpp::lib::error_code& ec) {
                if (ec || !m_running) {
                    return;
                }

                // ---- broadcast logic ----
                if (shared_state_ && shared_state_->has_data() &&
                    !m_connections.empty()) {

                    auto coord_data = shared_state_->get_latest();
                    if (coord_data->sequence > last_broadcast_sequence_) {
                        broadcast(coord_data->to_json());
                        last_broadcast_sequence_ = coord_data->sequence;
                        broadcasts_sent_++;

                        if (broadcasts_sent_ % 50 == 0) {
                            std::cout << "[WebSocket] Broadcasted "
                                      << broadcasts_sent_
                                      << " updates. Latest seq: "
                                      << coord_data->sequence << std::endl;
                        }
                    }
                }

                // ---- schedule next tick ----
                next_deadline += period;

                auto now = std::chrono::steady_clock::now();
                if (next_deadline < now - period * 2) {
                    std::cerr << "[WebSocket] WARNING: Deadline drift detected, resetting"
                              << std::endl;
                    next_deadline = now + period;
                }

                schedule();
            });
        };

        schedule();

        // 6. RUN EVENT LOOP (block)
        m_server.run();

        std::cout << "[WebSocket] Total broadcasts sent: "
                  << broadcasts_sent_ << std::endl;

    } catch (const websocketpp::exception& e) {
        std::cerr << "[WebSocket] Exception: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Error: " << e.what() << std::endl;
    }
}

void WebSocketServer::stop() {
    if (!m_running.load()) {
        return;
    }
    
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

void WebSocketServer::set_shared_state(std::shared_ptr<SharedCoordinateState> state) {
    shared_state_ = state;
}