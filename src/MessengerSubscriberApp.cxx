#include "MessengerSubscriberApp.hpp"
#include "WebSocketServer.hpp"

#include <condition_variable>
#include <stdexcept>
#include <sstream>

#include <fastdds/dds/core/status/SubscriptionMatchedStatus.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>

#include "MessengerPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;


MessengerSubscriberApp::MessengerSubscriberApp(const int& domain_id)
    : factory_(nullptr)
    , participant_(nullptr)
    , subscriber_(nullptr)
    , topic_(nullptr)
    , reader_(nullptr)
    , type_(new Messenger::MessagePubSubType())
    , samples_received_(0)
    , stop_(false)
{
    // Create the participant
    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;
    pqos.name("Messenger::Message_sub_participant");
    factory_ = DomainParticipantFactory::get_shared_instance();
    participant_ = factory_->create_participant(domain_id, pqos, nullptr, StatusMask::none());
    if (participant_ == nullptr)
    {
        throw std::runtime_error("Messenger::Message Participant initialization failed");
    }

    // Register the type
    type_.register_type(participant_);

    // Create the subscriber
    SubscriberQos sub_qos = SUBSCRIBER_QOS_DEFAULT;
    participant_->get_default_subscriber_qos(sub_qos);
    subscriber_ = participant_->create_subscriber(sub_qos, nullptr, StatusMask::none());
    if (subscriber_ == nullptr)
    {
        throw std::runtime_error("Messenger::Message Subscriber initialization failed");
    }

    // Create the topic
    TopicQos topic_qos = TOPIC_QOS_DEFAULT;
    participant_->get_default_topic_qos(topic_qos);
    topic_ = participant_->create_topic("Movie Discussion List", type_.get_type_name(), topic_qos);
    if (topic_ == nullptr)
    {
        throw std::runtime_error("Messenger::Message Topic initialization failed");
    }

    // Create the reader
    DataReaderQos reader_qos = DATAREADER_QOS_DEFAULT;
    subscriber_->get_default_datareader_qos(reader_qos);
    reader_qos.reliability().kind = ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;
    
    // reader_qos.representation().m_value.clear();
    // reader_qos.representation().m_value.push_back(DataRepresentationId_t::XCDR2_DATA_REPRESENTATION);
    
    reader_qos.durability().kind = DurabilityQosPolicyKind::TRANSIENT_LOCAL_DURABILITY_QOS;
    reader_qos.history().kind = HistoryQosPolicyKind::KEEP_ALL_HISTORY_QOS;
    

    reader_ = subscriber_->create_datareader(topic_, reader_qos, this, StatusMask::all());
    if (reader_ == nullptr)
    {
        throw std::runtime_error("Messenger::Message DataReader initialization failed");
    }
}

MessengerSubscriberApp::~MessengerSubscriberApp()
{
    if (nullptr != participant_)
    {
        // Delete DDS entities contained within the DomainParticipant
        participant_->delete_contained_entities();

        // Delete DomainParticipant
        factory_->delete_participant(participant_);
    }
}

void MessengerSubscriberApp::on_subscription_matched(
        DataReader* /*reader*/,
        const SubscriptionMatchedStatus& info)
{
    if (info.current_count_change == 1)
    {
        std::cout << "Messenger::Message Subscriber matched." << std::endl;
    }
    else if (info.current_count_change == -1)
    {
        std::cout << "Messenger::Message Subscriber unmatched." << std::endl;
    }
    else
    {
        std::cout << info.current_count_change
                  << " is not a valid value for SubscriptionMatchedStatus current count change" << std::endl;
    }
}

void MessengerSubscriberApp::set_websocket_server(std::shared_ptr<WebSocketServer> ws_server)
{
    ws_server_ = ws_server;
}

void MessengerSubscriberApp::on_data_available(DataReader* reader)
{
    Messenger::Message sample_;
    SampleInfo info;
    
    while ((!is_stopped()) && (RETCODE_OK == reader->take_next_sample(&sample_, &info)))
    {
        if ((info.instance_state == ALIVE_INSTANCE_STATE) && info.valid_data)
        {
            samples_received_++;
            
            // Parse tọa độ từ text field (format: "lon,lat,timestamp")
            std::string text = sample_.text();
            std::istringstream iss(text);
            std::string lon_str, lat_str, time_str;
            
            if (std::getline(iss, lon_str, ',') && 
                std::getline(iss, lat_str, ',') && 
                std::getline(iss, time_str))
            {
                double lon = std::stod(lon_str);
                double lat = std::stod(lat_str);
                int64_t timestamp = std::stoll(time_str);
                
                // Log mỗi 100 samples
                if (samples_received_ % 100 == 0) {
                    std::cout << "[Subscriber] Sample #" << samples_received_ 
                             << " - Coords: [" << lon << ", " << lat 
                             << "] at " << timestamp << "ms" << std::endl;
                }
                
                // Forward qua WebSocket nếu có
                if (ws_server_)
                {
                    std::ostringstream json;
                    json << "{\"coords\":[" << std::fixed << std::setprecision(8) 
                         << lon << "," << lat << "],"
                         << "\"time\":" << timestamp 
                         << ",\"sample_id\":" << samples_received_ << "}";
                    
                    ws_server_->broadcast(json.str());
                }
            }
            else
            {
                std::cout << "[Subscriber] Sample #" << samples_received_ 
                         << " RECEIVED (unparsed)" << std::endl;
            }
        }
    }
}

void MessengerSubscriberApp::run()
{
    std::unique_lock<std::mutex> lck(terminate_cv_mtx_);
    terminate_cv_.wait(lck, [this]
            {
                return is_stopped();
            });
}

bool MessengerSubscriberApp::is_stopped()
{
    return stop_.load();
}

void MessengerSubscriberApp::stop()
{
    stop_.store(true);
    terminate_cv_.notify_all();
}