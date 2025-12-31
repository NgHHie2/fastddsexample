#include "MessengerPublisherApp.hpp"
#include "WebSocketServer.hpp"
#include "CoordinateGenerator.hpp"

#include <condition_variable>
#include <csignal>
#include <stdexcept>
#include <thread>
#include <sstream>
#include <iomanip>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/log/Log.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>

#include "MessengerPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

MessengerPublisherApp::MessengerPublisherApp(
        const int& domain_id)
    : factory_(nullptr)
    , participant_(nullptr)
    , publisher_(nullptr)
    , topic_(nullptr)
    , writer_(nullptr)
    , type_(new Messenger::MessagePubSubType())
    , matched_(0)
    , samples_sent_(0)
    , stop_(false)
{
    //

    // Create the participant
    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;
    pqos.name("Messenger::Message_pub_participant");
    factory_ = DomainParticipantFactory::get_shared_instance();
    participant_ = factory_->create_participant(domain_id, pqos, nullptr, StatusMask::none());
    if (participant_ == nullptr)
    {
        throw std::runtime_error("Messenger::Message Participant initialization failed");
    }

    // Register the type
    type_.register_type(participant_);

    // Create the publisher
    PublisherQos pub_qos = PUBLISHER_QOS_DEFAULT;
    participant_->get_default_publisher_qos(pub_qos);
    publisher_ = participant_->create_publisher(pub_qos, nullptr, StatusMask::none());
    if (publisher_ == nullptr)
    {
        throw std::runtime_error("Messenger::Message Publisher initialization failed");
    }

    // Create the topic
    TopicQos topic_qos = TOPIC_QOS_DEFAULT;
    participant_->get_default_topic_qos(topic_qos);
    topic_ = participant_->create_topic("Movie Discussion List", type_.get_type_name(), topic_qos);
    if (topic_ == nullptr)
    {
        throw std::runtime_error("Messenger::Message Topic initialization failed");
    }

    // Create the data writer
    DataWriterQos writer_qos = DATAWRITER_QOS_DEFAULT;
    publisher_->get_default_datawriter_qos(writer_qos);
    writer_qos.reliability().kind = ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;
    writer_qos.durability().kind = DurabilityQosPolicyKind::TRANSIENT_LOCAL_DURABILITY_QOS;
    writer_qos.history().kind = HistoryQosPolicyKind::KEEP_ALL_HISTORY_QOS;
    writer_ = publisher_->create_datawriter(topic_, writer_qos, this, StatusMask::all());
    if (writer_ == nullptr)
    {
        throw std::runtime_error("Messenger::Message DataWriter initialization failed");
    }
}

MessengerPublisherApp::~MessengerPublisherApp()
{
    if (nullptr != participant_)
    {
        // Delete DDS entities contained within the DomainParticipant
        participant_->delete_contained_entities();

        // Delete DomainParticipant
        factory_->delete_participant(participant_);
    }
}

void MessengerPublisherApp::on_publication_matched(
        DataWriter* /*writer*/,
        const PublicationMatchedStatus& info)
{
    if (info.current_count_change == 1)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            matched_ = info.current_count;
        }
        std::cout << "Messenger::Message Publisher matched." << std::endl;
        cv_.notify_one();
    }
    else if (info.current_count_change == -1)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            matched_ = info.current_count;
        }
        std::cout << "Messenger::Message Publisher unmatched." << std::endl;
    }
    else
    {
        std::cout << info.current_count_change
                  << " is not a valid value for PublicationMatchedStatus current count change" << std::endl;
    }
}

void MessengerPublisherApp::run()
{
    CoordinateGenerator coord_gen;
    
    std::cout << "[Publisher] Starting coordinate broadcast..." << std::endl;
    std::cout << "[Publisher] Broadcasting to DDS topic: 'Movie Discussion List'" << std::endl;
    if (ws_server_) {
        std::cout << "[Publisher] Broadcasting to WebSocket clients" << std::endl;
    }
    
    while (!is_stopped())
    {
        // Lấy tọa độ mới
        auto coords = coord_gen.get_next_coordinate();
        auto timestamp = CoordinateGenerator::get_timestamp();
        
        if (publish_coordinates(coords.first, coords.second, timestamp))
        {
            samples_sent_++;
            if (samples_sent_ % 100 == 0) {  // Log mỗi 100 samples
                std::cout << "[Publisher] Sent " << samples_sent_ 
                         << " samples. Latest: [" << coords.first 
                         << ", " << coords.second << "]" << std::endl;
            }
        }
        
        // Delay ~20ms => ~50Hz
        std::unique_lock<std::mutex> period_lock(mutex_);
        cv_.wait_for(period_lock, std::chrono::milliseconds(20), [this]()
                {
                    return is_stopped();
                });
    }
    
    std::cout << "[Publisher] Total samples sent: " << samples_sent_ << std::endl;
}

void MessengerPublisherApp::set_websocket_server(std::shared_ptr<WebSocketServer> ws_server)
{
    ws_server_ = ws_server;
}

bool MessengerPublisherApp::publish_coordinates(double lon, double lat, int64_t timestamp)
{
    bool ret = false;
    
    // Wait for the data endpoints discovery
    std::unique_lock<std::mutex> matched_lock(mutex_);
    cv_.wait(matched_lock, [&]()
            {
                return ((matched_ > 0) || is_stopped());
            });

    if (!is_stopped())
    {
        // 1. Gửi qua DDS
        Messenger::Message sample_;
        sample_.from("CoordinatePublisher");
        sample_.subject("GPS_Coordinates");
        sample_.subject_id(1);
        
        // Format: "lon,lat,timestamp"
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(8) 
            << lon << "," << lat << "," << timestamp;
        sample_.text(oss.str());
        sample_.count(samples_sent_ + 1);
        
        ret = (RETCODE_OK == writer_->write(&sample_));
        
        // 2. Gửi qua WebSocket (nếu có)
        if (ws_server_ && ret && (samples_sent_ % 5 == 0))
        {
            std::ostringstream json;
                    json << "{\"coords\":[" << std::fixed << std::setprecision(8) 
                        << lon << "," << lat << "],"
                        << "\"time\":" << timestamp << "}";
                        
            ws_server_->broadcast(json.str());
        }
    }
    
    return ret;
}

bool MessengerPublisherApp::publish()
{
    bool ret = false;
    // Wait for the data endpoints discovery
    std::unique_lock<std::mutex> matched_lock(mutex_);
    cv_.wait(matched_lock, [&]()
            {
                // at least one has been discovered
                return ((matched_ > 0) || is_stopped());
            });

    if (!is_stopped())
    {
        /* Initialize your structure here */
        Messenger::Message sample_;
        ret = (RETCODE_OK == writer_->write(&sample_));
    }
    return ret;
}

bool MessengerPublisherApp::is_stopped()
{
    return stop_.load();
}

void MessengerPublisherApp::stop()
{
    stop_.store(true);
    cv_.notify_one();
}