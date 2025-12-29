#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <fastdds/dds/log/Log.hpp>
#include "MessengerApplication.hpp"
#include "WebSocketServer.hpp"

using eprosima::fastdds::dds::Log;

std::function<void(int)> stop_handler;

void signal_handler(
        int signum)
{
    stop_handler(signum);
}

std::string parse_signal(
        const int& signum)
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
#endif // _WIN32
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
    std::shared_ptr<WebSocketServer> ws_server;  // Shared pointer
    
    if (argc != 2 || (strcmp(argv[1], "publisher") != 0 && strcmp(argv[1], "subscriber") != 0))
    {
        std::cout << "Error: Incorrect arguments." << std::endl;
        std::cout << "Usage: " << std::endl << std::endl;
        std::cout << argv[0] << " publisher|subscriber" << std::endl << std::endl;
        ret = EXIT_FAILURE;
    }
    else
    {
        try
        {
            app = MessengerApplication::make_app(domain_id, argv[1]);
        }
        catch (const std::runtime_error& e)
        {
            EPROSIMA_LOG_ERROR(app_name, e.what());
            ret = EXIT_FAILURE;
        }
        
        std::thread app_thread(&MessengerApplication::run, app);
        
        int ws_port = (strcmp(argv[1], "publisher") == 0) ? 8081 : 8082;
        std::cout << "WebSocket running on port: " << ws_port << std::endl;
        
        ws_server = std::make_shared<WebSocketServer>();
        std::thread ws_thread([ws_server, ws_port]{ 
            ws_server->run(ws_port);  
        });
        
        std::cout << argv[1] << " running. Please press Ctrl+C to stop the " << argv[1] << " at any time." << std::endl;
        
        stop_handler = [&](int signum)
        {
            std::cout << "\n" << parse_signal(signum) << " received, stopping " << argv[1]
                      << " execution." << std::endl;
            app->stop();
            ws_server->stop();  // Dừng WebSocket
        };
        
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
#ifndef _WIN32
        signal(SIGQUIT, signal_handler);
        signal(SIGHUP, signal_handler);
#endif
        
        app_thread.join();
        ws_thread.join();
    }
    
    Log::Reset();
    return ret;
}