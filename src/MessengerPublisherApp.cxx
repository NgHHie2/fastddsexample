#include "MessengerPublisherApp.hpp"

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
    , last_published_sequence_(0)
    , stop_(false)
{
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
        std::cout << "[DDS Publisher] Matched with subscriber." << std::endl;
        cv_.notify_one();
    }
    else if (info.current_count_change == -1)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            matched_ = info.current_count;
        }
        std::cout << "[DDS Publisher] Unmatched from subscriber." << std::endl;
    }
    else
    {
        std::cout << info.current_count_change
                  << " is not a valid value for PublicationMatchedStatus current count change" << std::endl;
    }
}

void MessengerPublisherApp::run()
{
    if (!shared_state_) {
        std::cerr << "[DDS Publisher] ERROR: Shared state not set!" << std::endl;
        return;
    }
    
    std::cout << "[DDS Publisher] Starting at ~" 
              << (1000.0 / dds_publish_rate_ms_) << "Hz" << std::endl;
    std::cout << "[DDS Publisher] Reading from shared coordinate state" << std::endl;
    
    // Deadline-based timing để tránh drift
    auto period = std::chrono::milliseconds(dds_publish_rate_ms_);
    auto next_deadline = std::chrono::steady_clock::now() + period;
    
    while (!is_stopped())
    {
        if (publish_from_shared_state())
        {
            samples_sent_++;
            if (samples_sent_ % 50 == 0) {  // Log mỗi 50 samples
                auto latest = shared_state_->get_latest();
                std::cout << "[DDS Publisher] Sent " << samples_sent_ 
                         << " samples. Latest seq: " << latest->sequence << std::endl;
            }
        }
        
        // Sleep đến deadline tiếp theo (bù trừ thời gian xử lý)
        std::unique_lock<std::mutex> period_lock(mutex_);
        cv_.wait_until(period_lock, next_deadline, [this]()
                {
                    return is_stopped();
                });
        
        // Cập nhật deadline cho lần tiếp theo
        next_deadline += period;
        
        // Nếu bị trễ quá nhiều, reset deadline
        auto now = std::chrono::steady_clock::now();
        if (next_deadline < now - period * 2) {
            std::cerr << "[DDS Publisher] WARNING: Deadline drift detected, resetting" << std::endl;
            next_deadline = now + period;
        }
    }
    
    std::cout << "[DDS Publisher] Total samples published: " << samples_sent_ << std::endl;
}

void MessengerPublisherApp::set_shared_state(std::shared_ptr<SharedCoordinateState> state)
{
    shared_state_ = state;
}

bool MessengerPublisherApp::publish_from_shared_state()
{
    if (!shared_state_ || !shared_state_->has_data()) {
        return false;
    }
    
    bool ret = false;
    
    // Wait for the data endpoints discovery
    std::unique_lock<std::mutex> matched_lock(mutex_);
    cv_.wait(matched_lock, [&]()
            {
                return ((matched_ > 0) || is_stopped());
            });

    if (!is_stopped())
    {
        // Đọc tọa độ mới nhất từ shared state
        auto coord_data = shared_state_->get_latest();
        
        // Chỉ publish nếu có data mới (sequence khác)
        if (coord_data->sequence <= last_published_sequence_) {
            return false;
        }
        
        // Tạo DDS message
        Messenger::Message sample_;
        sample_.from("CoordinatePublisher");
        sample_.subject("GPS_Coordinates");
        sample_.subject_id(1);
        sample_.text(coord_data->to_csv());
        sample_.count(coord_data->sequence);
        
        ret = (RETCODE_OK == writer_->write(&sample_));
        
        if (ret) {
            last_published_sequence_ = coord_data->sequence;
        }
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