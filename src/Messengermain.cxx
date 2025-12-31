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
            // Tạo DDS application
            app = MessengerApplication::make_app(domain_id, argv[1]);
            
            // Khởi tạo WebSocket server
            int ws_port = is_publisher ? 8081 : 8082;
            ws_server = std::make_shared<WebSocketServer>();
            
            // Liên kết WebSocket với DDS app
            if (is_publisher)
            {
                auto pub_app = std::dynamic_pointer_cast<MessengerPublisherApp>(app);
                if (pub_app) {
                    pub_app->set_websocket_server(ws_server);
                    std::cout << "========================================" << std::endl;
                    std::cout << "   COORDINATE PUBLISHER" << std::endl;
                    std::cout << "========================================" << std::endl;
                    std::cout << "DDS Domain ID: " << domain_id << std::endl;
                    std::cout << "DDS Topic: Movie Discussion List" << std::endl;
                    std::cout << "WebSocket: ws://localhost:" << ws_port << std::endl;
                    std::cout << "Frequency: ~50Hz (20ms interval)" << std::endl;
                    std::cout << "Pattern: Figure-8 trajectory" << std::endl;
                    std::cout << "Center: [107.02243, 20.76300]" << std::endl;
                    std::cout << "========================================" << std::endl;
                }
            }
            else
            {
                auto sub_app = std::dynamic_pointer_cast<MessengerSubscriberApp>(app);
                if (sub_app) {
                    sub_app->set_websocket_server(ws_server);
                    std::cout << "========================================" << std::endl;
                    std::cout << "   COORDINATE SUBSCRIBER" << std::endl;
                    std::cout << "========================================" << std::endl;
                    std::cout << "DDS Domain ID: " << domain_id << std::endl;
                    std::cout << "DDS Topic: Movie Discussion List" << std::endl;
                    std::cout << "WebSocket: ws://localhost:" << ws_port << std::endl;
                    std::cout << "Mode: Receive & Forward" << std::endl;
                    std::cout << "========================================" << std::endl;
                }
            }
            
            // Chạy DDS app thread
            std::thread app_thread(&MessengerApplication::run, app);
            
            // Chạy WebSocket server thread
            std::thread ws_thread([ws_server, ws_port]{ 
                ws_server->run(ws_port);  
            });
            
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
            ws_thread.join();
            
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