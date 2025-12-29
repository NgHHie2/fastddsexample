cmake_minimum_required(VERSION 3.20)

project("generated_code")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(fastcdr REQUIRED)
find_package(fastdds 3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

find_path(WEBSOCKETPP_INCLUDE_DIR websocketpp/config/asio.hpp)
if(NOT WEBSOCKETPP_INCLUDE_DIR)
    message(FATAL_ERROR "Cannot find websocketpp headers")
endif()

include_directories(${WEBSOCKETPP_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})

message(STATUS "Configuring Messenger...")
add_library(Messenger_lib
    src/MessengerTypeObjectSupport.cxx
    src/MessengerPubSubTypes.cxx
)
target_link_libraries(Messenger_lib fastcdr fastdds)

add_executable(Messenger
    src/MessengerApplication.cxx
    src/MessengerPublisherApp.cxx
    src/MessengerSubscriberApp.cxx
    src/Messengermain.cxx
    src/WebSocketServer.cpp
)
target_link_libraries(Messenger
    fastcdr
    fastdds
    Messenger_lib
    ${Boost_LIBRARIES}       
)

target_include_directories(Messenger PRIVATE ${WEBSOCKETPP_INCLUDE_DIR})
