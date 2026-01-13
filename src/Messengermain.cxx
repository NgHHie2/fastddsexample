#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <fastdds/dds/log/Log.hpp>
#include "MessengerApplication.hpp"
#include "MessengerPublisherApp.hpp"
#include "MessengerSubscriberApp.hpp"
#include "WebSocketServer.hpp"
#include "SharedCoordinateState.hpp"
#include "CoordinateProducer.hpp"

using eprosima::fastdds::dds::Log;

std::function<void(int)> stop_handler;

void signal_handler(int signum)
{
    stop_handler(signum);
}

std::string parse_signal(const int& signum)
{
    switch (signum)
    {
        case SIGINT:
            return "SIGINT";
        case SIGTERM:
            return "SIGTERM";
#ifndef _WIN32
        case SIGQUIT:
            return "SIGQUIT";
        case SIGHUP:
            return "SIGHUP";
#endif
        default:
            return "UNKNOWN SIGNAL";
    }
}

int main(int argc, char** argv)
{
    Log::SetVerbosity(Log::Kind::Info);
    auto ret = EXIT_SUCCESS;
    int domain_id = 42;
    
    std::shared_ptr<MessengerApplication> app;
    std::shared_ptr<WebSocketServer> ws_server;
    std::shared_ptr<CoordinateProducer> coord_producer;
    std::shared_ptr<SharedCoordinateState> shared_state;
    
    if (argc != 2 || (strcmp(argv[1], "publisher") != 0 && strcmp(argv[1], "subscriber") != 0))
    {
        std::cout << "Error: Incorrect arguments." << std::endl;
        std::cout << "Usage: " << std::endl << std::endl;
        std::cout << argv[0] << " publisher|subscriber" << std::endl << std::endl;
        std::cout << std::endl;
        std::cout << "Description:" << std::endl;
        std::cout << "  publisher  - Generates figure-8 GPS coordinates and broadcasts via DDS + WebSocket" << std::endl;
        std::cout << "  subscriber - Receives coordinates from DDS and forwards to WebSocket clients" << std::endl;
        std::cout << std::endl;
        std::cout << "Architecture:" << std::endl;
        std::cout << "  - CoordinateProducer: Generates coordinates at 50Hz (20ms)" << std::endl;
        std::cout << "  - DDS Publisher: Publishes at 20Hz (50ms) from shared state" << std::endl;
        std::cout << "  - WebSocket: Broadcasts at 10Hz (100ms) from shared state" << std::endl;
        std::cout << std::endl;
        std::cout << "WebSocket Ports:" << std::endl;
        std::cout << "  Publisher:  ws://localhost:8081" << std::endl;
        std::cout << "  Subscriber: ws://localhost:8082" << std::endl;
        ret = EXIT_FAILURE;
    }
    else
    {
        bool is_publisher = (strcmp(argv[1], "publisher") == 0);
        
        try
        {
            if (is_publisher)
            {
                std::cout << "========================================" << std::endl;
                std::cout << "   COORDINATE PUBLISHER SYSTEM" << std::endl;
                std::cout << "========================================" << std::endl;
                std::cout << "Architecture: Producer-Consumer Model" << std::endl;
                std::cout << "DDS Domain ID: " << domain_id << std::endl;
                std::cout << "DDS Topic: Movie Discussion List" << std::endl;
                std::cout << std::endl;
                
                // 1. Tạo shared state
                shared_state = std::make_shared<SharedCoordinateState>();
                
                // 2. Tạo coordinate producer (50Hz)
                coord_producer = std::make_shared<CoordinateProducer>(
                    shared_state,
                    std::chrono::milliseconds(20),  // 50Hz
                    107.02243,  // center_lon
                    20.76300    // center_lat
                );
                
                // 3. Tạo DDS publisher app (20Hz)
                app = MessengerApplication::make_app(domain_id, argv[1]);
                auto pub_app = std::dynamic_pointer_cast<MessengerPublisherApp>(app);
                if (pub_app) {
                    pub_app->set_shared_state(shared_state);
                }
                
                // 4. Tạo WebSocket server (10Hz)
                ws_server = std::make_shared<WebSocketServer>(100); // 10Hz
                ws_server->set_shared_state(shared_state);
                
                std::cout << "Components:" << std::endl;
                std::cout << "  [1] CoordinateProducer: 50Hz (generates coordinates)" << std::endl;
                std::cout << "  [2] DDS Publisher:      20Hz (publishes to DDS)" << std::endl;
                std::cout << "  [3] WebSocket Server:   10Hz (broadcasts to clients + handles connections)" << std::endl;
                std::cout << "  [4] Shared State:       Atomic thread-safe buffer" << std::endl;
                std::cout << std::endl;
                std::cout << "WebSocket: ws://localhost:8081" << std::endl;
                std::cout << "Pattern: Figure-8 trajectory" << std::endl;
                std::cout << "Center: [107.02243, 20.76300]" << std::endl;
                std::cout << "========================================" << std::endl;
                
                // Start threads
                std::thread producer_thread(&CoordinateProducer::run, coord_producer);
                std::thread dds_thread(&MessengerApplication::run, app);
                std::thread ws_thread([ws_server]{ ws_server->run(8081); });
                
                std::cout << std::endl;
                std::cout << "System running. Press Ctrl+C to stop." << std::endl;
                std::cout << std::endl;
                
                // Setup signal handler
                stop_handler = [&](int signum)
                {
                    std::cout << "\n" << parse_signal(signum) << " received, shutting down..." << std::endl;
                    coord_producer->stop();
                    app->stop();
                    ws_server->stop();
                };
                
                signal(SIGINT, signal_handler);
                signal(SIGTERM, signal_handler);
#ifndef _WIN32
                signal(SIGQUIT, signal_handler);
                signal(SIGHUP, signal_handler);
#endif
                
                // Wait for threads
                producer_thread.join();
                dds_thread.join();
                ws_thread.join();
            }
            else  // subscriber
            {
                std::cout << "========================================" << std::endl;
                std::cout << "   COORDINATE SUBSCRIBER" << std::endl;
                std::cout << "========================================" << std::endl;
                std::cout << "DDS Domain ID: " << domain_id << std::endl;
                std::cout << "DDS Topic: Movie Discussion List" << std::endl;
                std::cout << "WebSocket: ws://localhost:8082" << std::endl;
                std::cout << "Mode: Receive & Forward" << std::endl;
                std::cout << "========================================" << std::endl;
                
                // Tạo DDS application
                app = MessengerApplication::make_app(domain_id, argv[1]);
                
                // Khởi tạo WebSocket server
                // ws_server = std::make_shared<WebSocketServer>();
                
                auto sub_app = std::dynamic_pointer_cast<MessengerSubscriberApp>(app);
                if (sub_app) {
                    sub_app->set_websocket_server(ws_server);
                }
                
                // Chạy DDS app thread
                std::thread app_thread(&MessengerApplication::run, app);
                
                // Chạy WebSocket server thread
                // std::thread ws_thread([ws_server]{ ws_server->run(8082); });
                
                std::cout << std::endl;
                std::cout << "System running. Press Ctrl+C to stop." << std::endl;
                std::cout << std::endl;
                
                // Setup signal handler
                stop_handler = [&](int signum)
                {
                    std::cout << "\n" << parse_signal(signum) << " received, shutting down..." << std::endl;
                    app->stop();
                    ws_server->stop();
                };
                
                signal(SIGINT, signal_handler);
                signal(SIGTERM, signal_handler);
#ifndef _WIN32
                signal(SIGQUIT, signal_handler);
                signal(SIGHUP, signal_handler);
#endif
                
                // Wait for threads
                app_thread.join();
                // ws_thread.join();
            }
            
            std::cout << "Shutdown complete." << std::endl;
        }
        catch (const std::runtime_error& e)
        {
            EPROSIMA_LOG_ERROR(app_name, e.what());
            ret = EXIT_FAILURE;
        }
    }
    
    Log::Reset();
    return ret;
}